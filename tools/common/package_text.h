#pragma once

// The text a Linux or Android package is built from. The packagers write these
// strings into the archive, the desktop entry, and the manifest. A Windows
// test calls the same functions, so it does not keep a second copy.

#include "project.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace zui_tool {

std::string linux_package_name(const std::string& name);

// Name is the label a person sees. Exec is the program the session starts.
// Icon is the bitmap that program ships, empty when the package has none.
struct LinuxDesktop {
    std::string name;
    std::string comment;
    std::string exec;
    std::string icon;
    std::string identifier;
};

LinuxDesktop linux_portable_desktop(const Project& decl, bool with_icon);
LinuxDesktop linux_deb_desktop(const Project& decl, bool with_icon);
std::string linux_desktop_entry(const LinuxDesktop& desktop);
std::string linux_deb_control(const std::string& package, const std::string& version,
                              const std::string& arch, const std::string& maintainer,
                              const std::string& description, const std::string& identifier,
                              std::uintmax_t installed_bytes);
std::string linux_portable_page(const std::string& name, const std::string& page);
std::string linux_deb_page(const std::string& package, const std::string& page);

bool android_version_code(const std::string& version, int& code, std::string& error);
std::string android_manifest(const Project& decl, int version_code, bool with_icon);
std::string android_asset_entry(const std::string& relative);

// Info.plist usage-description keys for the author's list, in list order. A key
// shared by two names keeps the first reason. Names with no iOS key are absent.
std::vector<std::pair<std::string, std::string>> apple_usage_entries(const Project& decl);
// Space-separated catalog names the author kept, including ones iOS has no usage key for.
std::string apple_declared_permissions(const Project& decl);

}  // namespace zui_tool
