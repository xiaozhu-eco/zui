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

void Interaction::commit_editable_change(html::Node* previous) {
    if (previous == nullptr || !text_.edit_dirty_ || !is_editable(previous)) {
        text_.edit_dirty_ = false;
        return;
    }
    text_.edit_dirty_ = false;
    fire(previous, "change");
}

void Interaction::commit_edit_selection(html::Node* node, std::size_t anchor, std::size_t focus) {
    if (node == nullptr || host_.document == nullptr) {
        return;
    }
    // Editable indices live on the same UTF-8 sequence as value; mirror before text.size() clamp.
    if (is_editable(node)) {
        node->text = node->value;
        // IME/composition scratch tracks flatten offsets; document Selection is the caret/highlight.
        text_.edit_.text = node->value;
        text_.edit_.anchor = anchor;
        text_.edit_.focus = focus;
        text_.edit_.clamp_selection();
        anchor = text_.edit_.anchor;
        focus = text_.edit_.focus;
    }
    text_.select_node_ = node;
    text_.select_anchor_ = anchor;
    text_.select_focus_ = focus;
    text_.selecting_tree_ = false;
    node->select_anchor = anchor;
    const std::size_t a = std::min(anchor, focus);
    const std::size_t b = std::max(anchor, focus);
    const html::SelectionRange previous = host_.document->selection;
    html::set_selection(*host_.document, node, a, b);
    node->caret = static_cast<int>(focus);
    // `select` fires when the user changes an editable selection to a non-collapsed range.
    if (a != b && (previous.node != node || previous.start != a || previous.end != b)) {
        fire(node, "select");
    }
    if (is_editable(node) &&
        (previous.node != node || previous.start != a || previous.end != b)) {
        note_caret_visual(node);
    }
}

void Interaction::commit_boundary_selection(html::BoundaryPoint anchor, html::BoundaryPoint focus) {
    if (host_.document == nullptr) {
        return;
    }
    text_.selecting_tree_ = true;
    text_.range_anchor_point_ = anchor;
    html::Node* host = anchor.node != nullptr ? anchor.node : focus.node;
    if (host != nullptr && host->tag == "#text" && host->parent != nullptr) {
        host = host->parent;
    }
    text_.select_node_ = host;
    html::set_selection(*host_.document, anchor, focus);
    if (focus.node != nullptr) {
        focus.node->caret = static_cast<int>(focus.offset);
    }
}

bool Interaction::move_document_caret(std::string_view key, bool extend) {
    if (host_.document == nullptr) {
        return false;
    }
    const html::SelectionRange& sel = host_.document->selection;
    html::BoundaryPoint focus = sel.focus.node != nullptr
                                    ? sel.focus
                                    : html::BoundaryPoint{sel.node, sel.end};
    html::BoundaryPoint anchor = sel.anchor.node != nullptr
                                     ? sel.anchor
                                     : html::BoundaryPoint{sel.node, sel.start};
    if (focus.node == nullptr) {
        return false;
    }
    const std::string& text =
        html::is_text_entry(focus.node) ? focus.node->value : focus.node->text;
    std::size_t off = std::min(focus.offset, text.size());
    if (key == "ArrowLeft") {
        if (off == 0) {
            return false;
        }
        off = text::grapheme_prev(text, off);
    } else if (key == "ArrowRight") {
        if (off >= text.size()) {
            return false;
        }
        off = text::grapheme_next(text, off);
    } else {
        return false;
    }
    focus.offset = off;
    if (extend) {
        html::set_selection(*host_.document, anchor, focus);
    } else {
        html::set_selection(*host_.document, focus, focus);
    }
    return true;
}

}  // namespace input
}  // namespace zui
