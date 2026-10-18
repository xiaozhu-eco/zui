#include "document.h"

#include <utility>

namespace zmd {
namespace {
constexpr std::size_t kHistoryLimit = 200;
}

void Document::reset(std::string title, std::string source, std::filesystem::path path) {
    title_ = std::move(title);
    source_ = std::move(source);
    path_ = std::move(path);
    dirty_ = false;
    save_ = Save::Saved;
    undo_.clear();
    redo_.clear();
}

void Document::set_source(std::string source, bool edited) {
    if (edited && source != source_) {
        push_undo();
        dirty_ = true;
        save_ = Save::Typing;
    }
    source_ = std::move(source);
}

void Document::set_title(std::string title) {
    title_ = std::move(title);
}

void Document::set_path(std::filesystem::path path) {
    path_ = std::move(path);
    if (!path_.empty()) {
        title_ = path_.filename().u8string();
    }
}

void Document::mark_typing() {
    dirty_ = true;
    save_ = Save::Typing;
}

void Document::mark_saved() {
    dirty_ = false;
    save_ = Save::Saved;
}

void Document::mark_error() {
    save_ = Save::Error;
}

void Document::push_undo() {
    undo_.push_back(source_);
    if (undo_.size() > kHistoryLimit) {
        undo_.erase(undo_.begin());
    }
    redo_.clear();
}

bool Document::undo(std::string& out) {
    if (undo_.empty()) {
        return false;
    }
    redo_.push_back(source_);
    out = undo_.back();
    undo_.pop_back();
    source_ = out;
    dirty_ = true;
    save_ = Save::Typing;
    return true;
}

bool Document::redo(std::string& out) {
    if (redo_.empty()) {
        return false;
    }
    undo_.push_back(source_);
    out = redo_.back();
    redo_.pop_back();
    source_ = out;
    dirty_ = true;
    save_ = Save::Typing;
    return true;
}

}  // namespace zmd
