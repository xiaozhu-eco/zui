#include "input/detail.h"
#include "input/interaction.h"

#include "html/document.h"
#include "html/selection.h"

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

namespace {

bool flatten_sel_span(html::Node* host, const html::SelectionRange& sel, std::size_t& from,
                      std::size_t& to) {
    if (host == nullptr) {
        return false;
    }
    if (sel.node == host && sel.end > sel.start) {
        from = sel.start;
        to = sel.end;
        return from < to;
    }
    const html::BoundaryPoint a = html::selection_start_point(sel);
    const html::BoundaryPoint b = html::selection_end_point(sel);
    if (a.node == nullptr || b.node == nullptr) {
        return false;
    }
    from = html::point_to_flatten(host, a);
    to = html::point_to_flatten(host, b);
    return from < to;
}

void ce_refresh(html::Node* host, std::string& s, std::size_t& caret) {
    html::refresh_edit_value(host);
    s = host->value;
    if (caret > s.size()) {
        caret = s.size();
    }
}

}  // namespace

bool Interaction::edit_erase_selection(html::Node* focused, std::string& s, std::size_t& caret) {
    if (host_.document == nullptr) {
        return false;
    }
    focused->text = focused->value;
    const html::SelectionRange sel = host_.document->selection;
    std::size_t from = 0;
    std::size_t to = 0;
    if (is_content_editable(focused)) {
        if (!flatten_sel_span(focused, sel, from, to)) {
            return false;
        }
        html::delete_flatten_range(focused, from, to);
        caret = from;
        ce_refresh(focused, s, caret);
        commit_edit_selection(focused, caret, caret);
        return true;
    }
    if (!(sel.node == focused && !html::selection_empty(sel) && sel.end <= focused->text.size())) {
        return false;
    }
    s.erase(sel.start, sel.end - sel.start);
    if (focused->format.size() >= sel.end) {
        focused->format.erase(sel.start, sel.end - sel.start);
    }
    caret = sel.start;
    focused->text = s;
    focused->value = s;
    commit_edit_selection(focused, caret, caret);
    return true;
}

bool Interaction::edit_splice(html::Node* focused, std::string& s, std::size_t& caret,
                              std::size_t start, std::size_t end, std::string_view next) {
        if (start > s.size()) {
            start = s.size();
        }
        if (end > s.size()) {
            end = s.size();
        }
        if (end < start) {
            end = start;
        }
        const bool same =
            (end - start) == next.size() &&
            (next.empty() || s.compare(start, end - start, next.data(), next.size()) == 0);
        caret = start + next.size();
        if (text_.edit_.composing.has_value()) {
            text_.edit_.composing->start = start;
            text_.edit_.composing->end = caret;
        }
        if (same) {
            return false;
        }
        if (is_content_editable(focused)) {
            if (start < end) {
                html::delete_flatten_range(focused, start, end);
            }
            if (!next.empty()) {
                html::insert_text_at(focused, start, next);
            }
            caret = start + next.size();
            ce_refresh(focused, s, caret);
            if (text_.edit_.composing.has_value()) {
                text_.edit_.composing->start = start;
                text_.edit_.composing->end = caret;
            }
            return true;
        }
        const char mark = start > 0 && start <= focused->format.size() ? focused->format[start - 1]
                                                                        : '\0';
        s.erase(start, end - start);
        if (focused->format.size() >= end) {
            focused->format.erase(start, end - start);
        } else if (focused->format.size() > start) {
            focused->format.resize(start);
        }
        s.insert(start, next.data(), next.size());
        if (focused->format.size() < start) {
            focused->format.resize(start, '\0');
        }
        focused->format.insert(start, next.size(), mark);
        return true;
}

bool Interaction::apply_edit_delete(html::Node* focused, const EditInput& input, std::string& s,
                                    std::size_t& caret) {
    bool changed = false;
    if (input.backspace != 0 || input.del != 0 || !input.text.empty()) {
        text_.preferred_x_ = std::numeric_limits<float>::quiet_NaN();
        text_.edit_.affinity = layout::CaretAffinity::Downstream;
    }
    for (int n = 0; n < input.backspace; ++n) {
        if (edit_erase_selection(focused, s, caret)) {
            changed = true;
            continue;
        }
        if (caret == 0) {
            break;
        }
        const std::size_t previous = text::grapheme_prev(s, caret);
        if (is_content_editable(focused)) {
            html::delete_flatten_range(focused, previous, caret);
            caret = previous;
            ce_refresh(focused, s, caret);
        } else {
            s.erase(previous, caret - previous);
            if (focused->format.size() >= caret) {
                focused->format.erase(previous, caret - previous);
            }
            caret = previous;
        }
        changed = true;
    }
    for (int n = 0; n < input.del; ++n) {
        if (edit_erase_selection(focused, s, caret)) {
            changed = true;
            continue;
        }
        if (caret >= s.size()) {
            break;
        }
        const std::size_t next = text::grapheme_next(s, caret);
        if (is_content_editable(focused)) {
            html::delete_flatten_range(focused, caret, next);
            ce_refresh(focused, s, caret);
        } else {
            s.erase(caret, next - caret);
            if (focused->format.size() >= next) {
                focused->format.erase(caret, next - caret);
            }
        }
        changed = true;
    }
    return changed;
}

void Interaction::toggle_inline_mark(html::Node* node, int bit) {
    if (!is_content_editable(node) || bit == 0 || host_.document == nullptr) {
        return;
    }
    seed_content_editable(node);
    const html::SelectionRange sel = host_.document->selection;
    std::size_t from = 0;
    std::size_t to = node->value.size();
    if (sel.node == node && !html::selection_empty(sel) && sel.end <= node->value.size()) {
        from = sel.start;
        to = sel.end;
    }
    if (from >= to) {
        return;
    }
    const char* tag = (bit & 1) != 0 ? "b" : "i";
    html::wrap_flatten_range(node, from, to, tag);
    html::refresh_edit_value(node);
    text_.edit_.text = node->value;
    if (host_.layout != nullptr) {
        host_.layout->mark_needs_layout(node);
    }
    if (host_.mark_geometry) {
        host_.mark_geometry();
    }
}

bool insert_line_break(html::Node& node) {
    if (node.disabled || (node.tag != "textarea" && !is_content_editable(&node))) {
        return false;
    }
    std::size_t caret = static_cast<std::size_t>(std::max(node.caret, 0));
    if (is_content_editable(&node)) {
        html::refresh_edit_value(&node);
        if (caret > node.value.size()) {
            caret = node.value.size();
        }
        html::insert_text_at(&node, caret, "\n");
        html::refresh_edit_value(&node);
        node.caret = static_cast<int>(caret + 1);
        return true;
    }
    if (caret > node.value.size()) {
        caret = node.value.size();
    }
    node.value.insert(caret, 1, '\n');
    node.caret = static_cast<int>(caret + 1);
    node.text = node.value;
    return true;
}

}  // namespace input
}  // namespace zui
