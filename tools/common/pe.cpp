#include "pe.h"

#include "zip.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <set>

namespace zui_tool {
namespace {

std::uint16_t read_u16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::uint32_t read_u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::string lower_ascii(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

struct Section {
    std::uint32_t virtual_address = 0;
    std::uint32_t virtual_size = 0;
    std::uint32_t raw_offset = 0;
    std::uint32_t raw_size = 0;
};

// Maps an RVA to a file offset. `size` is the number of bytes the caller reads
// there; the mapping fails when those bytes cross the end of the file.
bool rva_to_offset(const std::vector<Section>& sections, std::uint32_t rva, std::size_t size,
                   std::size_t file_size, std::size_t& offset) {
    for (const Section& section : sections) {
        const std::uint32_t span = std::max(section.virtual_size, section.raw_size);
        if (rva < section.virtual_address || rva - section.virtual_address >= span) {
            continue;
        }
        const std::uint64_t found =
            static_cast<std::uint64_t>(section.raw_offset) + (rva - section.virtual_address);
        if (found + size > file_size) {
            return false;
        }
        offset = static_cast<std::size_t>(found);
        return true;
    }
    return false;
}

bool name_starts_with(const std::string& name, const char* prefix) {
    const std::string lowered = lower_ascii(name);
    const std::size_t length = std::strlen(prefix);
    return lowered.size() >= length && lowered.compare(0, length, prefix) == 0;
}

}  // namespace

bool read_pe_imports(const std::filesystem::path& file, std::vector<std::string>& names,
                     std::string& error) {
    std::vector<std::uint8_t> bytes;
    if (!read_file_bytes(file, bytes, error)) {
        return false;
    }
    names.clear();
    if (bytes.size() < 0x40 || bytes[0] != 'M' || bytes[1] != 'Z') {
        error = file.string() + " is not a PE image";
        return false;
    }
    const std::uint64_t pe = read_u32(bytes.data() + 0x3c);
    if (pe + 24 > bytes.size() || std::memcmp(bytes.data() + pe, "PE\0\0", 4) != 0) {
        error = file.string() + " has a broken PE header";
        return false;
    }
    const std::uint16_t section_count = read_u16(bytes.data() + pe + 6);
    const std::uint16_t optional_size = read_u16(bytes.data() + pe + 20);
    const std::uint64_t optional = pe + 24;
    if (optional_size < 96 || optional + optional_size > bytes.size()) {
        error = file.string() + " has a broken optional header";
        return false;
    }
    const std::uint16_t magic = read_u16(bytes.data() + optional);
    std::uint64_t directories = 0;
    std::uint64_t directory_count_offset = 0;
    if (magic == 0x10b) {
        directories = optional + 96;
        directory_count_offset = optional + 92;
    } else if (magic == 0x20b) {
        directories = optional + 112;
        directory_count_offset = optional + 108;
    } else {
        error = file.string() + " is not a PE32 or PE32+ image";
        return false;
    }
    const std::uint32_t directory_count = read_u32(bytes.data() + directory_count_offset);
    if (directory_count < 2 || directories + 16 > optional + optional_size) {
        return true;  // No import directory, so nothing to report.
    }
    const std::uint32_t import_rva = read_u32(bytes.data() + directories + 8);
    const std::uint32_t import_size = read_u32(bytes.data() + directories + 12);
    if (import_rva == 0 || import_size == 0) {
        return true;
    }
    std::vector<Section> sections;
    for (std::uint16_t i = 0; i < section_count; ++i) {
        const std::uint64_t entry = optional + optional_size + static_cast<std::uint64_t>(i) * 40;
        if (entry + 40 > bytes.size()) {
            error = file.string() + " has a broken section table";
            return false;
        }
        const std::uint8_t* header = bytes.data() + entry;
        Section section;
        section.virtual_size = read_u32(header + 8);
        section.virtual_address = read_u32(header + 12);
        section.raw_size = read_u32(header + 16);
        section.raw_offset = read_u32(header + 20);
        sections.push_back(section);
    }
    std::size_t cursor = 0;
    if (!rva_to_offset(sections, import_rva, 20, bytes.size(), cursor)) {
        error = file.string() + " has a broken import table";
        return false;
    }
    for (;;) {
        if (cursor + 20 > bytes.size()) {
            error = file.string() + " has a broken import table";
            return false;
        }
        const std::uint8_t* descriptor = bytes.data() + cursor;
        const std::uint32_t name_rva = read_u32(descriptor + 12);
        if (name_rva == 0) {
            break;
        }
        std::size_t name_offset = 0;
        if (rva_to_offset(sections, name_rva, 1, bytes.size(), name_offset)) {
            std::string name;
            while (name_offset < bytes.size() && bytes[name_offset] != 0 && name.size() < 260) {
                name.push_back(static_cast<char>(bytes[name_offset++]));
            }
            if (!name.empty()) {
                names.push_back(name);
            }
        }
        cursor += 20;
    }
    return true;
}

bool collect_local_dlls(const std::filesystem::path& exe,
                        const std::vector<std::filesystem::path>& dirs,
                        std::vector<std::filesystem::path>& out, std::vector<std::string>& external,
                        std::string& error) {
    std::vector<std::string> pending;
    if (!read_pe_imports(exe, pending, error)) {
        return false;
    }
    out.clear();
    external.clear();
    std::set<std::string> seen;
    while (!pending.empty()) {
        const std::string name = pending.back();
        pending.pop_back();
        if (!seen.insert(lower_ascii(name)).second) {
            continue;
        }
        std::filesystem::path found;
        for (const std::filesystem::path& dir : dirs) {
            if (dir.empty()) {
                continue;
            }
            std::error_code ec;
            const std::filesystem::path candidate = dir / name;
            if (std::filesystem::is_regular_file(candidate, ec)) {
                found = candidate;
                break;
            }
        }
        if (found.empty()) {
            if (dll_name_is_zui(name)) {
                error = exe.filename().string() + " needs " + name +
                        ", which is not in the build or next to the executable";
                return false;
            }
            external.push_back(name);
            continue;
        }
        out.push_back(found);
        std::vector<std::string> nested;
        if (!read_pe_imports(found, nested, error)) {
            return false;
        }
        for (std::string& item : nested) {
            pending.push_back(std::move(item));
        }
    }
    std::sort(out.begin(), out.end(), [](const std::filesystem::path& left,
                                         const std::filesystem::path& right) {
        return left.filename().string() < right.filename().string();
    });
    return true;
}

bool dll_name_is_zui(const std::string& name) {
    const std::string lowered = lower_ascii(name);
    return lowered == "zui.dll" || lowered.rfind("zui_", 0) == 0 || lowered.rfind("zui-", 0) == 0;
}

bool dll_name_is_redistributable(const std::string& name) {
    const std::string lowered = lower_ascii(name);
    if (lowered == "msvcrt.dll") {
        return false;
    }
    const char* prefixes[] = {"msvcp",  "vcruntime",      "msvcr",  "ucrtbase",
                              "ucrtbased", "concrt",       "vccorlib", "api-ms-win-crt"};
    for (const char* prefix : prefixes) {
        if (name_starts_with(lowered, prefix)) {
            return true;
        }
    }
    return false;
}

}  // namespace zui_tool
