#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace zui_tool {

/// True when the file starts like an ELF image.
bool is_elf(const std::filesystem::path& file);

/// The ELF e_machine value of a file, or 0 when it is not ELF64.
std::uint16_t elf_machine(const std::filesystem::path& file);

/// The dpkg architecture name for an e_machine value, e.g. x86_64 / arm64.
/// Empty when the machine is unknown to us.
std::string dpkg_architecture(std::uint16_t machine);

/// The dpkg architecture of a built program, empty when the file is not ELF.
std::string dpkg_architecture_of(const std::filesystem::path& file);

}  // namespace zui_tool
