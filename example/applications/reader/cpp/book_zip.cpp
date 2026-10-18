#include "book_zip.h"

#include <cstdio>
#include <fstream>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "unzip.h"

namespace reader {
namespace {

std::string normalize_entry(std::string name) {
    for (char& c : name) {
        if (c == '\\') {
            c = '/';
        }
    }
    return name;
}

std::string path_narrow(const std::filesystem::path& path) {
    return path.string();
}

}  // namespace

ZipReader::~ZipReader() { close(); }

bool ZipReader::open(const std::filesystem::path& path) {
    close();
    // minizip uses the CRT narrow fopen path. On Windows the process code page must
    // be able to open the file; callers keep book paths under ASCII-friendly dirs when possible.
    file_ = unzOpen64(path_narrow(path).c_str());
    return file_ != nullptr;
}

void ZipReader::close() {
    if (file_ != nullptr) {
        unzClose(static_cast<unzFile>(file_));
        file_ = nullptr;
    }
}

std::vector<std::string> ZipReader::entries() const {
    std::vector<std::string> out;
    if (file_ == nullptr) {
        return out;
    }
    unzFile z = static_cast<unzFile>(file_);
    if (unzGoToFirstFile(z) != UNZ_OK) {
        return out;
    }
    do {
        char name[1024];
        unz_file_info64 info{};
        if (unzGetCurrentFileInfo64(z, &info, name, sizeof(name), nullptr, 0, nullptr, 0) ==
            UNZ_OK) {
            out.emplace_back(normalize_entry(name));
        }
    } while (unzGoToNextFile(z) == UNZ_OK);
    return out;
}

bool ZipReader::read(const std::string& entry, std::string& out) const {
    out.clear();
    if (file_ == nullptr) {
        return false;
    }
    unzFile z = static_cast<unzFile>(file_);
    const std::string want = normalize_entry(entry);
    if (unzLocateFile(z, want.c_str(), 0) != UNZ_OK) {
        // Some Windows zips store backslashes; scan entries.
        bool found = false;
        if (unzGoToFirstFile(z) == UNZ_OK) {
            do {
                char name[1024];
                unz_file_info64 info{};
                if (unzGetCurrentFileInfo64(z, &info, name, sizeof(name), nullptr, 0, nullptr, 0) !=
                    UNZ_OK) {
                    continue;
                }
                if (normalize_entry(name) == want) {
                    found = true;
                    break;
                }
            } while (unzGoToNextFile(z) == UNZ_OK);
        }
        if (!found) {
            return false;
        }
    }
    if (unzOpenCurrentFile(z) != UNZ_OK) {
        return false;
    }
    char buf[8192];
    int n = 0;
    while ((n = unzReadCurrentFile(z, buf, sizeof(buf))) > 0) {
        out.append(buf, static_cast<size_t>(n));
    }
    unzCloseCurrentFile(z);
    return n >= 0;
}

bool ZipReader::extract_to(const std::string& entry, const std::filesystem::path& dest) const {
    std::string bytes;
    if (!read(entry, bytes)) {
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(dest.parent_path(), ec);
    std::ofstream out(dest, std::ios::binary);
    if (!out) {
        return false;
    }
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

std::string zip_dirname(std::string_view path) {
    const auto slash = path.find_last_of('/');
    if (slash == std::string_view::npos) {
        return {};
    }
    return std::string(path.substr(0, slash + 1));
}

std::string zip_normalize(std::string path) {
    for (char& c : path) {
        if (c == '\\') {
            c = '/';
        }
    }
    while (path.size() >= 2 && path[0] == '.' && path[1] == '/') {
        path.erase(0, 2);
    }
    return path;
}

std::string zip_join(std::string_view base, std::string_view rel) {
    std::string r = zip_normalize(std::string(rel));
    if (r.empty()) {
        return zip_normalize(std::string(base));
    }
    if (r[0] == '/') {
        return r.substr(1);
    }
    if (r.find("://") != std::string::npos) {
        return r;
    }
    std::string dir = zip_normalize(std::string(base));
    if (!dir.empty() && dir.back() != '/') {
        dir = zip_dirname(dir);
    }
    std::vector<std::string> parts;
    auto push_path = [&](std::string_view s) {
        size_t i = 0;
        while (i < s.size()) {
            size_t j = s.find('/', i);
            if (j == std::string_view::npos) {
                j = s.size();
            }
            const std::string_view part = s.substr(i, j - i);
            if (part.empty() || part == ".") {
            } else if (part == "..") {
                if (!parts.empty()) {
                    parts.pop_back();
                }
            } else {
                parts.emplace_back(part);
            }
            i = j + (j < s.size() ? 1 : 0);
            if (j >= s.size()) {
                break;
            }
        }
    };
    push_path(dir);
    push_path(r);
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) {
            out.push_back('/');
        }
        out += parts[i];
    }
    return out;
}

}  // namespace reader
