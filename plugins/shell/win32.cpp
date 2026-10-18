#include "ext/slots.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace zui {
namespace backends {
namespace {

namespace fs = std::filesystem;

std::string effective_app_id(std::string_view app_id) {
    return app_id.empty() ? std::string("app") : std::string(app_id);
}

std::string env_or_empty(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string();
}

std::string ensure_dir(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir.string();
}

bool ascii_ieq_prefix(std::string_view text, std::string_view prefix) {
    if (text.size() < prefix.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        char a = text[i];
        const char b = prefix[i];
        if (a >= 'A' && a <= 'Z') {
            a = static_cast<char>(a - 'A' + 'a');
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

bool uri_scheme_allowed(std::string_view uri) {
    return ascii_ieq_prefix(uri, "https:") || ascii_ieq_prefix(uri, "http:") ||
           ascii_ieq_prefix(uri, "mailto:") || ascii_ieq_prefix(uri, "tel:") ||
           ascii_ieq_prefix(uri, "file:");
}

#ifdef _WIN32
std::wstring utf8_to_wide(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), size);
    return wide;
}
#endif

class Win32Shell final : public ShellService {
public:
    bool open_uri(std::string_view uri) override {
#ifdef _WIN32
        if (!uri_scheme_allowed(uri)) {
            return false;
        }
        const std::wstring wide = utf8_to_wide(uri);
        if (wide.empty()) {
            return false;
        }
        const HINSTANCE launched =
            ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(launched) > 32;
#else
        (void)uri;
        return false;
#endif
    }

    std::string user_data_dir(std::string_view app_id) const override {
        const std::string id = effective_app_id(app_id);
#ifdef _WIN32
        const std::string appdata = env_or_empty("APPDATA");
        if (!appdata.empty()) {
            return ensure_dir(fs::path(appdata) / "zui" / id);
        }
#endif
        return ensure_dir(fs::temp_directory_path() / "zui" / id);
    }

    std::string cache_dir(std::string_view app_id) const override {
        const std::string id = effective_app_id(app_id);
#ifdef _WIN32
        const std::string local = env_or_empty("LOCALAPPDATA");
        if (!local.empty()) {
            return ensure_dir(fs::path(local) / "zui" / id / "Cache");
        }
        const std::string appdata = env_or_empty("APPDATA");
        if (!appdata.empty()) {
            return ensure_dir(fs::path(appdata) / "zui" / id / "Cache");
        }
#endif
        return ensure_dir(fs::temp_directory_path() / "zui" / id / "Cache");
    }
};

}  // namespace

void register_win32_shell(ExtensionRegistry& registry) {
#if defined(_WIN32)
    registry.add_shell(PlatformKind::Win32, std::make_unique<Win32Shell>());
#else
    (void)registry;
#endif
}

}  // namespace backends
}  // namespace zui
