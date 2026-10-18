#include "zip.h"

#include <fstream>

#include <zlib.h>

namespace zui_tool {
namespace {

void append_u16(std::vector<std::uint8_t>& out, unsigned value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
}

void append_u32(std::vector<std::uint8_t>& out, unsigned value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xff));
}

unsigned read_u16(const std::uint8_t* data) {
    return static_cast<unsigned>(data[0]) | (static_cast<unsigned>(data[1]) << 8);
}

unsigned read_u32(const std::uint8_t* data) {
    return static_cast<unsigned>(data[0]) | (static_cast<unsigned>(data[1]) << 8) |
           (static_cast<unsigned>(data[2]) << 16) | (static_cast<unsigned>(data[3]) << 24);
}

std::uint32_t crc32_bytes(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xedb88320u & mask);
        }
    }
    return ~crc;
}

bool zip_name_ok(const std::string& name) {
    if (name.empty() || name.size() > 60000 || name.front() == '/' || name.back() == '/') {
        return false;
    }
    if (name.find(':') != std::string::npos || name.find('\\') != std::string::npos) {
        return false;
    }
    std::string segment;
    for (std::size_t i = 0; i <= name.size(); ++i) {
        if (i == name.size() || name[i] == '/') {
            if (segment.empty() || segment == "." || segment == "..") {
                return false;
            }
            segment.clear();
        } else {
            segment.push_back(name[i]);
        }
    }
    return true;
}

bool find_eocd(const std::uint8_t* data, std::size_t size, std::size_t& offset) {
    if (size < 22) {
        return false;
    }
    const std::size_t last = size - 22;
    const std::size_t earliest = size > 65557 ? size - 65557 : 0;
    for (std::size_t i = last + 1; i-- > earliest;) {
        if (data[i] == 0x50 && data[i + 1] == 0x4b && data[i + 2] == 0x05 && data[i + 3] == 0x06) {
            offset = i;
            return true;
        }
        if (i == 0) {
            break;
        }
    }
    return false;
}

// ZIP method 8 is a raw deflate stream: no zlib and no gzip wrapper, which is
// exactly what windowBits = -15 asks zlib for.
bool deflate_bytes(const std::vector<std::uint8_t>& in, std::vector<std::uint8_t>& out,
                   std::string& error) {
    z_stream stream{};
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK) {
        error = "zlib: cannot start deflate";
        return false;
    }
    out.assign(in.size() + in.size() / 8 + 64, 0);
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(in.data()));
    stream.avail_in = static_cast<uInt>(in.size());
    stream.next_out = reinterpret_cast<Bytef*>(out.data());
    stream.avail_out = static_cast<uInt>(out.size());
    const int code = deflate(&stream, Z_FINISH);
    if (code != Z_STREAM_END) {
        deflateEnd(&stream);
        error = "zlib: deflate failed";
        return false;
    }
    out.resize(stream.total_out);
    deflateEnd(&stream);
    return true;
}

bool inflate_bytes(const std::uint8_t* data, std::size_t size, std::size_t expected,
                   std::vector<std::uint8_t>& out, std::string& error) {
    z_stream stream{};
    if (inflateInit2(&stream, -15) != Z_OK) {
        error = "zlib: cannot start inflate";
        return false;
    }
    out.assign(expected, 0);
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data));
    stream.avail_in = static_cast<uInt>(size);
    stream.next_out = reinterpret_cast<Bytef*>(out.data());
    stream.avail_out = static_cast<uInt>(out.size());
    const int code = inflate(&stream, Z_FINISH);
    const std::size_t written = stream.total_out;
    inflateEnd(&stream);
    if (code != Z_STREAM_END || written != expected) {
        error = "zlib: the entry does not inflate to its recorded size";
        return false;
    }
    return true;
}

void slash_name(std::uint8_t* name, unsigned length) {
    for (unsigned i = 0; i < length; ++i) {
        if (name[i] == '\\') {
            name[i] = '/';
        }
    }
}

}  // namespace

bool read_file_bytes(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes,
                     std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot read " + path.string();
        return false;
    }
    in.seekg(0, std::ios::end);
    const auto length = in.tellg();
    if (length < 0) {
        error = "cannot read " + path.string();
        return false;
    }
    in.seekg(0, std::ios::beg);
    bytes.resize(static_cast<std::size_t>(length));
    if (length > 0) {
        in.read(reinterpret_cast<char*>(bytes.data()), length);
    }
    if (!in) {
        error = "cannot read " + path.string();
        return false;
    }
    return true;
}

