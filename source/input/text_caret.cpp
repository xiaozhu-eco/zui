#include "input/detail.h"
#include "input/interaction.h"

#include "html/document.h"

#include "layout/caret/caret_service.h"
#include "text/grapheme_break.h"
#include "layout/caret/caret_types.h"
#include "layout/point.h"
#include "layout/scrollbar_h.h"
#include "layout/scrollbar_metrics.h"
#include "layout/scrollbar_v.h"
#include "trace/trace.h"

#include <algorithm>
#include <limits>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zui {
namespace input {

using namespace input_detail;

namespace input_detail {
std::size_t utf8_next(const std::string& text, std::size_t index) {
    if (index >= text.size()) {
        return text.size();
    }
    ++index;
    while (index < text.size() && (static_cast<unsigned char>(text[index]) & 0xc0) == 0x80) {
        ++index;
    }
    return index;
}

std::size_t utf8_prev(const std::string& text, std::size_t index) {
    if (index == 0) {
        return 0;
    }
    --index;
    while (index > 0 && (static_cast<unsigned char>(text[index]) & 0xc0) == 0x80) {
        --index;
    }
    return index;
}

std::size_t line_begin(const std::string& text, std::size_t index) {
    index = std::min(index, text.size());
    while (index > 0 && text[index - 1] != '\n') {
        --index;
    }
    return index;
}

std::size_t line_end(const std::string& text, std::size_t index) {
    index = std::min(index, text.size());
    while (index < text.size() && text[index] != '\n') {
        ++index;
    }
    return index;
}

bool ascii_word_char(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

bool word_char_at(const std::string& text, std::size_t at) {
    if (at >= text.size()) {
        return false;
    }
    const unsigned char lead = static_cast<unsigned char>(text[at]);
    if (lead < 0x80) {
        return ascii_word_char(lead);
    }
    unsigned int code = 0;
    std::size_t need = 0;
    if ((lead & 0xE0) == 0xC0) {
        code = lead & 0x1Fu;
        need = 1;
    } else if ((lead & 0xF0) == 0xE0) {
        code = lead & 0x0Fu;
        need = 2;
    } else if ((lead & 0xF8) == 0xF0) {
        code = lead & 0x07u;
        need = 3;
    } else {
        return true;  // a stray continuation byte belongs to the character before it
    }
    for (std::size_t i = 0; i < need && at + 1 + i < text.size(); ++i) {
        code = (code << 6) | (static_cast<unsigned char>(text[at + 1 + i]) & 0x3Fu);
    }
    // The marks a word breaks at, and the ones it does not. A browser segments CJK by word: a run
    // of ideographs is one word for a double click and for Ctrl+Arrow (ICU's dictionary may split
    // it further - `你好` and `世界` - which this coarse rule does not attempt), while its
    // punctuation (，。、！？（）【】and the fullwidth forms) separates like any other mark. Latin-1
    // punctuation (¡ ¿ « ») separates too; fullwidth *letters* (Ａ-Ｚ) stay word characters.
    if (code >= 0x2000 && code <= 0x206F) {
        return false;  // general punctuation: dashes, curly quotes, ellipsis
    }
    if (code >= 0x3000 && code <= 0x303F) {
        return false;  // CJK symbols and punctuation
    }
    if (code >= 0xFE30 && code <= 0xFE4F) {
        return false;  // CJK compatibility forms
    }
    if (code >= 0xFF01 && code <= 0xFF0F) {
        return false;
    }
    if (code >= 0xFF1A && code <= 0xFF20) {
        return false;
    }
    if (code >= 0xFF3B && code <= 0xFF40) {
        return false;
    }
    if (code >= 0xFF5B && code <= 0xFF65) {
        return false;
    }
    if (code >= 0x00A0 && code <= 0x00BF) {
        return false;
    }
    return true;
}

/// Coarse word bounds around @p index (PR-5b). Non-word positions select one UTF-8 char.
void word_range(const std::string& text, std::size_t index, std::size_t& out_start,
                std::size_t& out_end) {
    if (text.empty()) {
        out_start = 0;
        out_end = 0;
        return;
    }
    if (index > text.size()) {
        index = text.size();
    }
    if (index == text.size()) {
        index = utf8_prev(text, index);
    }
    while (index > 0 && (static_cast<unsigned char>(text[index]) & 0xC0) == 0x80) {
        --index;
    }
    if (index < text.size() && word_char_at(text, index)) {
        std::size_t start = index;
        while (start > 0) {
            const std::size_t prev = utf8_prev(text, start);
            if (!word_char_at(text, prev)) {
                break;
            }
            start = prev;
        }
        std::size_t end = utf8_next(text, index);
        while (end < text.size() && word_char_at(text, end)) {
            end = utf8_next(text, end);
        }
        out_start = start;
        out_end = end;
        return;
    }
    if (index >= text.size()) {
        out_start = text.size();
        out_end = text.size();
        return;
    }
    out_start = index;
    out_end = utf8_next(text, index);
}
}  // namespace input_detail

std::size_t Interaction::hit_edit_index(html::Node* node, float x, float y) {
    if (node == nullptr || host_.layout == nullptr) {
        return 0;
    }
    layout::Box* box = host_.layout->principal_box(node);
    const std::size_t clamped =
        std::min(static_cast<std::size_t>(std::max(node->caret, 0)), node->value.size());
    if (box == nullptr || box->style_ptr == nullptr) {
        return clamped;
    }
    const layout::EditMapping mapping = layout::make_edit_mapping(*box, node->value);
    const layout::CaretHit hit = layout::hit_test(*box, mapping, x, y);
    if (!hit.ok) {
        // Fail closed: keep the existing caret. No content-edge measure fallback.
        return clamped;
    }
    text_.edit_.affinity = hit.affinity;
    return hit.offset;
}

CaretRect caret_rect(const layout::Box& box, layout::FreeTypeTextMetrics& metrics,
                     const std::string& value, std::size_t caret, layout::CaretAffinity affinity) {
    (void)metrics;
    caret = std::min(caret, value.size());
    CaretRect out;
    const layout::EditMapping mapping = layout::make_edit_mapping(box, value);
    const layout::CaretRect rect = layout::get_rect(box, mapping, caret, affinity);
    if (!rect.ok) {
        // Fail closed: zero-height rect (frame skips a meaningful caret paint).
        out.x = 0.f;
        out.y = 0.f;
        out.height = 0.f;
        return out;
    }
    out.x = rect.x;
    out.y = rect.y;
    out.width = rect.width > 0.f ? rect.width : 1.f;
    out.height = rect.height;
    return out;
}

std::size_t Interaction::edit_line_target(html::Node* node, std::size_t caret, int step) {
    if (node == nullptr || host_.layout == nullptr || step == 0) {
        return std::string::npos;
    }
    layout::Box* box = host_.layout->principal_box(node);
    if (box == nullptr || box->style_ptr == nullptr) {
        return std::string::npos;
    }
    const std::string& text = node->value;
    caret = std::min(caret, text.size());
    const layout::EditMapping mapping = layout::make_edit_mapping(*box, text);
    if (mapping.spans.empty()) {
        return std::string::npos;
    }
    if (!std::isfinite(text_.preferred_x_)) {
        const layout::CaretRect here =
            layout::get_rect(*box, mapping, caret, text_.edit_.affinity);
        if (here.ok) {
            text_.preferred_x_ = here.x;
        }
    }
    layout::CaretAffinity landed = text_.edit_.affinity;
    const std::size_t at = layout::line_step(*box, mapping, caret, text_.edit_.affinity, step,
                                             text_.preferred_x_, &landed);
    text_.edit_.affinity = landed;
    return at;
}

std::size_t Interaction::edit_edge_target(html::Node* node, std::size_t caret, bool to_end,
                                         layout::CaretAffinity* landed) const {
    caret = node != nullptr ? std::min(caret, node->value.size()) : 0;
    if (node == nullptr) {
        return caret;
    }
    if (host_.layout != nullptr) {
        if (layout::Box* box = host_.layout->principal_box(node)) {
            const layout::EditMapping mapping = layout::make_edit_mapping(*box, node->value);
            if (!mapping.spans.empty()) {
                return layout::line_edge(*box, mapping, caret, to_end, text_.edit_.affinity, landed);
            }
        }
    }
    if (landed != nullptr) {
        *landed = layout::CaretAffinity::Downstream;
    }
    // No line table: the `\n` bounds of the value.
    return to_end ? line_end(node->value, caret) : line_begin(node->value, caret);
}

void Interaction::reset_caret_blink() {
    text_.caret_blink_on_ = true;
    text_.caret_blink_epoch_ = std::numeric_limits<double>::quiet_NaN();
}

bool Interaction::tick_caret_blink(double frame_t) {
    html::Node* focused = host_.focused != nullptr ? *host_.focused : nullptr;
    if (focused == nullptr || !is_editable(focused)) {
        return false;
    }
    constexpr double kHalf = 0.53;
    if (!std::isfinite(text_.caret_blink_epoch_)) {
        text_.caret_blink_epoch_ = frame_t;
        text_.caret_blink_on_ = true;
        return false;
    }
    const double elapsed = frame_t - text_.caret_blink_epoch_;
    if (elapsed < 0.0) {
        text_.caret_blink_epoch_ = frame_t;
        return false;
    }
    const bool on = (static_cast<std::int64_t>(elapsed / kHalf) % 2) == 0;
    if (on == text_.caret_blink_on_) {
        return false;
    }
    text_.caret_blink_on_ = on;
    // Blink toggles visibility at the same caret clip (P5.4): damage only Clip(caret).
    if (host_.layout != nullptr && host_.metrics != nullptr) {
        layout::Box* box = host_.layout->principal_box(focused);
        if (box != nullptr) {
            const std::size_t caret =
                std::min(static_cast<std::size_t>(std::max(focused->caret, 0)), focused->value.size());
            const CaretRect bar =
                caret_rect(*box, *host_.metrics, focused->value, caret, text_.edit_.affinity);
            if (bar.height > 0.f) {
                html::note_paint_damage_rect(
                    focused, html::PaintRect{bar.x, bar.y, bar.width, bar.height});
            }
        }
    }
    text_.caret_visual_dirty_ = true;
    return true;
}

bool Interaction::take_caret_visual_dirty() {
    const bool dirty = text_.caret_visual_dirty_;
    text_.caret_visual_dirty_ = false;
    return dirty;
}

void Interaction::note_caret_visual(html::Node* node) {
    reset_caret_blink();
    if (node == nullptr || host_.layout == nullptr || host_.metrics == nullptr) {
        return;
    }
    layout::Box* box = host_.layout->principal_box(node);
    if (box == nullptr) {
        return;
    }
    const std::size_t caret =
        std::min(static_cast<std::size_t>(std::max(node->caret, 0)), node->value.size());
    const CaretRect bar = caret_rect(*box, *host_.metrics, node->value, caret, text_.edit_.affinity);
    if (bar.height <= 0.f) {
        return;
    }
    html::PaintRect next{bar.x, bar.y, bar.width, bar.height};
    html::PaintRect damage = next;
    if (text_.caret_damage_valid_) {
        const float x0 = std::min(text_.caret_damage_x_, next.x);
        const float y0 = std::min(text_.caret_damage_y_, next.y);
        const float x1 = std::max(text_.caret_damage_x_ + text_.caret_damage_w_, next.x + next.w);
        const float y1 = std::max(text_.caret_damage_y_ + text_.caret_damage_h_, next.y + next.h);
        damage = html::PaintRect{x0, y0, x1 - x0, y1 - y0};
    }
    html::note_paint_damage_rect(node, damage);
    text_.caret_damage_x_ = next.x;
    text_.caret_damage_y_ = next.y;
    text_.caret_damage_w_ = next.w;
    text_.caret_damage_h_ = next.h;
    text_.caret_damage_valid_ = true;
    text_.caret_visual_dirty_ = true;
}

bool Interaction::apply_edit_move(html::Node* focused, const EditInput& input, const std::string& s,
                                  std::size_t& caret) {
    bool caret_moved = false;
    if (input.left != 0 || input.right != 0 || input.home || input.end) {
        text_.preferred_x_ = std::numeric_limits<float>::quiet_NaN();
    }
    for (int n = 0; n < input.left; ++n) {
        if (input.word) {
            // Ctrl+Left: the start of the word before the caret - over whitespace first, so the
            // run of spaces between two words belongs to neither.
            std::size_t at = caret;
            while (at > 0 && !word_char_at(s, utf8_prev(s, at))) {
                at = utf8_prev(s, at);
            }
            while (at > 0 && word_char_at(s, utf8_prev(s, at))) {
                at = utf8_prev(s, at);
            }
            caret = at;
        } else {
            caret = text::grapheme_prev(s, caret);
        }
        caret_moved = true;
    }
    for (int n = 0; n < input.right; ++n) {
        if (input.word) {
            // Ctrl+Right: the end of the word the caret sits in, or of the next one after the
            // spaces it sits on.
            std::size_t at = caret;
            while (at < s.size() && !word_char_at(s, at)) {
                at = utf8_next(s, at);
            }
            while (at < s.size() && word_char_at(s, at)) {
                at = utf8_next(s, at);
            }
            caret = at;
        } else {
            caret = text::grapheme_next(s, caret);
        }
        caret_moved = true;
    }
    // Home / End and the vertical moves read the laid-out lines: a soft wrap is a line for them,
    // as much as a `\n` is. Ctrl+Home / Ctrl+End ask for the ends of the whole value instead.
    if (input.home) {
        if (input.word) {
            caret = 0;
            text_.edit_.affinity = layout::CaretAffinity::Downstream;
        } else {
            layout::CaretAffinity landed = text_.edit_.affinity;
            caret = edit_edge_target(focused, caret, false, &landed);
            text_.edit_.affinity = landed;
        }
        caret_moved = true;
    }
    if (input.end) {
        if (input.word) {
            caret = s.size();
            text_.edit_.affinity = layout::CaretAffinity::Downstream;
        } else {
            layout::CaretAffinity landed = text_.edit_.affinity;
            caret = edit_edge_target(focused, caret, true, &landed);
            text_.edit_.affinity = landed;
        }
        caret_moved = true;
    }
    for (int n = 0; n < input.up; ++n) {
        const std::size_t at = edit_line_target(focused, caret, -1);
        if (at == std::string::npos) {
            break;
        }
        caret = at;
        caret_moved = true;
    }
    for (int n = 0; n < input.down; ++n) {
        const std::size_t at = edit_line_target(focused, caret, 1);
        if (at == std::string::npos) {
            break;
        }
        caret = at;
        caret_moved = true;
    }
    // PageUp / PageDown. A control that scrolls its own text moves the caret a page of laid-out
    // lines and the frame's reveal follows it; one that shows all of its text at once (a textarea
    // that grows with its value) leaves the caret and scrolls the page it sits in, which is what
    // the same key does with nothing focused.
    if ((input.page_up != 0 || input.page_down != 0) && host_.layout != nullptr) {
        layout::Box* box = host_.layout->principal_box(focused);
        const float line_h = box != nullptr && !box->geo.runs.empty()
                                 ? std::max(1.f, box->geo.runs.front().line_block)
                                 : 0.f;
        float port_h = 0.f;
        bool own_scroll = false;
        if (box != nullptr) {
            const ScrollRange own = scroll_range(*box);
            own_scroll = own.max_y > 0.01f;
            if (own_scroll) {
                port_h = client_rect(*box, host_.scrollbar_thickness).h;
            }
        }
        // The page the caret sits in: the nearest ancestor that actually scrolls. A textarea's own
        // `overflow: auto` makes it the first candidate, but a control that grows with its value
        // has no range, so `nearest_scrollable` (which wants a range) is the right walk here.
        layout::Box* scroller =
            box != nullptr && !own_scroll
                ? nearest_scrollable(box->parent, host_.scrollbar_thickness)
                : nullptr;
        float view_w = -1.f;
        float view_h = -1.f;
        if (!own_scroll && scroller == nullptr && host_.layout->root() != nullptr) {
            document_scroll_view(*host_.layout, *host_.layout->root(), host_.scrollbar_thickness,
                                 view_w, view_h);
            const ScrollRange root_range = scroll_range(*host_.layout->root(), view_w, view_h);
            if (root_range.max_y > 0.01f || root_range.max_x > root_range.min_x + 0.01f) {
                scroller = host_.layout->root();
            } else {
                view_w = -1.f;
                view_h = -1.f;
            }
        }
        if (!own_scroll && scroller != nullptr && scroller->node != nullptr) {
            port_h = view_h >= 0.f ? view_h
                                   : client_rect(*scroller, host_.scrollbar_thickness).h;
        }
        const int lines = line_h > 0.f && port_h > 0.f
                              ? static_cast<int>(port_h / line_h)
                              : 0;
        if (own_scroll && lines > 0) {
            for (int n = 0; n < input.page_down; ++n) {
                const std::size_t at = edit_line_target(focused, caret, lines);
                if (at == std::string::npos) {
                    break;
                }
                caret = at;
                caret_moved = true;
            }
            for (int n = 0; n < input.page_up; ++n) {
                const std::size_t at = edit_line_target(focused, caret, -lines);
                if (at == std::string::npos) {
                    break;
                }
                caret = at;
                caret_moved = true;
            }
        } else if (scroller != nullptr && scroller->node != nullptr && port_h > 0.f) {
            const int steps = input.page_down != 0 ? input.page_down : -input.page_up;
            const float dy = static_cast<float>(steps) * std::max(1.f, port_h - line_h);
            if (apply_scroll(*scroller, scroller->node->scroll_x,
                             scroller->node->scroll_y + dy, view_w, view_h)) {
                host_.layout->sync_world();
                if (host_.document != nullptr) {
                    settle_content_visibility(*host_.layout, *host_.document,
                                              host_.scrollbar_thickness);
                }
            }
        }
    }
    return caret_moved;
}

}  // namespace input
}  // namespace zui
