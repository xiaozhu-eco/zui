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

bool Interaction::apply_editing(const EditInput& input) {
    html::Node* focused = host_.focused != nullptr ? *host_.focused : nullptr;
    if (focused == nullptr || !is_editable(focused)) {
        return false;
    }
    seed_content_editable(focused);
    bool changed = false;
    std::string& s = focused->value;
    if (is_content_editable(focused)) {
        html::refresh_edit_value(focused);
    }
    std::size_t caret = static_cast<std::size_t>(std::max(focused->caret, 0));
    if (caret > s.size()) {
        caret = s.size();
    }
    // Form-control format bytes stay a local projection. CE authority is the tree.
    if (is_content_editable(focused)) {
        text_.edit_.text = s;
    } else if (focused->format.size() < s.size()) {
        focused->format.resize(s.size(), '\0');
    }
    // Composition state lives only in text_.edit_.composing (spec 5.3).
    std::string composition_now;
    if (text_.edit_.composing.has_value()) {
        CompositionRange& range = *text_.edit_.composing;
        const std::size_t cs = std::min(range.start, s.size());
        const std::size_t ce = std::min(std::max(range.end, cs), s.size());
        composition_now.assign(s, cs, ce - cs);
        range.start = cs;
        range.end = ce;
        range.preview_text = composition_now;
    }
    // A preedit replaces the open range. The same string on a later frame is not a new
    // insert. Leaving the composition with `text` replaces that range once; leaving it
    // with an empty `text` drops the preedit. Typed text is inserted only when no range
    // is open.
    const bool composition_differs =
        input.composing &&
        (!text_.edit_.composing.has_value() || composition_now != input.composition);
    const bool composition_close = !input.composing && text_.edit_.composing.has_value();
    const bool close_changes = composition_close && input.text != composition_now;
    const bool plain_text =
        !input.composing && !text_.edit_.composing.has_value() && !input.text.empty();
    const bool will_edit = composition_differs || close_changes || plain_text ||
                           (!input.composing && (input.backspace > 0 || input.del > 0)) ||
                           (input.enter && !input.enter_prevented && !input.composing);
    if (will_edit && fire(focused, "beforeinput")) {
        return false;
    }
    if (apply_edit_ime(focused, input, s, caret)) {
        changed = true;
    }
    bool caret_moved = false;
    // A shift+move grows a selection: the caret moves and the other end stays put. That anchor is
    // the far end of a range already on this control - a drag, an earlier shift move - or the
    // caret itself when nothing is selected.
    std::size_t extend_anchor = caret;
    if (input.extend && !input.composing && host_.document != nullptr) {
        const html::SelectionRange sel = host_.document->selection;
        if (sel.node == focused && sel.end > sel.start && sel.end <= s.size()) {
            extend_anchor = sel.start == caret ? sel.end : sel.start;
        }
    }
    if (!input.composing) {
        if (apply_edit_delete(focused, input, s, caret)) {
            changed = true;
        }
        caret_moved = apply_edit_move(focused, input, s, caret);
    }
    focused->caret = static_cast<int>(caret);
    focused->text = s;
    focused->value = s;
    if (input.enter && !input.enter_prevented && !input.composing && insert_line_break(*focused)) {
        caret = static_cast<std::size_t>(std::max(focused->caret, 0));
        s = focused->value;
        text_.edit_.text = s;
        changed = true;
    }
    if (changed || caret_moved) {
        // The caret moved. The frame's `reveal_caret()` brings it back inside its scrollport once
        // this edit's layout has run - the value just grew and the boxes the input phase reads are
        // the previous frame's, so doing it here would scroll to where the caret used to be.
        text_.reveal_caret_ = focused;
        focused->text = s;
        focused->value = s;
        text_.edit_.text = s;
        text_.edit_.focus = caret;
        if (!(input.extend && !input.composing && extend_anchor != caret)) {
            text_.edit_.anchor = caret;
        }
        if (host_.document != nullptr) {
            if (input.extend && !input.composing && extend_anchor != caret) {
                // The same publish a drag uses, so the control paints the range and typing
                // replaces it exactly as it does for a mouse selection.
                commit_edit_selection(focused, extend_anchor, caret);
                caret = static_cast<std::size_t>(std::max(focused->caret, 0));
            } else {
                commit_edit_selection(focused, caret, caret);
            }
        }
        // Clip(union(prev,next)) + frame_visual without layout dirty (P5.4).
        note_caret_visual(focused);
    }
    if (changed && is_content_editable(focused)) {
        html::refresh_edit_value(focused);
        s = focused->value;
        focused->text = s;
        text_.edit_.text = s;
    }
    if (changed) {
        if (host_.layout != nullptr) {
            host_.layout->sync_text(focused);
            host_.layout->mark_needs_layout(focused);
        }
        if (host_.mark_geometry) {
            host_.mark_geometry();
        }
        note_restyle_roots({focused});
        fire(focused, "input");
        text_.edit_dirty_ = true;
    }
    return changed;
}

}  // namespace input
}  // namespace zui
