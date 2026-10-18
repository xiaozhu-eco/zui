#include "image/driver.h"

#include <stdio.h>

#include <csetjmp>
#include <cstdlib>
#include <cstring>
#include <string>

#include <jpeglib.h>

namespace zui {
namespace image {
namespace {

constexpr int kJpegQuality = 90;

struct MemDest {
    unsigned char* volatile data;
    unsigned long size;
};

struct JpegSession {
    jpeg_error_mgr pub;
    jmp_buf jmp;
    char message[JMSG_LENGTH_MAX];
    j_common_ptr cinfo;
    std::uint8_t* volatile pixels;
    std::uint8_t* volatile row;
    MemDest* volatile dest;
    int volatile created;
};

void jpeg_exit(j_common_ptr cinfo) {
    auto* session = reinterpret_cast<JpegSession*>(cinfo->err);
    (*cinfo->err->format_message)(cinfo, session->message);
    longjmp(session->jmp, 1);
}

void destroy_jpeg(JpegSession& session) {
    if (session.created && session.cinfo != nullptr) {
        jpeg_destroy(session.cinfo);
        session.created = 0;
    }
    std::free(session.pixels);
    std::free(session.row);
    if (session.dest != nullptr) {
        std::free(session.dest->data);
        std::free(session.dest);
        session.dest = nullptr;
    }
    session.pixels = nullptr;
    session.row = nullptr;
}

class JpegDriver final : public Driver {
public:
    const char* name() const override { return "JPEG"; }

    std::vector<std::string> suffixes() const override { return {"jpg", "jpeg"}; }

    bool decode_file(const std::string& path, const std::uint8_t* bytes, std::size_t size, Raster& out,
                     std::string& error) const override {
        if (path.empty() && (bytes == nullptr || size == 0)) {
            error = "empty jpeg";
            return false;
        }
        return decode(bytes, size, out, error);
    }

    bool decode(const std::uint8_t* bytes, std::size_t size, Raster& out, std::string& error) const override {
        out = Raster{};
        if (bytes == nullptr || size < 3 || size > 0xffffffffu) {
            error = "jpeg too small";
            return false;
        }
        jpeg_decompress_struct cinfo{};
        JpegSession session{};
        session.message[0] = '\0';
        session.cinfo = reinterpret_cast<j_common_ptr>(&cinfo);
        cinfo.err = jpeg_std_error(&session.pub);
        session.pub.error_exit = jpeg_exit;
        if (setjmp(session.jmp)) {
            error = session.message[0] != '\0' ? session.message : "jpeg read failed";
            destroy_jpeg(session);
            return false;
        }
        jpeg_create_decompress(&cinfo);
        session.created = 1;
        jpeg_mem_src(&cinfo, bytes, static_cast<unsigned long>(size));
        if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
            jpeg_destroy_decompress(&cinfo);
            session.created = 0;
            error = "jpeg header is not valid";
            return false;
        }
        cinfo.out_color_space = JCS_EXT_RGBA;
        jpeg_start_decompress(&cinfo);
        const int width = static_cast<int>(cinfo.output_width);
        const int height = static_cast<int>(cinfo.output_height);
        if (!dimensions_ok(width, height) || cinfo.output_components != 4) {
            jpeg_destroy_decompress(&cinfo);
            session.created = 0;
            error = "jpeg dimensions are out of range";
            return false;
        }
        const std::size_t nbytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
        session.pixels = static_cast<std::uint8_t*>(std::malloc(nbytes));
        if (session.pixels == nullptr) {
            jpeg_destroy_decompress(&cinfo);
            session.created = 0;
            error = "out of memory";
            return false;
        }
        while (cinfo.output_scanline < cinfo.output_height) {
            JSAMPROW row = session.pixels + static_cast<std::size_t>(cinfo.output_scanline) *
                                                static_cast<std::size_t>(width) * 4;
            jpeg_read_scanlines(&cinfo, &row, 1);
        }
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        session.created = 0;
        out.width = width;
        out.height = height;
        out.rgba.assign(session.pixels, session.pixels + nbytes);
        destroy_jpeg(session);
        return true;
    }

    bool encode(const Raster& image, std::vector<std::uint8_t>& out, std::string& error) const override {
        out.clear();
        if (!dimensions_ok(image.width, image.height)) {
            error = "jpeg dimensions are out of range";
            return false;
        }
        const std::size_t pixels =
            static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
        if (image.rgba.size() < pixels * 4) {
            error = "rgba buffer is short";
            return false;
        }
        jpeg_compress_struct cinfo{};
        JpegSession session{};
        session.message[0] = '\0';
        session.cinfo = reinterpret_cast<j_common_ptr>(&cinfo);
        cinfo.err = jpeg_std_error(&session.pub);
        session.pub.error_exit = jpeg_exit;
        session.dest = static_cast<MemDest*>(std::calloc(1, sizeof(MemDest)));
        if (session.dest == nullptr) {
            error = "out of memory";
            return false;
        }
        if (setjmp(session.jmp)) {
            error = session.message[0] != '\0' ? session.message : "jpeg write failed";
            destroy_jpeg(session);
            return false;
        }
        jpeg_create_compress(&cinfo);
        session.created = 1;
        jpeg_mem_dest(&cinfo, const_cast<unsigned char**>(&session.dest->data), &session.dest->size);
        cinfo.image_width = static_cast<JDIMENSION>(image.width);
        cinfo.image_height = static_cast<JDIMENSION>(image.height);
        cinfo.input_components = 3;
        cinfo.in_color_space = JCS_RGB;
        jpeg_set_defaults(&cinfo);
        jpeg_set_quality(&cinfo, kJpegQuality, TRUE);
        jpeg_start_compress(&cinfo, TRUE);
        session.row = static_cast<std::uint8_t*>(std::malloc(static_cast<std::size_t>(image.width) * 3));
        if (session.row == nullptr) {
            error = "out of memory";
            jpeg_destroy_compress(&cinfo);
            session.created = 0;
            destroy_jpeg(session);
            return false;
        }
        while (cinfo.next_scanline < cinfo.image_height) {
            const std::uint8_t* src =
                image.rgba.data() + static_cast<std::size_t>(cinfo.next_scanline) *
                                        static_cast<std::size_t>(image.width) * 4;
            for (int x = 0; x < image.width; ++x) {
                session.row[x * 3 + 0] = src[x * 4 + 0];
                session.row[x * 3 + 1] = src[x * 4 + 1];
                session.row[x * 3 + 2] = src[x * 4 + 2];
            }
            JSAMPROW row = session.row;
            jpeg_write_scanlines(&cinfo, &row, 1);
        }
        jpeg_finish_compress(&cinfo);
        jpeg_destroy_compress(&cinfo);
        session.created = 0;
        if (session.dest->data == nullptr || session.dest->size == 0) {
            destroy_jpeg(session);
            error = "jpeg write produced no bytes";
            return false;
        }
        out.assign(session.dest->data, session.dest->data + session.dest->size);
        destroy_jpeg(session);
        return true;
    }
};

}  // namespace

void register_jpeg_driver(DriverManager& manager) {
    manager.add(std::make_unique<JpegDriver>());
}

}  // namespace image
}  // namespace zui
