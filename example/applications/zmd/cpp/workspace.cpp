#include "workspace.h"

#include <algorithm>
#include <cctype>

#include "util.h"

namespace zmd {
namespace {

bool is_markdown(const std::filesystem::path& path) {
    std::string ext = util::lower(path.extension().string());
    return ext == ".md" || ext == ".markdown" || ext == ".txt";
}

/// A name from the page is UTF-8. `std::filesystem::path(std::string)` on Windows goes
/// through the narrow locale and throws on CJK, so every name->path conversion uses
/// u8path. Same for the reverse direction.
std::filesystem::path path_from_utf8(std::string_view text) {
    return std::filesystem::u8path(std::string(text));
}

std::string path_to_utf8(const std::filesystem::path& path) {
    // C++17: u8string() already returns std::string on this toolchain.
    return path.u8string();
}

bool by_folder_then_name(const FileEntry& a, const FileEntry& b) {
    if (a.folder != b.folder) {
        return a.folder;
    }
    std::string left = util::lower(a.name);
    std::string right = util::lower(b.name);
    return left < right;
}

}  // namespace

Workspace::Workspace(std::filesystem::path root) {
    set_root(std::move(root));
}

void Workspace::set_root(std::filesystem::path root) {
    root_ = std::move(root);
    folder_.clear();
    rescan();
}

std::string Workspace::name() const {
    if (root_.empty()) {
        return "工作目录";
    }
    std::string leaf = path_to_utf8(root_.filename());
    return leaf.empty() ? path_to_utf8(root_) : leaf;
}

std::filesystem::path Workspace::path_of(std::string_view name) const {
    return dir() / path_from_utf8(name);
}

std::filesystem::path Workspace::dir() const {
    return folder_.empty() ? root_ : root_ / path_from_utf8(folder_);
}

std::string Workspace::folder_name() const {
    if (folder_.empty()) {
        return "\xe6\x96\x87\xe4\xbb\xb6";  // 文件
    }
    return path_to_utf8(path_from_utf8(folder_).filename());
}

bool Workspace::enter(std::string_view name) {
    if (name.empty() || name.find('/') != std::string_view::npos ||
        name.find('\\') != std::string_view::npos) {
        return false;
    }
    const std::filesystem::path candidate = dir() / path_from_utf8(name);
    std::error_code ec;
    if (!std::filesystem::is_directory(candidate, ec)) {
        return false;
    }
    std::string relative = folder_.empty() ? std::string(name)
                                           : folder_ + "/" + std::string(name);
    folder_ = std::move(relative);
    rescan();
    return true;
}

bool Workspace::up() {
    if (folder_.empty()) {
        return false;
    }
    const std::size_t cut = folder_.find_last_of('/');
    folder_ = cut == std::string::npos ? std::string() : folder_.substr(0, cut);
    rescan();
    return true;
}

void Workspace::rescan() {
    entries_.clear();
    if (root_.empty()) {
        return;
    }
    std::error_code ec;
    const std::filesystem::path shown = dir();
    std::filesystem::directory_iterator it(shown, ec);
    if (ec) {
        return;
    }
    for (const std::filesystem::directory_entry& item : it) {
        std::error_code entry_ec;
        const std::string name = path_to_utf8(item.path().filename());
        if (!name.empty() && name.front() == '.') {
            continue;  // dot-files are not documents
        }
        if (item.is_directory(entry_ec)) {
            entries_.push_back(FileEntry{name, true, false});
        } else if (item.is_regular_file(entry_ec) && is_markdown(item.path())) {
            entries_.push_back(FileEntry{name, false, false});
        }
    }
    std::sort(entries_.begin(), entries_.end(), by_folder_then_name);
}

std::string unique_name_in(const std::filesystem::path& dir, std::string_view stem,
                           std::string_view ext) {
    auto taken = [&dir](const std::string& candidate) {
        std::error_code ec;
        return std::filesystem::exists(dir / path_from_utf8(candidate), ec);
    };
    std::string first = std::string(stem) + std::string(ext);
    if (!taken(first)) {
        return first;
    }
    for (int i = 2; i < 1000; ++i) {
        std::string next = std::string(stem) + " " + std::to_string(i) + std::string(ext);
        if (!taken(next)) {
            return next;
        }
    }
    return first;
}

std::string Workspace::unique_name(std::string_view stem, std::string_view ext) const {
    return unique_name_in(dir(), stem, ext);
}

}  // namespace zmd
