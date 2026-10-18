#include "package_text.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace zui_tool {
namespace {

std::string lowercase(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

std::string xml_text(const std::string& text) {
    std::string out;
    for (char c : text) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '"':
            out += "&quot;";
            break;
        case '<':
            out += "&lt;";
            break;
        default:
            out.push_back(c);
            break;
        }
    }
    return out;
}

}  // namespace

std::string linux_package_name(const std::string& name) {
    std::string out;
    for (char c : lowercase(name)) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '+' ||
                        c == '.';
        out.push_back(ok ? c : '-');
    }
    return out;
}

LinuxDesktop linux_portable_desktop(const Project& decl, bool with_icon) {
    LinuxDesktop desktop;
    desktop.name = decl.name;
    desktop.comment = decl.windows.description;
    desktop.exec = "run.sh";
    desktop.icon = with_icon ? "icon.png" : "";
    desktop.identifier = decl.identifier;
    return desktop;
}

LinuxDesktop linux_deb_desktop(const Project& decl, bool with_icon) {
    LinuxDesktop desktop;
    desktop.name = decl.name;
    desktop.comment = decl.windows.description;
    const std::string package = linux_package_name(decl.name);
    desktop.exec = package;
    desktop.icon = with_icon ? package : "";
    desktop.identifier = decl.identifier;
    return desktop;
}

std::string linux_desktop_entry(const LinuxDesktop& desktop) {
    std::string text = "[Desktop Entry]\n"
                       "Type=Application\n"
                       "Name=" +
                       desktop.name +
                       "\n"
                       "Comment=" +
                       desktop.comment +
                       "\n"
                       "Exec=" +
                       desktop.exec + "\n";
    if (!desktop.icon.empty()) {
        text += "Icon=" + desktop.icon + "\n";
    }
    text += "Terminal=false\n"
            "Categories=Utility;\n"
            "X-Zui-Identifier=" +
            desktop.identifier + "\n";
    return text;
}

std::string linux_deb_control(const std::string& package, const std::string& version,
                              const std::string& arch, const std::string& maintainer,
                              const std::string& description, const std::string& identifier,
                              std::uintmax_t installed_bytes) {
    return "Package: " + package + "\n" + "Version: " + version + "\n" + "Architecture: " + arch +
           "\n" + "Maintainer: " + maintainer + "\n" +
           "Installed-Size: " + std::to_string((installed_bytes + 1023) / 1024) + "\n" +
           "Section: utils\n"
           "Priority: optional\n"
           "Depends: libc6\n"
           "X-Zui-Identifier: " +
           identifier + "\n" + "Description: " + description + "\n";
}

std::string linux_portable_page(const std::string& name, const std::string& page) {
    return name + "/" + page;
}

std::string linux_deb_page(const std::string& package, const std::string& page) {
    return "./usr/lib/" + package + "/" + page;
}

bool android_version_code(const std::string& version, int& code, std::string& error) {
    int parts[4] = {0, 0, 0, 0};
    int index = 0;
    int value = 0;
    bool any = false;
    for (std::size_t i = 0; i <= version.size(); ++i) {
        if (i == version.size() || version[i] == '.') {
            if (index >= 4) {
                error = "version " + version + " has more than four parts";
                return false;
            }
            if (any && value > 99) {
                error = "version " + version + " has a part above 99; android version codes "
                        "cannot carry it";
                return false;
            }
            parts[index++] = any ? value : 0;
            value = 0;
            any = false;
        } else {
            any = true;
            value = value * 10 + (version[i] - '0');
        }
    }
    code = parts[0] * 1000000 + parts[1] * 10000 + parts[2] * 100 + parts[3];
    if (code < 1) {
        code = 1;
    }
    return true;
}

