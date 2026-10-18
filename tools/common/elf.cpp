#include "elf.h"

#include "zip.h"

#include <cstdint>

namespace zui_tool {
namespace {

std::uint16_t read_u16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

}  // namespace

bool is_elf(const std::filesystem::path& file) {
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!read_file_bytes(file, bytes, error) || bytes.size() < 20) {
        return false;
    }
    return bytes[0] == 0x7f && bytes[1] == 'E' && bytes[2] == 'L' && bytes[3] == 'F';
}

std::uint16_t elf_machine(const std::filesystem::path& file) {
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!read_file_bytes(file, bytes, error) || bytes.size() < 20) {
        return 0;
    }
    if (bytes[0] != 0x7f || bytes[1] != 'E' || bytes[2] != 'L' || bytes[3] != 'F') {
        return 0;
    }
    if (bytes[4] != 2) {  // ELFCLASS64
        return 0;
    }
    // e_machine is endian-dependent; the byte order is what e_ident[5] says.
    if (bytes[5] == 2) {
        return static_cast<std::uint16_t>((bytes[18] << 8) | bytes[19]);
    }
    return read_u16(bytes.data() + 18);
}

std::string dpkg_architecture(std::uint16_t machine) {
    switch (machine) {
    case 0x3e:
        return "amd64";
    case 0xb7:
        return "arm64";
    case 0x28:
        return "armhf";
    case 0xf3:
        return "riscv64";
    default:
        return {};
    }
}

std::string dpkg_architecture_of(const std::filesystem::path& file) {
    return dpkg_architecture(elf_machine(file));
}

}  // namespace zui_tool
