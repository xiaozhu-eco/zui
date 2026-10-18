#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#include "util_detail.h"

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
bool is_browse_roots(const std::string& path) {
    return path.empty();
}

std::string browse_roots_label() {
#ifdef _WIN32
    return "此电脑";
#elif defined(__ANDROID__)
    return "存储位置";
#elif defined(__APPLE__)
    return "访达位置";
#else
    return "文件系统";
#endif
}

bool is_fs_root_path(const std::string& path) {
    if (path.empty()) return false;
    fs::path p = Utf8Path::from(path);
    fs::path root = p.root_path();
    if (root.empty()) return false;
    auto a = Utf8Path::to(p.lexically_normal());
    auto b = Utf8Path::to(root.lexically_normal());
#ifdef _WIN32
    if (a.size() == 2 && a[1] == ':') return true;
#endif
    while (!a.empty() && (a.back() == '/' || a.back() == '\\')) a.pop_back();
    while (!b.empty() && (b.back() == '/' || b.back() == '\\')) b.pop_back();
#ifdef _WIN32
    for (auto& c : a) c = (char)tolower((unsigned char)c);
    for (auto& c : b) c = (char)tolower((unsigned char)c);
#endif
    return a == b;
}

std::vector<DirEnt> list_fs_roots() {
    std::vector<DirEnt> out;
#ifdef _WIN32
    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; i++) {
        if (!(mask & (1u << i))) continue;
        char letter = (char)('A' + i);
        std::string root;
        root.push_back(letter);
        root += ":\\";
        UINT type = GetDriveTypeA(root.c_str());
        if (type == DRIVE_NO_ROOT_DIR) continue;
        DirEnt d;
        d.name = std::string(1, letter) + ":";
        d.path = root;
        d.is_dir = true;
        d.size = 0;
        out.push_back(std::move(d));
    }
#elif defined(__ANDROID__)
    auto add_if_dir = [&](const char* name, const char* path) {
        if (!path_is_dir(path)) return;
        for (auto& e : out) if (e.path == path) return;
        DirEnt d;
        d.name = name;
        d.path = path;
        d.is_dir = true;
        out.push_back(std::move(d));
    };
    add_if_dir("内部存储", "/storage/emulated/0");
    add_if_dir("内部存储", "/sdcard");
    {
        std::error_code ec;
        fs::directory_iterator it("/storage", fs::directory_options::skip_permission_denied, ec);
        for (; it != fs::directory_iterator(); it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            auto name = it->path().filename().string();
            if (name == "emulated" || name == "self" || name.empty() || name[0] == '.') continue;
            std::error_code ec2;
            if (!it->is_directory(ec2)) continue;
            std::string p = it->path().string();
            add_if_dir(name.c_str(), p.c_str());
        }
    }
    if (out.empty()) add_if_dir("/", "/");
#elif defined(__APPLE__)
    {
        DirEnt d;
        d.name = "Macintosh HD";
        d.path = "/";
        d.is_dir = true;
        out.push_back(d);
    }
    if (path_is_dir("/Volumes")) {
        std::error_code ec;
        fs::directory_iterator it("/Volumes", fs::directory_options::skip_permission_denied, ec);
        for (; it != fs::directory_iterator(); it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            std::error_code ec2;
            if (!it->is_directory(ec2)) continue;
            auto name = it->path().filename().string();
            if (name.empty() || name[0] == '.') continue;
            DirEnt d;
            d.name = name;
            d.path = it->path().string();
            d.is_dir = true;
            out.push_back(std::move(d));
        }
    }
#else
    {
        DirEnt d;
        d.name = "/";
        d.path = "/";
        d.is_dir = true;
        out.push_back(d);
    }
    for (const char* m : {"/mnt", "/media", "/run/media"}) {
        if (!path_is_dir(m)) continue;
        DirEnt d;
        d.name = m;
        d.path = m;
        d.is_dir = true;
        out.push_back(std::move(d));
    }
#endif
    std::sort(out.begin(), out.end(), [](const DirEnt& a, const DirEnt& b) {
        return a.name < b.name;
    });
    return out;
}

std::string parent_dir(const std::string& path) {
    if (path.empty()) return {};
    if (is_fs_root_path(path)) return {};
#ifdef __ANDROID__
    if (path == "/storage/emulated/0" || path == "/sdcard" || path == "/sdcard/") return {};
#endif
    auto par = Utf8Path::from(path).parent_path();
    if (par.empty()) return {};
    auto s = Utf8Path::to(par);
    if (s.empty() || s == path) return {};
    return s;
}

bool path_exists(const std::string& path) {
    std::error_code ec;
    return fs::exists(Utf8Path::from(path), ec);
}

bool path_is_dir(const std::string& path) {
    if (path.empty()) return true;
    std::error_code ec;
    return fs::is_directory(Utf8Path::from(path), ec);
}

bool create_dir_all(const std::string& path) {
    std::error_code ec;
    fs::create_directories(Utf8Path::from(path), ec);
    return fs::is_directory(Utf8Path::from(path), ec);
}

uint64_t file_size_u8(const std::string& path) {
    std::error_code ec;
    auto n = fs::file_size(Utf8Path::from(path), ec);
    return ec ? 0 : (uint64_t)n;
}

