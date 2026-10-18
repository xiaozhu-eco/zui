#include "rows.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace share {
namespace {

std::string lower(std::string_view text) {
    std::string out(text);
    for (char& ch : out) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

std::string extension_of(std::string_view name) {
    const std::size_t dot = name.rfind('.');
    if (dot == std::string_view::npos || dot + 1 >= name.size()) {
        return {};
    }
    return lower(name.substr(dot + 1));
}

bool any_of(std::string_view value, std::initializer_list<std::string_view> options) {
    for (std::string_view option : options) {
        if (value == option) {
            return true;
        }
    }
    return false;
}

}  // namespace

std::string file_kind_class(std::string_view name, bool is_dir) {
    if (is_dir) {
        return "dir";
    }
    const std::string ext = extension_of(name);
    if (ext == "pdf") {
        return "pdf";
    }
    if (ext == "apk") {
        return "apk";
    }
    if (any_of(ext, {"mp4", "mkv", "avi", "mov", "webm", "m4v"})) {
        return "vid";
    }
    if (any_of(ext, {"png", "jpg", "jpeg", "gif", "webp", "bmp", "heic"})) {
        return "img";
    }
    return "doc";
}

std::string file_kind_label(std::string_view name, bool is_dir) {
    const std::string kind = file_kind_class(name, is_dir);
    if (kind == "dir") {
        return "文件夹";
    }
    if (kind == "pdf") {
        return "PDF";
    }
    if (kind == "apk") {
        return "安装包";
    }
    if (kind == "vid") {
        return "视频";
    }
    if (kind == "img") {
        return "图片";
    }
    return "文本";
}

std::string first_glyph(std::string_view text) {
    if (text.empty()) {
        return "竹";
    }
    const unsigned char lead = static_cast<unsigned char>(text[0]);
    std::size_t length = 1;
    if (lead >= 0xF0) {
        length = 4;
    } else if (lead >= 0xE0) {
        length = 3;
    } else if (lead >= 0xC0) {
        length = 2;
    }
    length = (std::min)(length, text.size());
    return std::string(text.substr(0, length));
}

/* ---------------------------------------------------------------- peer chips */

std::size_t PeerChipRows::row_count() const { return peers_->size(); }

std::string PeerChipRows::row_key(std::size_t row) const {
    return row < peers_->size() ? std::to_string((*peers_)[row].id) : std::string{};
}

std::string PeerChipRows::value(std::size_t row, std::string_view key) const {
    if (row >= peers_->size()) {
        return {};
    }
    const Peer& peer = (*peers_)[row];
    if (key == "id") {
        return std::to_string(peer.id);
    }
    if (key == "name") {
        return peer.name;
    }
    return {};
}

/* ---------------------------------------------------------------- peer cards */

std::size_t PeerCardRows::row_count() const { return peers_->size(); }

std::string PeerCardRows::row_key(std::size_t row) const {
    return row < peers_->size() ? std::to_string((*peers_)[row].id) : std::string{};
}

std::string PeerCardRows::value(std::size_t row, std::string_view key) const {
    if (row >= peers_->size()) {
        return {};
    }
    const Peer& peer = (*peers_)[row];
    if (key == "id") {
        return std::to_string(peer.id);
    }
    if (key == "initial") {
        return first_glyph(peer.name);
    }
    if (key == "name") {
        return peer.name;
    }
    if (key == "ip") {
        return peer.ip;
    }
    return {};
}

/* ---------------------------------------------------------------- file rows */

std::size_t FileRows::row_count() const { return browser_->entries().size(); }

std::string FileRows::row_key(std::size_t row) const {
    return row < browser_->entries().size() ? browser_->entries()[row].path : std::string{};
}

std::string FileRows::value(std::size_t row, std::string_view key) const {
    if (row >= browser_->entries().size()) {
        return {};
    }
    const DirEnt& entry = browser_->entries()[row];
    if (key == "id") {
        return std::to_string(row);
    }
    if (key == "path") {
        return entry.path;
    }
    if (key == "cls") {
        // The row template is `class="frow {{cls}}"`.
        return std::string(browser_->is_selected(entry.path) ? "on " : "") +
               (entry.is_dir ? "is-dir" : "is-file");
    }
    if (key == "kind") {
        return file_kind_class(entry.name, entry.is_dir);
    }
    if (key == "name") {
        return entry.name;
    }
    if (key == "size") {
        return entry.is_dir ? std::string("—") : format_bytes(entry.size);
    }
    if (key == "type") {
        return file_kind_label(entry.name, entry.is_dir);
    }
    return {};
}

/* ---------------------------------------------------------------- done rows */

std::size_t DoneRows::row_count() const { return names_->size(); }

std::string DoneRows::value(std::size_t row, std::string_view key) const {
    if (row >= names_->size() || key != "name") {
        return {};
    }
    return (*names_)[row];
}

/* ---------------------------------------------------------------- progress */

std::size_t ProgressRows::row_count() const { return 1; }

std::string ProgressRows::value(std::size_t row, std::string_view key) const {
    if (row != 0 || key != "pct") {
        return {};
    }
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%.1f", percent_);
    return buffer;
}

}  // namespace share