bool write_zip(const std::filesystem::path& path, const std::vector<ZipEntry>& entries,
               std::string& error) {
    if (entries.size() > 0xffffu) {
        error = "a zip holds at most 65535 entries";
        return false;
    }
    for (const ZipEntry& entry : entries) {
        if (!zip_name_ok(entry.name)) {
            error = "zip entry " + entry.name + " is not a relative path";
            return false;
        }
        if (entry.bytes.size() > 0xffffffffu || entry.name.size() > 0xffffu) {
            error = "zip entry " + entry.name + " is too large for a zip";
            return false;
        }
    }
    std::vector<std::uint8_t> local;
    std::vector<std::uint8_t> central;
    std::vector<std::uint32_t> offsets;
    offsets.reserve(entries.size());
    for (const ZipEntry& entry : entries) {
        offsets.push_back(static_cast<std::uint32_t>(local.size()));
        const std::uint32_t crc = crc32_bytes(entry.bytes.data(), entry.bytes.size());
        const auto raw_size = static_cast<unsigned>(entry.bytes.size());
        std::vector<std::uint8_t> packed;
        std::string deflate_error;
        unsigned method = 0;
        unsigned stored_size = raw_size;
        if (!entry.store && deflate_bytes(entry.bytes, packed, deflate_error) &&
            packed.size() < raw_size) {
            // A method the archive does not need is a method it does not use.
            method = 8;
            stored_size = static_cast<unsigned>(packed.size());
        }
        append_u32(local, 0x04034b50u);
        append_u16(local, 20);
        append_u16(local, 0);
        append_u16(local, method);
        append_u16(local, 0);               // mod time: the epoch of a fixed date
        append_u16(local, 0x0021u);         // mod date: 1980-01-01, so a build repeats
        append_u32(local, crc);
        append_u32(local, stored_size);
        append_u32(local, raw_size);
        append_u16(local, static_cast<unsigned>(entry.name.size()));
        append_u16(local, 0);
        local.insert(local.end(), entry.name.begin(), entry.name.end());
        if (method == 8) {
            local.insert(local.end(), packed.begin(), packed.end());
        } else {
            local.insert(local.end(), entry.bytes.begin(), entry.bytes.end());
        }

        append_u32(central, 0x02014b50u);
        append_u16(central, 20);
        append_u16(central, 20);
        append_u16(central, 0);
        append_u16(central, method);
        append_u16(central, 0);
        append_u16(central, 0x0021u);
        append_u32(central, crc);
        append_u32(central, stored_size);
        append_u32(central, raw_size);
        append_u16(central, static_cast<unsigned>(entry.name.size()));
        append_u16(central, 0);
        append_u16(central, 0);
        append_u16(central, 0);
        append_u16(central, 0);
        append_u32(central, 0);
        append_u32(central, offsets.back());
        central.insert(central.end(), entry.name.begin(), entry.name.end());
    }
    const auto central_offset = static_cast<unsigned>(local.size());
    const auto central_size = static_cast<unsigned>(central.size());
    std::vector<std::uint8_t> eocd;
    append_u32(eocd, 0x06054b50u);
    append_u16(eocd, 0);
    append_u16(eocd, 0);
    append_u16(eocd, static_cast<unsigned>(entries.size()));
    append_u16(eocd, static_cast<unsigned>(entries.size()));
    append_u32(eocd, central_size);
    append_u32(eocd, central_offset);
    append_u16(eocd, 0);

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(local.data()), static_cast<std::streamsize>(local.size()));
    out.write(reinterpret_cast<const char*>(central.data()), static_cast<std::streamsize>(central.size()));
    out.write(reinterpret_cast<const char*>(eocd.data()), static_cast<std::streamsize>(eocd.size()));
    if (!out) {
        error = "cannot write " + path.string();
        return false;
    }
    return true;
}

