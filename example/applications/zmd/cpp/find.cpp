#include "find.h"

#include <string>

#include "document.h"
#include "dom.h"
#include "editor.h"

namespace zmd {

Find::Find(zui::Document& page, Document& doc, Editor& editor)
    : page_(page), later_(page), doc_(doc), editor_(editor) {}

void Find::open(bool with_replace) {
    open_ = true;
    replace_ = with_replace;
    page_.set(dom::Id::find, "visible", "1");
    // Focus after the frame's style flush: the visible write clears the `hidden` attribute, and
    // the computed `display: none` is still the old one in this turn - `is_focusable` refuses a
    // node the style table has not shown yet, so an immediate focus would leave the editor with
    // the keyboard while the bar sits open. The browser's `findInput().focus()` runs after the
    // DOM update and lands.
    later_.run([this] { page_.focus(dom::Id::find_input); });
}

void Find::close() {
    open_ = false;
    page_.set(dom::Id::find, "visible", "0");
    page_.set(dom::Id::find_count, "text", "");
}

void Find::set_query(std::string_view query) {
    query_ = std::string(query);
    cursor_ = 0;
    recount();
    report();
    if (matches_ > 0) {
        select_cursor();
    }
}

void Find::set_replacement(std::string_view text) {
    replacement_ = std::string(text);
}

void Find::recount() {
    hits_.clear();
    matches_ = 0;
    if (query_.empty()) {
        cursor_ = 0;
        return;
    }
    const std::string& text = doc_.source();
    std::size_t at = text.find(query_);
    while (at != std::string::npos) {
        hits_.push_back(at);
        ++matches_;
        at = text.find(query_, at + query_.size());
    }
    if (cursor_ >= hits_.size()) {
        cursor_ = 0;
    }
}

void Find::report() const {
    page_.set(dom::Id::find_count, "text",
              matches_ == 0 ? std::string("0 处")
                            : std::to_string(ordinal()) + " / " + std::to_string(matches_));
}

bool Find::select_cursor() {
    if (hits_.empty()) {
        return false;
    }
    const std::size_t at = hits_[cursor_];
    editor_.set_selection(at, at + query_.size());
    report();
    return true;
}

bool Find::next() {
    if (hits_.empty()) {
        return false;
    }
    cursor_ = (cursor_ + 1) % hits_.size();
    return select_cursor();
}

bool Find::previous() {
    if (hits_.empty()) {
        return false;
    }
    cursor_ = (cursor_ + hits_.size() - 1) % hits_.size();
    return select_cursor();
}

bool Find::replace_one() {
    if (hits_.empty()) {
        return false;
    }
    std::string text = doc_.source();
    const std::size_t at = hits_[cursor_];
    text.replace(at, query_.size(), replacement_);
    doc_.set_source(text, true);
    page_.set(editor_.surface(), "value", text);
    recount();
    report();
    return true;
}

int Find::replace_all() {
    if (query_.empty()) {
        return 0;
    }
    std::string text = doc_.source();
    int replaced = 0;
    std::size_t at = text.find(query_);
    while (at != std::string::npos) {
        text.replace(at, query_.size(), replacement_);
        at = text.find(query_, at + replacement_.size());
        ++replaced;
    }
    if (replaced > 0) {
        doc_.set_source(text, true);
        page_.set(editor_.surface(), "value", text);
    }
    recount();
    report();
    return replaced;
}

}  // namespace zmd
