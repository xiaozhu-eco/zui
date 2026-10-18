#pragma once

#include "project.h"

#include <string>

namespace zui_tool {

/// The file bodies a new project is made of. The entry includes only the public
/// app header; the page opens next to the program; the CMakeLists links the
/// engine as an imported target so its runtime libraries stage themselves.
class Templates {
public:
    /// `src/main.cpp`: load, one bind, one data write, run.
    static std::string entry(const Project& project);
    /// The page, with its identity meta tags and the link to the stylesheet.
    static std::string page(const Project& project);
    /// The stylesheet: standard CSS, no framework.
    static std::string style(const Project& project);
    /// Where that stylesheet lives: the page links "app.css", so this is the
    /// path that has to exist and be declared.
    static std::string app_css_rel(const Project& project);
    /// The launcher icon the scaffold starts with.
    static std::string icon(const Project& project);
    /// The project's CMakeLists: desktop executable, or the android library.
    static std::string cmake(const Project& project);
    /// How to build, run, and pack, plus the two rules the packer enforces
    /// (declared files, declared permissions).
    static std::string readme(const Project& project);
    /// What a generated project ignores: the build tree and the packages.
    static std::string gitignore();

    /// Startup content for a declared runtime file. A file the engine cannot
    /// read empty starts with the smallest document of its kind; every other
    /// kind starts empty for the author to fill in.
    static std::string placeholder(const std::string& rel);

    /// A declared runtime path whose last segment carries no extension names a
    /// directory, not a file.
    static bool is_directory_entry(const std::string& rel);
};

}  // namespace zui_tool