std::string android_manifest(const Project& decl, int version_code, bool with_icon) {
    std::string permissions;
    std::vector<std::string> features;
    for (const Permission& permission : decl.permissions) {
        const CatalogPermission* known = find_catalog_permission(permission.name);
        if (known == nullptr) {
            continue;
        }
        if (known->max_sdk > 0) {
            permissions += "    <uses-permission android:name=\"" + permission.android +
                           "\" android:maxSdkVersion=\"" + std::to_string(known->max_sdk) +
                           "\"/>\n";
        } else {
            permissions += "    <uses-permission android:name=\"" + permission.android + "\"/>\n";
        }
        if (known->legacy != nullptr && known->legacy[0] != '\0') {
            permissions += "    <uses-permission android:name=\"" + std::string(known->legacy) +
                           "\" android:maxSdkVersion=\"" + std::to_string(known->legacy_max_sdk) +
                           "\"/>\n";
        }
        if (known->feature != nullptr && known->feature[0] != '\0') {
            features.emplace_back(known->feature);
        }
    }
    std::sort(features.begin(), features.end());
    features.erase(std::unique(features.begin(), features.end()), features.end());
    std::string feature_lines;
    for (const std::string& feature : features) {
        feature_lines += "    <uses-feature android:name=\"" + feature +
                         "\" android:required=\"false\"/>\n";
    }
    const std::string icon =
        with_icon ? " android:icon=\"@mipmap/ic_launcher\" android:roundIcon=\"@mipmap/ic_launcher\""
                  : "";
    std::string orientation;
    if (!decl.android.orientation.empty()) {
        orientation = " android:screenOrientation=\"" + decl.android.orientation + "\"";
    }
    const std::string debuggable = decl.android.debuggable ? " android:debuggable=\"true\"" : "";
    const std::string label = xml_text(decl.name);
    return "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
           "<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\"\n"
           "    package=\"" +
           decl.identifier + "\"\n    android:versionCode=\"" + std::to_string(version_code) +
           "\"\n    android:versionName=\"" + decl.version +
           "\">\n"
           "    <uses-sdk android:minSdkVersion=\"" +
           std::to_string(decl.android.min_sdk) + "\" android:targetSdkVersion=\"" +
           std::to_string(decl.android.target_sdk) + "\"/>\n" +
           permissions + feature_lines + "    <application android:label=\"" + label + "\"" + icon +
           debuggable +
           "\n"
           "        android:theme=\"@android:style/Theme.DeviceDefault.NoActionBar\"\n"
           "        android:allowBackup=\"true\"\n"
           "        android:hasCode=\"true\"\n"
           "        android:extractNativeLibs=\"true\">\n"
           "        <activity android:name=\"com.zui.runtime.ZuiActivity\"\n"
           "            android:exported=\"true\"\n"
           "            android:label=\"" +
           label + "\"\n            android:launchMode=\"singleTask\"" + orientation +
           "\n"
           "            android:configChanges=\"orientation|screenSize|screenLayout|"
           "keyboardHidden|smallestScreenSize|uiMode|density|fontScale\">\n"
           "            <intent-filter>\n"
           "                <action android:name=\"android.intent.action.MAIN\"/>\n"
           "                <category android:name=\"android.intent.category.LAUNCHER\"/>\n"
           "            </intent-filter>\n"
           "        </activity>\n"
           "    </application>\n"
           "</manifest>\n";
}

std::vector<std::pair<std::string, std::string>> apple_usage_entries(const Project& decl) {
    std::vector<std::pair<std::string, std::string>> out;
    auto add = [&out](const char* key, const std::string& reason) {
        if (key == nullptr || key[0] == '\0') {
            return;
        }
        for (const auto& have : out) {
            if (have.first == key) {
                return;
            }
        }
        out.emplace_back(key, reason);
    };
    for (const Permission& permission : decl.permissions) {
        const CatalogPermission* known = find_catalog_permission(permission.name);
        if (known == nullptr) {
            continue;
        }
        add(known->ios_key, permission.reason);
        add(known->ios_key_alt, permission.reason);
    }
    return out;
}

std::string apple_declared_permissions(const Project& decl) {
    std::string out;
    for (const Permission& permission : decl.permissions) {
        if (find_catalog_permission(permission.name) == nullptr) {
            continue;
        }
        if (!out.empty()) {
            out.push_back(' ');
        }
        out += permission.name;
    }
    return out;
}

std::string android_asset_entry(const std::string& relative) {
    std::string path = "assets/payload";
    if (!relative.empty()) {
        path.push_back('/');
        path += relative;
    }
    return path;
}

}  // namespace zui_tool
