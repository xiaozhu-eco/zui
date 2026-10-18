#include "apple_bundle.h"

#include "zip.h"

#include <fstream>

namespace zui_tool {
namespace {

std::string xml_text(const std::string& text) {
    std::string out;
    for (char c : text) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        default:
            out.push_back(c);
            break;
        }
    }
    return out;
}

}  // namespace

bool is_macho(const std::filesystem::path& file) {
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!read_file_bytes(file, bytes, error) || bytes.size() < 4) {
        return false;
    }
    const std::uint32_t magic = static_cast<std::uint32_t>(bytes[0]) << 24 |
                                static_cast<std::uint32_t>(bytes[1]) << 16 |
                                static_cast<std::uint32_t>(bytes[2]) << 8 |
                                static_cast<std::uint32_t>(bytes[3]);
    // 64-bit thin (both byte orders) and a universal binary.
    return magic == 0xfeedfacfu || magic == 0xcffaedfeu || magic == 0xcafebabeu ||
           magic == 0xbebafecau;
}

AppleBundle::AppleBundle(std::filesystem::path destination, AppleBundleInfo info)
    : info_(std::move(info)) {
    app_root_ = std::move(destination) / (info_.name + ".app");
}

void AppleBundle::add_executable(const std::vector<std::uint8_t>& bytes) {
    executable_ = bytes;
}

void AppleBundle::add_resource(const std::string& relative,
                               const std::vector<std::uint8_t>& bytes) {
    resources_.emplace_back(relative, bytes);
}

void AppleBundle::add_resource_file(const std::string& relative,
                                    const std::filesystem::path& source) {
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (read_file_bytes(source, bytes, error)) {
        resources_.emplace_back(relative, std::move(bytes));
    }
}

void AppleBundle::set_extra(const std::string& key, const std::string& value) {
    extras_.emplace_back(key, value);
}

void AppleBundle::set_extra_flag(const std::string& key, bool value) {
    extra_flags_.emplace_back(key, value);
}

void AppleBundle::build_plist() {
    const std::string display = info_.display_name.empty() ? info_.name : info_.display_name;
    plist_ = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
             "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
             "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
             "<plist version=\"1.0\">\n"
             "<dict>\n";
    auto string_key = [this](const char* key, const std::string& value) {
        if (!value.empty()) {
            plist_ += "\t<key>" + std::string(key) + "</key>\n\t<string>" + xml_text(value) +
                      "</string>\n";
        }
    };
    string_key("CFBundleName", info_.name);
    string_key("CFBundleDisplayName", display);
    string_key("CFBundleIdentifier", info_.identifier);
    string_key("CFBundleExecutable", info_.executable);
    string_key("CFBundleVersion", info_.version);
    string_key("CFBundleShortVersionString", info_.version);
    string_key("CFBundleIconFile", info_.icon_file);
    plist_ += "\t<key>CFBundlePackageType</key>\n\t<string>APPL</string>\n";
    plist_ += "\t<key>CFBundleInfoDictionaryVersion</key>\n\t<string>6.0</string>\n";
    if (info_.ios) {
        plist_ += "\t<key>LSRequiresIPhoneOS</key>\n\t<true/>\n";
        plist_ += "\t<key>UIDeviceFamily</key>\n\t<array>\n\t\t<integer>1</integer>\n"
                  "\t\t<integer>2</integer>\n\t</array>\n";
        plist_ += "\t<key>UILaunchScreen</key>\n\t<dict/>\n";
        plist_ += "\t<key>UIRequiredDeviceCapabilities</key>\n\t<array>\n\t\t<string>arm64</string>\n"
                  "\t</array>\n";
        if (!info_.orientations.empty()) {
            plist_ += "\t<key>UISupportedInterfaceOrientations</key>\n\t<array>\n";
            for (const std::string& orientation : info_.orientations) {
                plist_ += "\t\t<string>" + xml_text(orientation) + "</string>\n";
            }
            plist_ += "\t</array>\n";
        }
    } else {
        string_key("LSMinimumSystemVersion", info_.minimum_system);
        string_key("LSApplicationCategoryType", info_.category);
        plist_ += "\t<key>NSHighResolutionCapable</key>\n\t<true/>\n";
    }
    for (const auto& extra : extras_) {
        string_key(extra.first.c_str(), extra.second);
    }
    for (const auto& flag : extra_flags_) {
        plist_ += "\t<key>" + flag.first + "</key>\n\t" + (flag.second ? "<true/>" : "<false/>") +
                  "\n";
    }
    plist_ += "</dict>\n</plist>\n";
}

bool AppleBundle::write(std::string& error) {
    build_plist();
    if (executable_.empty()) {
        error = "the bundle has no executable";
        return false;
    }
    // A plist that names an icon the bundle does not carry is a broken app: the
    // error is here rather than on a device that shows a blank tile.
    // The icon may be named in the standard field or added as an extra key, so
    // both are checked against what the bundle actually carries.
    std::string icon = info_.icon_file;
    for (const auto& extra : extras_) {
        if (extra.first == "CFBundleIconFile") {
            icon = extra.second;
        }
    }
    if (!icon.empty()) {
        bool carried = false;
        for (const auto& resource : resources_) {
            const std::size_t slash = resource.first.find_last_of('/');
            const std::string leaf =
                slash == std::string::npos ? resource.first : resource.first.substr(slash + 1);
            carried = carried || leaf == icon;
        }
        if (!carried) {
            error = "CFBundleIconFile names " + icon + ", which the bundle does not carry";
            return false;
        }
    }
    std::error_code ec;
    const std::filesystem::path binary =
        info_.ios ? app_root_ / info_.executable
                  : app_root_ / "Contents" / "MacOS" / info_.executable;
    const std::filesystem::path resources =
        info_.ios ? app_root_ : app_root_ / "Contents" / "Resources";
    const std::filesystem::path plist_path =
        info_.ios ? app_root_ / "Info.plist" : app_root_ / "Contents" / "Info.plist";
    std::filesystem::create_directories(binary.parent_path(), ec);
    std::filesystem::create_directories(resources, ec);
    if (ec) {
        error = "cannot create " + app_root_.string();
        return false;
    }
    {
        std::ofstream out(binary, std::ios::binary);
        out.write(reinterpret_cast<const char*>(executable_.data()),
                  static_cast<std::streamsize>(executable_.size()));
        if (!out) {
            error = "cannot write " + binary.string();
            return false;
        }
    }
    std::filesystem::permissions(binary,
                                 std::filesystem::perms::owner_exec |
                                     std::filesystem::perms::group_exec |
                                     std::filesystem::perms::others_exec,
                                 std::filesystem::perm_options::add, ec);
    for (const auto& resource : resources_) {
        const std::filesystem::path path =
            resources / std::filesystem::path(resource.first, std::filesystem::path::generic_format);
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(resource.second.data()),
                  static_cast<std::streamsize>(resource.second.size()));
        if (!out) {
            error = "cannot write " + path.string();
            return false;
        }
    }
    {
        std::ofstream out(plist_path, std::ios::binary);
        out << plist_;
        if (!out) {
            error = "cannot write " + plist_path.string();
            return false;
        }
    }
    return true;
}

}  // namespace zui_tool