uint64_t file_content_id_u8(const std::string& path) {
    FILE* fp = fopen_u8(path, "rb");
    if (!fp) return 0;
    uint64_t h = 14695981039346656037ull;
    uint8_t buf[1 << 16];
    for (;;) {
        size_t n = fread(buf, 1, sizeof(buf), fp);
        if (!n) break;
        for (size_t i = 0; i < n; i++) {
            h ^= (uint64_t)buf[i];
            h *= 1099511628211ull;
        }
    }
    fclose(fp);
    // Never return 0 for a readable file (0 means "unknown/dir").
    return h ? h : 1;
}


bool rename_u8(const std::string& from, const std::string& to) {
    std::error_code ec;
    fs::rename(Utf8Path::from(from), Utf8Path::from(to), ec);
    if (!ec) return true;
    fs::remove(Utf8Path::from(to), ec);
    fs::rename(Utf8Path::from(from), Utf8Path::from(to), ec);
    return !ec;
}

bool remove_u8(const std::string& path) {
    std::error_code ec;
    fs::remove(Utf8Path::from(path), ec);
    return !ec;
}


std::string unique_path_u8(const std::string& path) {
    if (!path_exists(path)) return path;
    fs::path p = Utf8Path::from(path);
    fs::path dir = p.parent_path();
    std::string stem = Utf8Path::to(p.stem());
    std::string ext = Utf8Path::to(p.extension());
    for (int i = 1; i < 10000; i++) {
        fs::path cand = dir / Utf8Path::from(stem + " (" + std::to_string(i) + ")" + ext);
        std::string s = Utf8Path::to(cand);
        if (!path_exists(s)) return s;
    }
    return path;
}


std::string dest_join(const std::string& root, const std::string& rel) {
    if (rel.empty()) return {};
    auto r = Utf8Path::from(rel);
    if (r.is_absolute()) return {};
    auto full = Utf8Path::from(root);
    for (const auto& part : r) {
        auto s = Utf8Path::to(part);
        if (s.empty() || s == "." || s == "..") return {};
        full /= part;
    }
    return Utf8Path::to(full);
}


static std::string url_decode_pct(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '%' && i + 2 < s.size()) {
            const int hi = hex(s[i + 1]);
            const int lo = hex(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 3;
                continue;
            }
        }
        out.push_back(s[i++]);
    }
    return out;
}

static bool under_android_primary(const std::string& path) {
    static const char* roots[] = {
        "/storage/emulated/0",
        "/sdcard",
    };
    for (const char* root : roots) {
        const size_t n = std::strlen(root);
        if (path == root) return true;
        if (path.size() > n && path.compare(0, n, root) == 0 &&
            (path[n] == '/' || path[n] == '\\')) {
            return true;
        }
    }
    return false;
}

std::string try_resolve_save_dir(const std::string& path_or_uri) {
    if (path_or_uri.empty()) return {};

    // content:// — map only the well-known primary tree id; never return the URI.
    if (path_or_uri.rfind("content://", 0) == 0) {
        static const char kPrefix[] =
            "content://com.android.externalstorage.documents/tree/";
        if (path_or_uri.rfind(kPrefix, 0) != 0) return {};
        std::string doc = path_or_uri.substr(sizeof(kPrefix) - 1);
        // Drop optional /document/... suffix if present.
        const auto slash = doc.find('/');
        if (slash != std::string::npos) doc.resize(slash);
        doc = url_decode_pct(doc);
        if (doc.rfind("primary:", 0) != 0) return {};
        std::string rel = doc.substr(8);  // after "primary:"
        std::string mapped = "/storage/emulated/0";
        if (!rel.empty()) {
            if (rel.front() == '/') mapped += rel;
            else mapped += "/" + rel;
        }
        if (!create_dir_all(mapped)) return {};
        return mapped;
    }

    const fs::path p = Utf8Path::from(path_or_uri);
    if (!p.is_absolute()) return {};

#if defined(__ANDROID__)
    if (!under_android_primary(path_or_uri)) return {};
#endif

    if (!create_dir_all(path_or_uri)) return {};
    return path_or_uri;
}

Config load_config() {
    Config c;
    auto dir = config_dir();
    create_dir_all(dir);
    auto path = join_path(dir, "config.txt");
    std::ifstream in(Utf8Path::from(path));
    std::string line;
    while (in && std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto k = line.substr(0, eq);
        auto v = line.substr(eq + 1);
        if (k == "id") c.id = std::strtoull(v.c_str(), nullptr, 16);
        else if (k == "name") c.name = v;
        else if (k == "save_dir") c.save_dir = v;
    }
    if (c.id == 0) c.id = random_u64();
    if (c.save_dir.empty()) {
        c.save_dir = default_save_dir();
    } else {
        // Must NOT clear solely because is_android() is true.
        const std::string resolved = try_resolve_save_dir(c.save_dir);
        if (resolved.empty()) {
            // Keep raw non-URI paths; only force default when empty/invalid URI.
            if (c.save_dir.rfind("content://", 0) == 0) {
                c.save_dir = default_save_dir();
            }
        } else {
            c.save_dir = resolved;
        }
    }
    save_config(c);
    return c;
}

void save_config(const Config& c) {
    auto dir = config_dir();
    create_dir_all(dir);
    auto path = join_path(dir, "config.txt");
    std::ofstream out(Utf8Path::from(path), std::ios::trunc);
    char idbuf[32];
    std::snprintf(idbuf, sizeof(idbuf), "%016llx", (unsigned long long)c.id);
    out << "id=" << idbuf << "\n";
    out << "name=" << c.name << "\n";
    out << "save_dir=" << c.save_dir << "\n";
}
}  // namespace share
