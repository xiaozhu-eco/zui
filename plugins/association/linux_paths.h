#ifndef ZUI_PLUGIN_ASSOCIATION_LINUX_PATHS_H
#define ZUI_PLUGIN_ASSOCIATION_LINUX_PATHS_H

#include <string>
#include <string_view>
#include <vector>

#if defined(__linux__) && !defined(__ANDROID__)
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#endif

namespace zui {
namespace association_linux {

inline std::string field_value(std::string_view text, std::string_view key) {
    const std::string prefix = std::string(key) + "=";
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        const std::string_view line(text.data() + at, end - at);
        if (line.size() >= prefix.size() && line.compare(0, prefix.size(), prefix) == 0) {
            return std::string(line.substr(prefix.size()));
        }
        if (end == text.size()) {
            break;
        }
        at = end + 1;
    }
    return {};
}

inline void ensure_lines(std::string& text, const std::vector<std::string>& lines) {
    if (text.find("[Default Applications]") == std::string::npos) {
        if (!text.empty() && text.back() != '\n') {
            text.push_back('\n');
        }
        text += "[Default Applications]\n";
    }
    for (const std::string& line : lines) {
        if (line.empty() || text.find(line) != std::string::npos) {
            continue;
        }
        const std::size_t header = text.find("[Default Applications]");
        const std::size_t end = text.find('\n', header);
        if (end == std::string::npos) {
            text.push_back('\n');
            text += line;
            text.push_back('\n');
        } else {
            text.insert(end + 1, line + "\n");
        }
    }
}

inline std::string without_desktop(std::string text, const std::string& desktop_name) {
    std::string out;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        const bool last = end == std::string::npos;
        if (last) {
            end = text.size();
        }
        const std::string line = text.substr(at, end - at);
        if (line.find(desktop_name) == std::string::npos) {
            out += line;
            out.push_back('\n');
        }
        if (last) {
            break;
        }
        at = end + 1;
    }
    return out;
}

#if defined(__linux__) && !defined(__ANDROID__)

inline bool dirs(std::filesystem::path& applications, std::filesystem::path& mimeapps) {
    const char* data = std::getenv("XDG_DATA_HOME");
    const char* config = std::getenv("XDG_CONFIG_HOME");
    const char* home_raw = std::getenv("HOME");
    const std::string home = home_raw == nullptr ? std::string() : std::string(home_raw);
    if ((data == nullptr || data[0] == '\0') && home.empty()) {
        return false;
    }
    const std::filesystem::path data_home =
        (data != nullptr && data[0] != '\0') ? std::filesystem::path(data) : std::filesystem::path(home) / ".local" / "share";
    const std::filesystem::path config_home =
        (config != nullptr && config[0] != '\0') ? std::filesystem::path(config) : std::filesystem::path(home) / ".config";
    if (config_home.empty() && home.empty()) {
        return false;
    }
    applications = data_home / "applications";
    mimeapps = config_home / "mimeapps.list";
    return true;
}

inline std::string read_text(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

inline bool write_text(const std::filesystem::path& path, std::string_view text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
    return static_cast<bool>(out);
}

#endif

}  // namespace association_linux
}  // namespace zui

#endif
