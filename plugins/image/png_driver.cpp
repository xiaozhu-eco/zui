#include "image/driver.h"

#include <png.h>

#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace zui {
namespace image {
namespace {

struct PngSink {
    std::uint8_t* volatile data;
    std::size_t size;
    std::size_t cap;
};

struct PngIo {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    std::size_t pos = 0;
    PngSink* sink = nullptr;
};

struct PngSession {
    jmp_buf jmp;
    char message[160];
    png_structp volatile png;
    png_infop volatile info;
    std::uint8_t* volatile pixels;
    png_bytep* volatile rows;
    PngSink* volatile sink;
};

void png_error_exit(png_structp png, png_const_charp message) {
    auto* session = static_cast<PngSession*>(png_get_error_ptr(png));
    if (session == nullptr) {
        return;
    }
    if (message != nullptr) {
        std::snprintf(session->message, sizeof(session->message), "%s", message);
    } else {
        std::snprintf(session->message, sizeof(session->message), "png error");
    }
    longjmp(session->jmp, 1);
}

void png_warning(png_structp, png_const_charp) {}

void png_read_bytes(png_structp png, png_bytep out, png_size_t count) {
    auto* io = static_cast<PngIo*>(png_get_io_ptr(png));
    if (io == nullptr || io->pos + count > io->size) {
        png_error(png, "png truncated");
    }
    std::memcpy(out, io->data + io->pos, count);
    io->pos += count;
}

void png_write_bytes(png_structp png, png_bytep data, png_size_t count) {
    auto* io = static_cast<PngIo*>(png_get_io_ptr(png));
    if (io == nullptr || io->sink == nullptr) {
        png_error(png, "png write failed");
    }
    PngSink* sink = io->sink;
    if (sink->size + count > sink->cap) {
        std::size_t cap = sink->cap == 0 ? 4096 : sink->cap;
        while (cap < sink->size + count) {
            cap *= 2;
        }
        auto* grown = static_cast<std::uint8_t*>(std::realloc(sink->data, cap));
        if (grown == nullptr) {
            png_error(png, "out of memory");
        }
        sink->data = grown;
        sink->cap = cap;
    }
    std::memcpy(sink->data + sink->size, data, count);
    sink->size += count;
}

void png_flush_bytes(png_structp) {}

void destroy_read(PngSession& session) {
    std::free(session.pixels);
    std::free(session.rows);
    session.pixels = nullptr;
    session.rows = nullptr;
    png_structp png = session.png;
    png_infop info = session.info;
    if (png != nullptr) {
        png_destroy_read_struct(&png, info != nullptr ? &info : nullptr, nullptr);
    }
    session.png = nullptr;
    session.info = nullptr;
}

void destroy_write(PngSession& session) {
    std::free(session.rows);
    session.rows = nullptr;
    if (session.sink != nullptr) {
        std::free(session.sink->data);
        std::free(session.sink);
        session.sink = nullptr;
    }
    png_structp png = session.png;
    png_infop info = session.info;
    if (png != nullptr) {
        png_destroy_write_struct(&png, info != nullptr ? &info : nullptr);
    }
    session.png = nullptr;
    session.info = nullptr;
}

class PngDriver final : public Driver {
public:
    const char* name() const override { return "PNG"; }

    std::vector<std::string> suffixes() const override { return {"png"}; }

    bool decode_file(const std::string& path, const std::uint8_t* bytes, std::size_t size, Raster& out,
                     std::string& error) const override {
        if (path.empty() && (bytes == nullptr || size == 0)) {
            error = "empty png";
            return false;
        }
        return decode(bytes, size, out, error);
    }

