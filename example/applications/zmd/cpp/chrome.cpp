#include "chrome.h"

#include <string>

#include "dom.h"

namespace zmd {
namespace {

/// 1204 -> "1,204": the status row groups thousands, like the prototype.
std::string grouped(int value) {
    std::string digits = std::to_string(value);
    std::string out;
    int count = 0;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        if (count == 3) {
            out.push_back(',');
            count = 0;
        }
        out.push_back(*it);
        ++count;
    }
    std::string flipped(out.rbegin(), out.rend());
    return flipped;
}

constexpr int kVeilMs = 1200;

}  // namespace

Chrome::Chrome(zui::Document& page) : page_(page) {}

Chrome::~Chrome() {
    // The veil timer calls back into this object. Drop it before the session goes away.
    if (veil_ != 0) {
        page_.clear_timer(veil_);
        veil_ = 0;
    }
}

void Chrome::set_title(std::string_view title) {
    page_.set(dom::Id::title, "text", title);
}

void Chrome::set_counts(int words, int lines) {
    page_.set(dom::Id::counts, "text",
              grouped(words) + " 字 · " + grouped(lines) + " 行");
}

void Chrome::set_save_state(Document::Save state) {
    if (state == shown_) {
        return;
    }
    shown_ = state;
    // The dot's state is an attribute the sheet selects on
    // (`#md-editor-mock .mdw-save[data-state="typing"]`), so it is a `data-` write,
    // not a class. The prototype's js does `setAttribute("data-state", …)`.
    std::string_view value = dom::SaveState::saved;
    std::string_view label = "已保存";
    switch (state) {
        case Document::Save::Typing:
            value = dom::SaveState::typing;
            label = "正在保存";
            break;
        case Document::Save::Error:
            value = dom::SaveState::error;
            label = "保存失败";
            break;
        case Document::Save::Saved:
        default:
            value = dom::SaveState::saved;
            label = "已保存";
            break;
    }
    page_.set(dom::Id::save, "data-state", value);
    // `aria-label` is the same write path (`Document::set` handles attributes), and the
    // prototype's markEditing() keeps the two in step.
    page_.set(dom::Id::save, "aria-label", label);
}

void Chrome::pulse() {
    set_save_state(Document::Save::Typing);
    if (!veiled_) {
        page_.add_class(dom::Id::app, dom::Class::is_typing);
        veiled_ = true;
    }
    if (veil_) {
        page_.clear_timer(veil_);
    }
    veil_ = page_.set_timeout(
        [this] {
            page_.remove_class(dom::Id::app, dom::Class::is_typing);
            veiled_ = false;
            veil_ = 0;
            // 保存失败 is the document's word, not the pulse's. The settle used to answer
            // 已保存 for every keystroke, so a failed write was reported safe 1.2s after it
            // happened. Only the next write (or its failure) may leave the error state.
            if (shown_ != Document::Save::Error) {
                set_save_state(Document::Save::Saved);
            }
        },
        kVeilMs);
}

void Chrome::reset() {
    if (veil_ != 0) {
        page_.clear_timer(veil_);
        veil_ = 0;
    }
    veiled_ = false;
    page_.remove_class(dom::Id::app, dom::Class::is_typing);
}

void Chrome::set_blurred(bool blurred) {
    if (blurred) {
        page_.add_class(dom::Id::app, dom::Class::is_blurred);
    } else {
        page_.remove_class(dom::Id::app, dom::Class::is_blurred);
    }
}

}  // namespace zmd
