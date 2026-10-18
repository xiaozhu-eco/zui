#include "archive.h"

#include <algorithm>
#include <cstring>

#include <zlib.h>

namespace zui_tool {
namespace {

constexpr std::size_t kBlock = 512;

void put(std::vector<std::uint8_t>& out, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    out.insert(out.end(), bytes, bytes + size);
}

void put_field(std::vector<std::uint8_t>& header, std::size_t at, std::size_t width,
               const std::string& text) {
    std::memcpy(header.data() + at, text.data(), std::min(width, text.size()));
}

// Octal with a trailing NUL (or space), which is what tar expects for numbers.
std::string octal(std::uint64_t value, std::size_t width, char terminator) {
    std::string text(width, '0');
    text[width - 1] = terminator;
    for (std::size_t i = width - 1; i > 0 && value != 0; --i) {
        text[i - 1] = static_cast<char>('0' + (value & 7));
        value >>= 3;
    }
    return text;
}

std::string prefix_of(const std::string& name) { return name.substr(0, name.find('/')); }

}  // namespace

bool gzip_bytes(const std::vector<std::uint8_t>& in, std::vector<std::uint8_t>& out,
                std::string& error) {
    z_stream stream{};
    // 15 window bits + 16 selects the gzip wrapper.
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK) {
        error = "zlib: cannot start gzip";
        return false;
    }
    out.assign(in.size() / 2 + 1024, 0);
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(in.data()));
    stream.avail_in = static_cast<uInt>(in.size());
    stream.next_out = reinterpret_cast<Bytef*>(out.data());
    stream.avail_out = static_cast<uInt>(out.size());
    int code = deflate(&stream, Z_FINISH);
    while (code == Z_OK) {
        const std::size_t used = out.size();
        out.resize(used * 2);
        stream.next_out = reinterpret_cast<Bytef*>(out.data() + used);
        stream.avail_out = static_cast<uInt>(out.size() - used);
        code = deflate(&stream, Z_FINISH);
    }
    if (code != Z_STREAM_END) {
        deflateEnd(&stream);
        error = "zlib: gzip failed";
        return false;
    }
    out.resize(stream.total_out);
    deflateEnd(&stream);
    return true;
}

bool gunzip_bytes(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& out,
                  std::string& error) {
    z_stream stream{};
    if (inflateInit2(&stream, 15 + 16) != Z_OK) {
        error = "zlib: cannot start gunzip";
        return false;
    }
    out.assign(std::max<std::size_t>(size * 4, 1024), 0);
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data));
    stream.avail_in = static_cast<uInt>(size);
    for (;;) {
        const std::size_t used = static_cast<std::size_t>(stream.total_out);
        if (used == out.size()) {
            out.resize(out.size() * 2);
        }
        stream.next_out = reinterpret_cast<Bytef*>(out.data() + used);
        stream.avail_out = static_cast<uInt>(out.size() - used);
        const int code = inflate(&stream, Z_NO_FLUSH);
        if (code == Z_STREAM_END) {
            break;
        }
        if (code != Z_OK) {
            inflateEnd(&stream);
            error = "zlib: the stream does not gunzip";
            return false;
        }
    }
    out.resize(stream.total_out);
    inflateEnd(&stream);
    return true;
}

void TarWriter::append_header(const std::string& name, std::uint64_t size, std::uint32_t mode,
                              char type) {
    std::vector<std::uint8_t> header(kBlock, 0);
    std::string prefix;
    std::string short_name = name;
    if (short_name.size() > 100) {
        // ustar splits the path at a slash: prefix + name must stay inside their
        // fields.
        const std::size_t cut = short_name.rfind('/', 155);
        if (cut != std::string::npos && short_name.size() - cut - 1 <= 100) {
            prefix = short_name.substr(0, cut);
            short_name = short_name.substr(cut + 1);
        } else {
            short_name = short_name.substr(short_name.size() - 100);
        }
    }
    put_field(header, 0, 100, short_name);
    put_field(header, 100, 8, octal(mode, 8, '\0'));
    put_field(header, 108, 8, octal(0, 8, '\0'));   // uid
    put_field(header, 116, 8, octal(0, 8, '\0'));   // gid
    put_field(header, 124, 12, octal(size, 12, '\0'));
    put_field(header, 136, 12, octal(0, 12, '\0'));  // mtime: a repeatable build
    std::memset(header.data() + 148, ' ', 8);        // chksum, filled in below
    header[156] = static_cast<std::uint8_t>(type);
    put_field(header, 257, 6, "ustar");
    put_field(header, 263, 2, "00");
    put_field(header, 265, 32, "root");
    put_field(header, 297, 32, "root");
    put_field(header, 345, 155, prefix);
    unsigned sum = 0;
    for (std::uint8_t byte : header) {
        sum += byte;
    }
    put_field(header, 148, 7, octal(sum, 7, '\0'));
    header[154] = 0;
    header[155] = ' ';
    put(bytes_, header.data(), header.size());
}

