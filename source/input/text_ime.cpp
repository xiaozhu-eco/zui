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

void Interaction::composition_event(int phase) {
    if (host_.document == nullptr || host_.document->root == nullptr || phase < 1 || phase > 3) {
        return;
    }
    html::Node* focused = host_.focused != nullptr ? *host_.focused : nullptr;
    html::Node* target =
        focused != nullptr ? focused : html::find_tag(host_.document->root.get(), "body");
    if (target == nullptr) {
        return;
    }
    const char* kind = phase == 1 ? "compositionstart" : phase == 2 ? "compositionupdate"
                                                                    : "compositionend";
    fire(target, kind);
}

bool Interaction::apply_edit_ime(html::Node* focused, const EditInput& input, std::string& s,
                                 std::size_t& caret) {
    bool changed = false;
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

    if (input.composing) {
        if (composition_differs) {
            if (!text_.edit_.composing.has_value()) {
                edit_erase_selection(focused, s, caret);
                CompositionRange range;
                range.start = caret;
                range.end = caret;
                text_.edit_.composing = range;
            }
            CompositionRange& range = *text_.edit_.composing;
            if (edit_splice(focused, s, caret, range.start, range.end, input.composition)) {
                changed = true;
            }
            range.preview_text = input.composition;
            range.end = range.start + input.composition.size();
        }
    } else if (text_.edit_.composing.has_value()) {
        CompositionRange& range = *text_.edit_.composing;
        if (close_changes && edit_splice(focused, s, caret, range.start, range.end, input.text)) {
            changed = true;
        }
        text_.edit_.composing = std::nullopt;
    } else if (!input.text.empty()) {
        edit_erase_selection(focused, s, caret);
        if (is_content_editable(focused)) {
            html::insert_text_at(focused, caret, input.text);
            html::refresh_edit_value(focused);
            s = focused->value;
            caret += input.text.size();
            if (caret > s.size()) {
                caret = s.size();
            }
        } else {
            s.insert(caret, input.text);
            caret += input.text.size();
        }
        changed = true;
    }
    return changed;
}

}  // namespace input
}  // namespace zui
