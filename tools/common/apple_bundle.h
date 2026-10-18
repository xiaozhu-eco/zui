#pragma once

// A macOS .app bundle and an iOS .app, written from memory: the layout and the
// Info.plist are ours, because nothing on a non-Apple host provides them. A
// bundle is also what an .ipa wraps, so both Apple packagers build on this.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace zui_tool {

struct AppleBundleInfo {
    std::string name;             // CFBundleName, and the .app directory
    std::string identifier;       // CFBundleIdentifier
    std::string version;          // CFBundleVersion (and CFBundleShortVersionString)
    std::string executable;       // CFBundleExecutable: the file inside MacOS/
    std::string minimum_system;   // LSMinimumSystemVersion, macOS only
    std::string category;         // LSApplicationCategoryType, macOS only
    std::string icon_file;        // CFBundleIconFile, without the extension
    bool ios = false;             // iOS layout: a flat .app, no Contents/
    std::vector<std::string> orientations;  // UISupportedInterfaceOrientations
    std::string display_name;     // CFBundleDisplayName (defaults to name)
};

class AppleBundle {
public:
    /// Writes into @p destination, creating `<destination>/<name>.app`.
    AppleBundle(std::filesystem::path destination, AppleBundleInfo info);

    void add_executable(const std::vector<std::uint8_t>& bytes);
    /// A resource at its path inside the bundle (relative, forward slashes).
    void add_resource(const std::string& relative, const std::vector<std::uint8_t>& bytes);
    void add_resource_file(const std::string& relative, const std::filesystem::path& source);
    /// Info.plist keys the packager adds on top of the standard set.
    void set_extra(const std::string& key, const std::string& value);
    void set_extra_flag(const std::string& key, bool value);

    /// The bundle's root, and where its files went, for the packaging step that
    /// wraps it (a dmg, an ipa).
    const std::filesystem::path& app_root() const { return app_root_; }

    bool write(std::string& error);
    const std::string& plist() const { return plist_; }

private:
    void build_plist();
    std::filesystem::path app_root_;
    AppleBundleInfo info_;
    std::vector<std::uint8_t> executable_;
    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> resources_;
    std::vector<std::pair<std::string, std::string>> extras_;
    std::vector<std::pair<std::string, bool>> extra_flags_;
    std::string plist_;
};

/// True when the file starts like a Mach-O image (thin or universal).
bool is_macho(const std::filesystem::path& file);

}  // namespace zui_tool