bool read_zip(const std::uint8_t* data, std::size_t size, std::vector<ZipEntry>& entries,
              std::string& error) {
    std::size_t eocd = 0;
    if (data == nullptr || !find_eocd(data, size, eocd)) {
        error = "zip directory is missing";
        return false;
    }
    const unsigned count = read_u16(data + eocd + 10);
    const unsigned central_size = read_u32(data + eocd + 12);
    const unsigned central_off = read_u32(data + eocd + 16);
    if (static_cast<std::size_t>(central_off) + central_size > size) {
        error = "zip directory is truncated";
        return false;
    }
    std::size_t cursor = central_off;
    entries.clear();
    entries.reserve(count);
    for (unsigned n = 0; n < count; ++n) {
        if (cursor + 46 > size || read_u32(data + cursor) != 0x02014b50u) {
            error = "zip directory is truncated";
            return false;
        }
        const unsigned method = read_u16(data + cursor + 10);
        const unsigned comp_size = read_u32(data + cursor + 20);
        const unsigned raw_size = read_u32(data + cursor + 24);
        const unsigned name_len = read_u16(data + cursor + 28);
        const unsigned extra_len = read_u16(data + cursor + 30);
        const unsigned comment_len = read_u16(data + cursor + 32);
        const unsigned local_off = read_u32(data + cursor + 42);
        if (method != 0 && method != 8) {
            error = "zip entry uses a compression method this tool does not read";
            return false;
        }
        if (cursor + 46 + name_len + extra_len + comment_len > size) {
            error = "zip directory is truncated";
            return false;
        }
        ZipEntry entry;
        entry.name.assign(reinterpret_cast<const char*>(data + cursor + 46), name_len);
        if (local_off + 30 > size) {
            error = "zip entry is truncated";
            return false;
        }
        const unsigned local_name = read_u16(data + local_off + 26);
        const unsigned local_extra = read_u16(data + local_off + 28);
        const std::size_t data_off = static_cast<std::size_t>(local_off) + 30 + local_name + local_extra;
        if (data_off + comp_size > size) {
            error = "zip entry is truncated";
            return false;
        }
        if (method == 0) {
            if (comp_size != raw_size) {
                error = "zip entry sizes disagree";
                return false;
            }
            entry.bytes.assign(data + data_off, data + data_off + comp_size);
        } else if (!inflate_bytes(data + data_off, comp_size, raw_size, entry.bytes, error)) {
            return false;
        }
        entries.push_back(std::move(entry));
        cursor += 46 + name_len + extra_len + comment_len;
    }
    return true;
}

bool normalize_zip_separators(const std::filesystem::path& path, std::string& error) {
    std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!file) {
        error = "cannot normalize " + path.string();
        return false;
    }
    file.seekg(0, std::ios::end);
    const auto length = file.tellg();
    if (length < 0) {
        error = "cannot normalize " + path.string();
        return false;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    file.seekg(0, std::ios::beg);
    if (length > 0) {
        file.read(reinterpret_cast<char*>(bytes.data()), length);
    }
    if (!file) {
        error = "cannot normalize " + path.string();
        return false;
    }
    std::size_t i = 0;
    while (i + 4 < bytes.size()) {
        const unsigned sig = read_u32(bytes.data() + i);
        if (sig == 0x04034b50u && i + 30 <= bytes.size()) {
            const unsigned name_len = read_u16(bytes.data() + i + 26);
            const unsigned extra_len = read_u16(bytes.data() + i + 28);
            const unsigned comp_size = read_u32(bytes.data() + i + 18);
            if (i + 30 + name_len > bytes.size()) {
                break;
            }
            slash_name(bytes.data() + i + 30, name_len);
            const std::size_t next = i + 30 + name_len + extra_len + comp_size;
            i = next > i ? next : i + 1;
        } else if (sig == 0x02014b50u && i + 46 <= bytes.size()) {
            const unsigned name_len = read_u16(bytes.data() + i + 28);
            const unsigned extra_len = read_u16(bytes.data() + i + 30);
            const unsigned comment_len = read_u16(bytes.data() + i + 32);
            if (i + 46 + name_len > bytes.size()) {
                break;
            }
            slash_name(bytes.data() + i + 46, name_len);
            const std::size_t next = i + 46 + name_len + extra_len + comment_len;
            i = next > i ? next : i + 1;
        } else {
            ++i;
        }
    }
    file.clear();
    file.seekp(0, std::ios::beg);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        error = "cannot normalize " + path.string();
        return false;
    }
    return true;
}

}  // namespace zui_tool
