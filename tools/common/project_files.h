#pragma once

// The template reader and project writer. The project object stays free of I/O.

#include "project.h"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace zui_tool {

/// The file side of a project: reading the declaration, writing a new tree, and
/// checking a tree before it is packed. No prompt and no terminal lives here.
class ProjectWriter {
public:
    /// Reads and validates zui.json.
    static bool read(const std::filesystem::path& file, Project& project, std::string& error);

    /// Writes zui.json, the entry, the page, the stylesheet, the icon,
    /// CMakeLists.txt, README, .gitignore, and the declared runtime files.
    static bool write(const std::filesystem::path& dir, const Project& project, std::string& error);

    /// The page, the icon, the entry, the declared runtime files, and the
    /// permission list must exist and stay inside the project. The message names
    /// the input that failed.
    static bool require_inputs(const std::filesystem::path& dir, const Project& project,
                               std::string& error);

    /// The files named in the project, plus the icon. The page is packed
    /// separately.
    static bool collect_runtime_files(
        const std::filesystem::path& dir, const Project& project,
        std::vector<std::pair<std::string, std::filesystem::path>>& out, std::string& error);

    /// Relative files a page references that the declared project would not
    /// carry: the packer ships what zui.json names and does not read markup, so
    /// a reference the author forgot to declare shows up here instead of as a
    /// missing stylesheet or image at run time.
    static std::vector<std::string> unshipped_references(const std::filesystem::path& dir,
                                                         const Project& project);
};

}  // namespace zui_tool