    bool decode(const std::uint8_t* bytes, std::size_t size, Raster& out, std::string& error) const override {
        out = Raster{};
        if (bytes == nullptr || size < 8) {
            error = "png too small";
            return false;
        }
        PngSession session{};
        session.message[0] = '\0';
        session.png = png_create_read_struct(PNG_LIBPNG_VER_STRING, &session, png_error_exit, png_warning);
        if (session.png == nullptr) {
            error = "png_create_read_struct failed";
            return false;
        }
        session.info = png_create_info_struct(session.png);
        if (session.info == nullptr) {
            destroy_read(session);
            error = "png_create_info_struct failed";
            return false;
        }
        PngIo io;
        io.data = bytes;
        io.size = size;
        if (setjmp(session.jmp)) {
            error = session.message[0] != '\0' ? session.message : "png read failed";
            destroy_read(session);
            return false;
        }
        png_set_read_fn(session.png, &io, png_read_bytes);
        png_read_info(session.png, session.info);
        const auto width = png_get_image_width(session.png, session.info);
        const auto height = png_get_image_height(session.png, session.info);
        if (!dimensions_ok(static_cast<int>(width), static_cast<int>(height))) {
            png_error(session.png, "png dimensions are out of range");
        }
        const int color = png_get_color_type(session.png, session.info);
        const int depth = png_get_bit_depth(session.png, session.info);
        if (depth == 16) {
            png_set_strip_16(session.png);
        }
        if (color == PNG_COLOR_TYPE_PALETTE) {
            png_set_palette_to_rgb(session.png);
        }
        if (color == PNG_COLOR_TYPE_GRAY && depth < 8) {
            png_set_expand_gray_1_2_4_to_8(session.png);
        }
        if (png_get_valid(session.png, session.info, PNG_INFO_tRNS) != 0) {
            png_set_tRNS_to_alpha(session.png);
        }
        if (color == PNG_COLOR_TYPE_GRAY || color == PNG_COLOR_TYPE_GRAY_ALPHA) {
            png_set_gray_to_rgb(session.png);
        }
        const bool has_alpha = (color & PNG_COLOR_MASK_ALPHA) != 0 ||
                               png_get_valid(session.png, session.info, PNG_INFO_tRNS) != 0;
        if (!has_alpha) {
            png_set_filler(session.png, 0xFF, PNG_FILLER_AFTER);
        }
        png_set_interlace_handling(session.png);
        png_read_update_info(session.png, session.info);
        if (png_get_rowbytes(session.png, session.info) != static_cast<png_size_t>(width) * 4) {
            png_error(session.png, "png row is not RGBA8");
        }
        const std::size_t nbytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
        session.pixels = static_cast<std::uint8_t*>(std::malloc(nbytes));
        session.rows = static_cast<png_bytep*>(std::malloc(sizeof(png_bytep) * height));
        if (session.pixels == nullptr || session.rows == nullptr) {
            png_error(session.png, "out of memory");
        }
        for (png_uint_32 y = 0; y < height; ++y) {
            session.rows[y] = session.pixels + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4;
        }
        png_read_image(session.png, session.rows);
        png_read_end(session.png, nullptr);
        out.width = static_cast<int>(width);
        out.height = static_cast<int>(height);
        out.rgba.assign(session.pixels, session.pixels + nbytes);
        destroy_read(session);
        return true;
    }

    bool encode(const Raster& image, std::vector<std::uint8_t>& out, std::string& error) const override {
        out.clear();
        if (!dimensions_ok(image.width, image.height)) {
            error = "png dimensions are out of range";
            return false;
        }
        const std::size_t nbytes =
            static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4;
        if (image.rgba.size() < nbytes) {
            error = "rgba buffer is short";
            return false;
        }
        PngSession session{};
        session.message[0] = '\0';
        session.png = png_create_write_struct(PNG_LIBPNG_VER_STRING, &session, png_error_exit, png_warning);
        if (session.png == nullptr) {
            error = "png_create_write_struct failed";
            return false;
        }
        session.info = png_create_info_struct(session.png);
        if (session.info == nullptr) {
            destroy_write(session);
            error = "png_create_info_struct failed";
            return false;
        }
        session.sink = static_cast<PngSink*>(std::calloc(1, sizeof(PngSink)));
        if (session.sink == nullptr) {
            destroy_write(session);
            error = "out of memory";
            return false;
        }
        PngIo io;
        io.sink = session.sink;
        if (setjmp(session.jmp)) {
            error = session.message[0] != '\0' ? session.message : "png write failed";
            destroy_write(session);
            return false;
        }
        png_set_write_fn(session.png, &io, png_write_bytes, png_flush_bytes);
        png_set_IHDR(session.png, session.info, static_cast<png_uint_32>(image.width),
                     static_cast<png_uint_32>(image.height), 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                     PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
        session.rows = static_cast<png_bytep*>(std::malloc(sizeof(png_bytep) * static_cast<std::size_t>(image.height)));
        if (session.rows == nullptr) {
            png_error(session.png, "out of memory");
        }
        for (int y = 0; y < image.height; ++y) {
            session.rows[y] = const_cast<png_byte*>(image.rgba.data() +
                                                    static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) * 4);
        }
        png_set_rows(session.png, session.info, session.rows);
        png_write_png(session.png, session.info, PNG_TRANSFORM_IDENTITY, nullptr);
        out.assign(session.sink->data, session.sink->data + session.sink->size);
        destroy_write(session);
        return true;
    }
};

}  // namespace

void register_png_driver(DriverManager& manager) {
    manager.add(std::make_unique<PngDriver>());
}

}  // namespace image
}  // namespace zui
