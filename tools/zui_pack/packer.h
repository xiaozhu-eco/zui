#pragma once

// One platform, one packager. The CLI resolves --platform to one of these and
// runs it; nothing else in the tool knows which platform is being built.

#include "project.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace zui_tool {

/// Everything a packager needs: the project, the zui source tree, a build tree
/// that holds the engine, and where the packages go.
struct PackContext {
    std::filesystem::path project;
    std::filesystem::path root;
    std::filesystem::path build;
    std::filesystem::path out;  // empty: <project>/dist
    Project decl;

    std::filesystem::path dist() const {
        return out.empty() ? project / "dist" : out;
    }

    /// `<name>-<version>`, the stem every package file name starts with.
    std::string stem() const { return decl.name + "-" + decl.version; }
};

class Packer {
public:
    virtual ~Packer() = default;
    virtual const char* platform() const = 0;

    /// A sentence naming what this host lacks, or empty when it can package.
    /// A packager that cannot run says why instead of writing a package that
    /// only looks like one.
    virtual std::string missing_host_tool(const PackContext& ctx) const {
        (void)ctx;
        return {};
    }

    /// Writes the packages zui.json selects. The return value is a process exit
    /// code.
    virtual int pack(const PackContext& ctx) = 0;
};

std::unique_ptr<Packer> make_windows_packer();
std::unique_ptr<Packer> make_linux_packer();
std::unique_ptr<Packer> make_macos_packer();
std::unique_ptr<Packer> make_ios_packer();
std::unique_ptr<Packer> make_android_packer();

/// The desktop application binary: what `zui.json` names in `target`, or this
/// project's own CMake build. Windows, Linux, and macOS all get theirs this way,
/// so the rule for finding a built program lives in one place.
bool build_desktop_app(const PackContext& ctx, std::filesystem::path& exe, std::string& error);

/// Every runtime library the executable imports, transitively, from the
/// directories the build keeps them in. Empty on a platform whose loader does
/// not work that way.
bool collect_runtime_libraries(const std::filesystem::path& exe,
                               const std::filesystem::path& build,
                               std::vector<std::filesystem::path>& libraries,
                               std::vector<std::string>& external, std::string& error);

/// The page, the icon, and the declared runtime files, as (relative name, path).
bool collect_payload(const PackContext& ctx,
                     std::vector<std::pair<std::string, std::filesystem::path>>& files,
                     std::string& error);

}  // namespace zui_tool