void TarWriter::add(const std::string& name, const std::vector<std::uint8_t>& bytes,
                    std::uint32_t mode) {
    append_header(name, bytes.size(), mode, '0');
    put(bytes_, bytes.data(), bytes.size());
    const std::size_t remainder = bytes.size() % kBlock;
    if (remainder != 0) {
        const std::vector<std::uint8_t> padding(kBlock - remainder, 0);
        put(bytes_, padding.data(), padding.size());
    }
}

void TarWriter::add_text(const std::string& name, const std::string& text, std::uint32_t mode) {
    std::vector<std::uint8_t> bytes(text.begin(), text.end());
    add(name, bytes, mode);
}

void TarWriter::add_directory(const std::string& name, std::uint32_t mode) {
    std::string path = name;
    if (!path.empty() && path.back() != '/') {
        path.push_back('/');
    }
    append_header(path, 0, mode, '5');
}

std::vector<std::uint8_t> TarWriter::finish() const {
    std::vector<std::uint8_t> out = bytes_;
    const std::vector<std::uint8_t> end(2 * kBlock, 0);
    out.insert(out.end(), end.begin(), end.end());
    return out;
}

bool tar_list(const std::uint8_t* data, std::size_t size,
              std::vector<std::pair<std::string, std::size_t>>& out, std::string& error) {
    std::size_t at = 0;
    while (at + kBlock <= size) {
        const std::uint8_t* header = data + at;
        bool empty = true;
        for (std::size_t i = 0; i < kBlock; ++i) {
            empty = empty && header[i] == 0;
        }
        if (empty) {
            return true;
        }
        std::string name(reinterpret_cast<const char*>(header), 100);
        name = name.substr(0, name.find('\0'));
        std::string prefix(reinterpret_cast<const char*>(header + 345), 155);
        prefix = prefix.substr(0, prefix.find('\0'));
        if (!prefix.empty()) {
            name = prefix + "/" + name;
        }
        std::string size_text(reinterpret_cast<const char*>(header + 124), 12);
        size_text = size_text.substr(0, size_text.find_first_of("\0 "));
        std::size_t member_size = 0;
        for (char digit : size_text) {
            if (digit < '0' || digit > '7') {
                if (member_size == 0) {
                    break;
                }
                break;
            }
            member_size = member_size * 8 + static_cast<std::size_t>(digit - '0');
        }
        out.emplace_back(name, member_size);
        const std::size_t padded = (member_size + kBlock - 1) / kBlock * kBlock;
        if (at + kBlock + padded > size) {
            error = "tar: the last member is truncated";
            return false;
        }
        at += kBlock + padded;
    }
    error = "tar: no end-of-archive block";
    return false;
}

void ArWriter::add(const std::string& name, const std::vector<std::uint8_t>& bytes) {
    if (bytes_.empty()) {
        const char magic[8] = {'!', '<', 'a', 'r', 'c', 'h', '>', '\n'};
        put(bytes_, magic, 8);
    }
    // A name without spaces needs no long-name table; GNU ar pads and appends
    // the slash that means "no more name".
    std::string field = name + "/";
    field.resize(16, ' ');
    put(bytes_, field.data(), 16);
    const std::string stamp = "0           ";  // mtime: repeatable
    put(bytes_, stamp.data(), 12);
    const std::string owner = "0     ";        // uid
    put(bytes_, owner.data(), 6);
    put(bytes_, owner.data(), 6);               // gid
    const std::string mode = octal(0100644, 8, ' ');
    put(bytes_, mode.data(), 8);
    const std::string length = std::to_string(bytes.size());
    std::string size_field = length;
    size_field.resize(10, ' ');
    put(bytes_, size_field.data(), 10);
    const char end[2] = {'`', '\n'};
    put(bytes_, end, 2);
    put(bytes_, bytes.data(), bytes.size());
    if (bytes.size() % 2 != 0) {
        const char pad = '\n';
        put(bytes_, &pad, 1);
    }
}

std::vector<std::uint8_t> ArWriter::finish() const { return bytes_; }

bool ar_list(const std::uint8_t* data, std::size_t size,
             std::vector<std::pair<std::string, std::size_t>>& out, std::string& error) {
    if (size < 8 || std::memcmp(data, "!<arch>\n", 8) != 0) {
        error = "ar: the magic is missing";
        return false;
    }
    std::size_t at = 8;
    while (at + 60 <= size) {
        const std::uint8_t* header = data + at;
        std::string name(reinterpret_cast<const char*>(header), 16);
        name = name.substr(0, name.find_first_of(" \0"));
        if (!name.empty() && name.back() == '/') {
            name.pop_back();
        }
        std::string size_text(reinterpret_cast<const char*>(header + 48), 10);
        size_text = size_text.substr(0, size_text.find_first_of(" \0"));
        std::size_t member_size = 0;
        for (char digit : size_text) {
            if (digit < '0' || digit > '9') {
                break;
            }
            member_size = member_size * 10 + static_cast<std::size_t>(digit - '0');
        }
        out.emplace_back(name, member_size);
        at += 60 + member_size + (member_size % 2);
    }
    return true;
}

}  // namespace zui_tool
