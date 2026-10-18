#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace zui_tool {

// The executable's version resource: what a person reads under Properties and
// what a store shows for a file. Empty fields are left out of the resource.
struct ExeMetadata {
    std::string product;
    std::string description;
    std::string company;
    std::string copyright;
    std::string version;
    std::string original_filename;
};

// Writes a Windows icon resource into an executable image. The picture comes
// from the project's icon.png or icon.svg, then the zui bamboo mark, then a
// plain tile. `bytes` is replaced with the updated PE.
bool stamp_exe_bytes(std::vector<std::uint8_t>& bytes, const std::filesystem::path& project,
                     const std::filesystem::path& root, const std::string& icon_rel,
                     const ExeMetadata& metadata, std::string& error);

// Writes mipmap-*/ic_launcher.png under res_dir for aapt2. Same picture as
// stamp_exe_bytes. icon_rel is the path recorded in the project.
bool write_launcher_icons(const std::filesystem::path& res_dir, const std::filesystem::path& project,
                          const std::filesystem::path& root, const std::string& icon_rel,
                          std::string& error);

}  // namespace zui_tool
