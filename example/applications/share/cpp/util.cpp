#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#include "util_detail.h"

#include <zui/host.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace share {
bool is_android() {
#if defined(__ANDROID__)
    return true;
#else
    return false;
#endif
}

void set_config_dir(std::string dir) { config_override() = std::move(dir); }

static bool is_termux() {
    const char* p = std::getenv("PREFIX");
    return p && std::strstr(p, "com.termux");
}

bool storage_access_ok(zui::Host& host) {
#if defined(__ANDROID__)
    if (is_termux()) return true;
    return host.permission_state(zui::Permission::ManageStorage) == zui::PermissionState::Granted;
#else
    (void)host;
    return true;
#endif
}

bool storage_access_ok() {
#if defined(__ANDROID__)
    if (is_termux()) return true;
    // Prefer storage_access_ok(Host&) from Session; without a Host, deny.
    return false;
#else
    return true;
#endif
}

void request_storage_access(zui::Host& host, zui::Host::PermissionHandler done) {
#if defined(__ANDROID__)
    if (is_termux()) {
        if (done) done(zui::PermissionState::Granted);
        return;
    }
    host.request_permission(zui::Permission::ManageStorage, std::move(done));
#else
    (void)host;
    if (done) done(zui::PermissionState::Unsupported);
#endif
}

void request_storage_access() {
    // Prefer request_storage_access(Host&, ...) from Session; no-op without a Host.
}

std::string home_dir() {
#ifdef _WIN32
    const char* p = std::getenv("USERPROFILE");
    if (p && *p) return p;
    const char* d = std::getenv("HOMEDRIVE");
    const char* h = std::getenv("HOMEPATH");
    if (d && h) return std::string(d) + h;
    return "C:/";
#else
    const char* p = std::getenv("HOME");
    if (p && *p) return p;
#if defined(__ANDROID__)
    return "/storage/emulated/0";
#else
    return "/";
#endif
#endif
}

std::string config_dir() {
    if (!config_override().empty()) {
        return config_override();
    }
#ifdef _WIN32
    const char* p = std::getenv("APPDATA");
    if (p && *p) return join_path(p, "xz-share");
    return join_path(home_dir(), "xz-share");
#elif defined(__APPLE__)
    return join_path(join_path(home_dir(), "Library/Application Support"), "xz-share");
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) return join_path(xdg, "xz-share");
    return join_path(join_path(home_dir(), ".config"), "xz-share");
#endif
}

std::string default_save_dir() {
    if (is_android()) {
        const char* cands[] = {
            "/storage/emulated/0/Download/小竹分享",
            "/sdcard/Download/小竹分享",
        };
        for (const char* c : cands) {
            auto parent = parent_dir(c);
            if (path_is_dir(parent) || path_exists(parent)) return c;
        }
        auto dl = join_path(join_path(home_dir(), "storage"), "downloads");
        if (path_is_dir(dl)) return join_path(dl, "小竹分享");
        return join_path(home_dir(), "小竹分享");
    }
    auto downloads = join_path(home_dir(), "Downloads");
    if (path_is_dir(downloads) || path_exists(downloads)) return join_path(downloads, "小竹分享");
    return join_path(home_dir(), "小竹分享");
}

std::string default_browse_dir() {
    if (is_android()) {
        const char* cands[] = {
            "/storage/emulated/0",
            "/sdcard",
        };
        for (const char* c : cands) {
            if (path_is_dir(c)) return c;
        }
        auto shared = join_path(join_path(home_dir(), "storage"), "shared");
        if (path_is_dir(shared)) return shared;
    }
    return home_dir();
}

std::string join_path(const std::string& a, const std::string& b) {
    return Utf8Path::to(Utf8Path::from(a) / Utf8Path::from(b));
}

std::string path_to_file_uri(std::string_view path) {
    if (path.empty()) {
        return {};
    }
    std::string normalized(path);
    for (char& ch : normalized) {
        if (ch == '\\') {
            ch = '/';
        }
    }
    // Windows drive: C:/a/b -> file:///C:/a/b
    if (normalized.size() >= 2 &&
        ((normalized[0] >= 'A' && normalized[0] <= 'Z') ||
         (normalized[0] >= 'a' && normalized[0] <= 'z')) &&
        normalized[1] == ':') {
        return std::string("file:///") + normalized;
    }
    // Absolute POSIX (and already-normalized rooted paths): /a/b -> file:///a/b
    if (!normalized.empty() && normalized[0] == '/') {
        return std::string("file://") + normalized;
    }
    return std::string("file:///") + normalized;
}

}  // namespace share
