#ifndef SHARE_UTIL_DETAIL_H
#define SHARE_UTIL_DETAIL_H

#include "util.h"

#include <filesystem>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace share {

namespace fs = std::filesystem;

/// UTF-8 path text in, a `std::filesystem::path` out. Windows takes the wide
/// conversion; other hosts use the native narrow path.
class Utf8Path {
public:
    static fs::path from(const std::string& s) {
#ifdef _WIN32
        return fs::u8path(s);
#else
        return fs::path(s);
#endif
    }

    static std::string to(const fs::path& p) {
#ifdef _WIN32
        auto u = p.u8string();
        return std::string(u.begin(), u.end());
#else
        return p.string();
#endif
    }

    static std::wstring wide(const std::string& s) {
#ifdef _WIN32
        if (s.empty()) {
            return L"";
        }
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        std::wstring w((size_t)n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
        return w;
#else
        (void)s;
        return {};
#endif
    }
};

/// Host override for `config.txt`. Empty means the platform default. One
/// function-local static, shared by every call in the process.
inline std::string& config_override() {
    static std::string dir;
    return dir;
}

}  // namespace share

#endif
