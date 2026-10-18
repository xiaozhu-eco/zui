#include "fs_util.h"

#include <fstream>
#include <iterator>

#if defined(_WIN32)
#include <sys/stat.h>
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace zdt {

bool read_file(const std::string& path, std::string& out, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open " + path;
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool file_exists(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return static_cast<bool>(in);
}

bool ensure_dirs_for(const std::string& path) {
    for (std::size_t i = 1; i < path.size(); ++i) {
        const char c = path[i];
        if (c != '/' && c != '\\') {
            continue;
        }
        const std::string dir = path.substr(0, i);
        if (dir.empty() || dir.back() == ':') {
            continue;
        }
#if defined(_WIN32)
        _mkdir(dir.c_str());
#else
        mkdir(dir.c_str(), 0755);
#endif
    }
    return true;
}

bool write_file(const std::string& path, const std::string& data, std::string& error) {
    ensure_dirs_for(path);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "cannot write " + path;
        return false;
    }
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!out) {
        error = "short write to " + path;
        return false;
    }
    return true;
}

std::string join_path(const std::string& base, const std::string& leaf) {
    if (base.empty()) {
        return leaf;
    }
    if (base.back() == '/' || base.back() == '\\') {
        return base + leaf;
    }
    return base + "/" + leaf;
}

}  // namespace zdt
