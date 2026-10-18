#include "browser.h"

#include <algorithm>

namespace share {

std::string FileBrowser::label() const {
    return dir_.empty() ? browse_roots_label() : dir_;
}

void FileBrowser::refresh(bool force) {
    if (!force && listed_ == dir_) {
        return;
    }
    listed_ = dir_;
    if (!path_is_dir(dir_)) {
        entries_.clear();
        error_ = "无法打开目录：" + dir_;
        return;
    }
    entries_ = list_dir(dir_);
    error_.clear();
    prune_selection();
}

void FileBrowser::prune_selection() {
    if (dir_.empty()) {
        return;
    }
    for (auto it = selected_.begin(); it != selected_.end();) {
        it = path_exists(*it) ? std::next(it) : selected_.erase(it);
    }
}

bool FileBrowser::open(const std::string& path) {
    if (!path_is_dir(path)) {
        error_ = "无法打开目录：" + path;
        return false;
    }
    dir_ = path;
    refresh(true);
    return error_.empty();
}

bool FileBrowser::up() {
    const std::string parent = parent_dir(dir_);
    if (parent.empty() || parent == dir_) {
        return false;
    }
    dir_ = parent;
    refresh(true);
    return error_.empty();
}

void FileBrowser::toggle(const std::string& path) {
    if (selected_.count(path) != 0) {
        selected_.erase(path);
        return;
    }
    selected_.insert(path);
}

std::uint64_t FileBrowser::selected_bytes() const {
    std::uint64_t total = 0;
    for (const std::string& path : selected_) {
        if (!path_is_dir(path)) {
            total += file_size_u8(path);
        }
    }
    return total;
}

std::string FileBrowser::selection_label() const {
    if (selected_.empty()) {
        return "已选 0 项";
    }
    const std::uint64_t bytes = selected_bytes();
    if (bytes == 0) {
        return "已选 " + std::to_string(selected_.size()) + " 项";
    }
    return "已选 " + std::to_string(selected_.size()) + " 项 · " + format_bytes(bytes);
}

std::vector<ShareFile> FileBrowser::expand() const { return expand_selection(selected_); }

}  // namespace share
