#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#include "util_detail.h"

#include "net.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
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
std::vector<DirEnt> list_dir(const std::string& path) {
    if (path.empty()) return list_fs_roots();
    std::vector<DirEnt> out;
#ifndef _WIN32
    DIR* dp = opendir(path.c_str());
    if (!dp) return out;
    while (struct dirent* ent = readdir(dp)) {
        if (!ent->d_name[0] || ent->d_name[0] == '.') continue;
        DirEnt d;
        d.name = ent->d_name;
        d.path = join_path(path, d.name);
        bool typed = false;
#ifdef DT_DIR
        if (ent->d_type == DT_DIR) {
            d.is_dir = true;
            typed = true;
        } else if (ent->d_type == DT_REG || ent->d_type == DT_LNK) {
            d.is_dir = false;
            typed = true;
        }
#endif
        struct stat st {};
        if (stat(d.path.c_str(), &st) == 0) {
            d.is_dir = S_ISDIR(st.st_mode);
            if (!d.is_dir) d.size = (uint64_t)st.st_size;
        } else if (!typed) {
            d.is_dir = false;
        }
        out.push_back(std::move(d));
    }
    closedir(dp);
#else
    std::error_code ec;
    auto dir = Utf8Path::from(path);
    if (!fs::is_directory(dir, ec)) return out;
    fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    fs::directory_iterator end;
    while (it != end) {
        if (ec) {
            ec.clear();
            it.increment(ec);
            continue;
        }
        std::error_code ec2;
        auto name = Utf8Path::to(it->path().filename());
        if (!name.empty() && name[0] != '.') {
            DirEnt d;
            d.name = name;
            d.path = Utf8Path::to(it->path());
            d.is_dir = it->is_directory(ec2);
            if (!d.is_dir) {
                auto n = it->file_size(ec2);
                if (!ec2) d.size = n;
            }
            out.push_back(std::move(d));
        }
        it.increment(ec);
    }
#endif
    std::sort(out.begin(), out.end(), [](const DirEnt& a, const DirEnt& b) {
        if (a.is_dir != b.is_dir) return a.is_dir && !b.is_dir;
        return a.name < b.name;
    });
    return out;
}

std::vector<ShareFile> expand_selection(const std::set<std::string>& sel) {
    std::vector<ShareFile> out;
    std::error_code ec;
    for (const auto& s : sel) {
        auto p = Utf8Path::from(s);
        if (fs::is_directory(p, ec)) {
            ShareFile d;
            d.rel = Utf8Path::to(p.filename());
            d.abs = s;
            d.is_dir = true;
            out.push_back(d);
            fs::recursive_directory_iterator it(
                p, fs::directory_options::skip_permission_denied, ec);
            fs::recursive_directory_iterator end;
            for (; !ec && it != end; it.increment(ec)) {
                std::error_code ec2;
                auto rel = fs::relative(it->path(), p.parent_path(), ec2);
                if (ec2) continue;
                auto rels = Utf8Path::to(rel);
                if (rels.find("..") != std::string::npos) continue;
                if (it->is_directory(ec2)) {
                    ShareFile f;
                    f.rel = rels;
                    f.abs = Utf8Path::to(it->path());
                    f.is_dir = true;
                    out.push_back(std::move(f));
                } else if (it->is_regular_file(ec2)) {
                    ShareFile f;
                    f.rel = rels;
                    f.abs = Utf8Path::to(it->path());
                    f.size = it->file_size(ec2);
                    f.content_id = file_content_id_u8(f.abs);
                    out.push_back(std::move(f));
                }
            }
        } else if (fs::is_regular_file(p, ec)) {
            ShareFile f;
            f.rel = Utf8Path::to(p.filename());
            f.abs = s;
            f.size = fs::file_size(p, ec);
            f.content_id = file_content_id_u8(f.abs);
            out.push_back(std::move(f));
        }
    }
    std::set<std::string> used;
    for (auto& f : out) {
        if (used.insert(f.rel).second) continue;
        auto parent = Utf8Path::to(Utf8Path::from(f.abs).parent_path().filename());
        if (!parent.empty()) f.rel = parent + "/" + f.rel;
        used.insert(f.rel);
    }
    return out;
}


std::string notify_incoming_body(const IncomingOffer& o) {
    const std::string nfiles = (o.nfiles == 1)
        ? std::string("1 个文件")
        : (std::to_string(o.nfiles) + " 个文件");
    return o.from_name + " 想发送 " + nfiles + "（" + format_bytes(o.nbytes) + "）";
}

std::string notify_done_body(const XferView& v) {
    const char* verb = v.sending ? "发送" : "接收";
    return std::string(verb) + "完成：与 " + v.peer_name + "，共 " + format_bytes(v.total);
}

std::string notify_fail_body(const XferView& v) {
    if (v.phase == XferPhase::Rejected) {
        return v.peer_name + " 拒绝了此次发送";
    }
    std::string body = "与 " + v.peer_name + " 的传输失败";
    if (!v.error.empty()) {
        std::string tail = v.error;
        constexpr std::size_t kMaxTail = 80;
        if (tail.size() > kMaxTail) {
            tail.resize(kMaxTail);
        }
        body += "：";
        body += tail;
    }
    return body;
}

std::string format_bytes(uint64_t n) {
    const char* u[] = {"B", "KB", "MB", "GB", "TB"};
    double x = (double)n;
    int i = 0;
    while (x >= 1024.0 && i < 4) {
        x /= 1024.0;
        i++;
    }
    char buf[64];
    if (i == 0) std::snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)n);
    else std::snprintf(buf, sizeof(buf), "%.2f %s", x, u[i]);
    return buf;
}

std::string format_rate(double bytes_per_sec) {
    if (bytes_per_sec < 0) bytes_per_sec = 0;
    return format_bytes((uint64_t)bytes_per_sec) + "/s";
}

double now_sec() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

uint64_t random_u64() {
    std::random_device rd;
    uint64_t a = rd();
    uint64_t b = rd();
    auto t = (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count();
    return (a << 32) ^ b ^ t ^ (t << 17);
}

FILE* fopen_u8(const std::string& path, const char* mode) {
#ifdef _WIN32
    auto wp = Utf8Path::wide(path);
    auto wm = Utf8Path::wide(mode);
    return _wfopen(wp.c_str(), wm.c_str());
#else
    return std::fopen(path.c_str(), mode);
#endif
}

int file_seek(FILE* fp, uint64_t off) {
#ifdef _WIN32
    return _fseeki64(fp, (__int64)off, SEEK_SET);
#else
    return fseeko(fp, (off_t)off, SEEK_SET);
#endif
}

std::string trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= ' ') a++;
    while (b > a && (unsigned char)s[b - 1] <= ' ') b--;
    return std::string(s.substr(a, b - a));
}
}  // namespace share
