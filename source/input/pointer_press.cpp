#include "input/detail.h"
#include "input/interaction.h"

#include "html/selection.h"
#include "layout/caret/caret_service.h"
#include "layout/paint_transform.h"
#include "layout/point.h"
#include "layout/scrollbar_h.h"
#include "layout/scrollbar_metrics.h"
#include "layout/scrollbar_v.h"
#include "layout/viewport.h"
#include "trace/trace.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
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

void Interaction::press_non_left(float x, float y, const std::string& button, int click_count,
                                 const Pointer& mods) {
    if (host_.layout == nullptr || host_.document == nullptr) {
        return;
    }
    layout::Box* hit = host_.layout->hit_test(x, y);
    html::Node* click_node = node_of(hit);
    Pointer payload = mods;
    payload.x = x;
    payload.y = y;
    payload.button = button;
    payload.count = click_count > 0 ? click_count : 1;
    if (html::Node* down = handler_owner(click_node, "pointerdown")) {
        fire(down, "pointerdown", nullptr, nullptr, nullptr, &payload);
    }
    const char* kind = button == "right" ? "contextmenu" : "auxclick";
    if (html::Node* handler = handler_owner(click_node, kind)) {
        fire(handler, kind, nullptr, nullptr, nullptr, &payload);
    }
}

void Interaction::press(float x, float y, int click_count, const Pointer& mods) {
    if (host_.layout == nullptr || host_.document == nullptr) {
        return;
    }
    text_.selecting_text_ = false;
    clear_capture(mods.pointer_id);
    layout::Box* hit = host_.layout->hit_test(x, y);
    if (press_scrollbar(hit, x, y)) {
        return;
    }

    if (host_.focused == nullptr || host_.scroll_drag == nullptr) {
        return;
    }
    html::Node* click_node = node_of(hit);
    html::Node* hit_select = nullptr;
    html::Node* select_changed = press_select_popup(click_node, hit_select);

    html::Node*& focused = *host_.focused;
    html::Node* next_focus = editable_from(click_node);
    if (next_focus == nullptr && hit_select != nullptr) {
        next_focus = hit_select;
    }
    if (next_focus == nullptr && click_node != nullptr) {
        // The innermost hit is often a control's own `<img>` / `<i>`: the press focuses the
        // control around it, and a text selection the press made elsewhere stays put.
        next_focus = focus_candidate_from(click_node);
    }
    // Pointer focus: `:focus-visible` only for text-entry controls (Selectors 4 / Chromium).
    if (focused != next_focus) {
        focus(next_focus, /*keyboard_modality=*/false);
    } else if (focused != nullptr && focused == next_focus) {
        // Same control clicked again: drop a keyboard-drawn focus ring on a button/link.
        const bool show = always_focus_visible(focused);
        if (focused->focus_visible != show) {
            focused->focus_visible = show;
            note_restyle_roots({focused});
        }
    }
    if (focused != nullptr && is_editable(focused)) {
        text_.preferred_x_ = std::numeric_limits<float>::quiet_NaN();
            const std::size_t caret = hit_edit_index(focused, x, y);
        if (click_count >= 2) {
            if (click_count >= 3) {
                // The third click takes the laid-out line under the pointer, the range the paper's
                // own third click takes (a soft wrap is a line, like a `\n`).
                const std::size_t begin = edit_edge_target(focused, caret, false);
                layout::CaretAffinity edge_aff = text_.edit_.affinity;
                const std::size_t end = edit_edge_target(focused, caret, true, &edge_aff);
                text_.edit_.affinity = edge_aff;
                commit_edit_selection(focused, begin, end);
            } else {
                std::size_t w0 = 0;
                std::size_t w1 = 0;
                word_range(focused->value, caret, w0, w1);
                commit_edit_selection(focused, w0, w1);
            }
            text_.selecting_text_ = false;
        } else if (mods.shift && host_.document != nullptr) {
            // Shift+click extends the selection: the caret moves to the click and the range's
            // other end stays where it was - the far end of the range this control already had
            // (a collapsed one is the caret). The same anchor rule `EditInput.extend` uses, so a
            // drag that follows keeps extending from it the way a browser's does.
            const html::SelectionRange& sel = host_.document->selection;
            const bool range_on_focus = sel.node == focused && sel.end <= focused->value.size();
            const std::size_t anchor =
                range_on_focus ? (sel.start == caret ? sel.end : sel.start) : caret;
            commit_edit_selection(focused, anchor, caret);
            text_.selecting_text_ = true;
        } else {
            commit_edit_selection(focused, caret, caret);
            text_.selecting_text_ = true;
        }
    } else if (next_focus == nullptr) {
        html::Node* host = click_node;
        if (host != nullptr && host->tag == "#text" && host->parent != nullptr) {
            host = host->parent;
        }
        auto is_inline_level = [](const html::Node* n) {
            if (n == nullptr) {
                return false;
            }
            if (n->tag == "#text" || n->tag == "br" || n->tag == "span" || n->tag == "b" ||
                n->tag == "i" || n->tag == "strong" || n->tag == "em" || n->tag == "a" ||
                n->tag == "code" || n->tag == "u" || n->tag == "s" || n->tag == "mark") {
                return true;
            }
            const std::string* display = n->style("display");
            return display != nullptr &&
                   (*display == "inline" || *display == "inline-block" ||
                    *display == "inline-flex" || *display == "contents");
        };
        while (host != nullptr && host->parent != nullptr && is_inline_level(host)) {
            host = host->parent;
        }
        layout::Box* text_box = nullptr;
        if (host != nullptr && host_.layout != nullptr) {
            text_box = host_.layout->principal_box(host);
        }
        if (text_box == nullptr) {
            text_box = hit;
        }
        if (handler_owner(click_node, "pointerdown") != nullptr ||
            handler_owner(click_node, "pointermove") != nullptr) {
            // The page owns the gesture. Do not start an engine selection on the hit text:
            // that range is paper innerText and fights a source map the page maintains.
            // Do not clear Document Selection either. The page projects its own range onto
            // this host, and a press must not wipe the highlight it just painted.
            text_.select_node_ = nullptr;
            text_.selecting_tree_ = false;
            text_.selecting_text_ = false;
        } else if (text_box != nullptr && host != nullptr && !html::range_edit_text(host).empty()) {
            html::BoundaryPoint p = layout::hit_point(*text_box, host, x, y);
            if (p.node == nullptr) {
                p.node = host;
            }
            const std::string edit = html::range_edit_text(host);
            const std::size_t flatten = html::point_to_flatten(host, p);
            auto commit_span = [&](std::size_t from, std::size_t to) {
                html::BoundaryPoint a;
                html::BoundaryPoint b;
                html::flatten_to_point(host, from, a);
                html::flatten_to_point(host, to, b);
                if (a.node == nullptr) {
                    a.node = host;
                    a.offset = from;
                }
                if (b.node == nullptr) {
                    b.node = host;
                    b.offset = to;
                }
                commit_boundary_selection(a, b);
            };
            if (click_count >= 3) {
                layout::CaretAffinity edge_aff = text_.edit_.affinity;
                const layout::EditMapping mapping = layout::make_edit_mapping(*text_box, edit);
                std::size_t begin = 0;
                std::size_t end = edit.size();
                if (!mapping.spans.empty()) {
                    begin = layout::line_edge(*text_box, mapping, flatten, false, text_.edit_.affinity);
                    end = layout::line_edge(*text_box, mapping, flatten, true, text_.edit_.affinity,
                                            &edge_aff);
                    text_.edit_.affinity = edge_aff;
                }
                commit_span(begin, end);
                text_.selecting_text_ = false;
            } else if (click_count >= 2) {
                std::size_t w0 = 0;
                std::size_t w1 = 0;
                word_range(edit, flatten, w0, w1);
                commit_span(w0, w1);
                text_.selecting_text_ = false;
            } else {
                html::BoundaryPoint anchor = p;
                if (mods.shift && host_.document->selection.anchor.node != nullptr) {
                    anchor = host_.document->selection.anchor;
                }
                commit_boundary_selection(anchor, p);
                text_.selecting_text_ = true;
            }
        } else {
            html::clear_selection(*host_.document);
            text_.select_node_ = nullptr;
            text_.selecting_tree_ = false;
        }
    }
    // HOST PR-8: primary press captures the press node until release (kanban/album drag prep).
    set_capture(mods.pointer_id, click_node);
    Pointer payload = mods;
    payload.x = x;
    payload.y = y;
    payload.button = "left";
    payload.count = click_count > 0 ? click_count : 1;
    // A handler may rebuild the subtree the press came from - `apply_model` re-creates every row
    // of a list - which frees the nodes this dispatch is still holding. Snapshot the structural
    // clock and give up on the rest of the press once it moves: everything below (the other
    // handlers, `change`, the file-input search) reads nodes resolved before the dispatch.
    const std::uint64_t dom_before = html::dom_stamp(host_.document);
    bool down_prevented = false;
    if (html::Node* down = handler_owner(click_node, "pointerdown")) {
        down_prevented = fire(down, "pointerdown", nullptr, nullptr, nullptr, &payload);
    }
    bool tree_moved = html::dom_stamp(host_.document) != dom_before;
    if (tree_moved) {
        clear_capture(mods.pointer_id);
        return;
    }
    // Select option commit: input then change (HTML form control order for value commits). The
    // popup is not a generic click, so it stays on the press.
    if (select_changed != nullptr) {
        fire(select_changed, "input");
        fire(select_changed, "change");
    }
    // PR-INPUT-3: the click fires on release (browser order), on the press and release targets'
    // nearest common ancestor. The press only records what the release needs.
    PressState state;
    state.target = click_node;
    state.x = x;
    state.y = y;
    state.mods = mods;
    state.click_count = click_count > 0 ? click_count : 1;
    if (mods.pointer_type != PointerType::Mouse) {
        // A finger or pen can drag the nearest scrollable ancestor instead of clicking it.
        if (layout::Box* scroller = scroller_from(hit)) {
            bool allow_x = true;
            bool allow_y = true;
            touch_action_axes(click_node, scroller->node, allow_x, allow_y);
            if (allow_x || allow_y) {
                state.scroller = scroller->node;
                state.scroll_x0 =
                    scroller->node != nullptr ? scroller->node->scroll_x : scroller->geo.scroll_x;
                state.scroll_y0 =
                    scroller->node != nullptr ? scroller->node->scroll_y : scroller->geo.scroll_y;
                state.allow_x = allow_x;
                state.allow_y = allow_y;
            }
        }
    }
    // `:active` while the press is live; finish_click / cancel clears it.
    set_active(click_node, true);
    state.active_node = click_node;
    press_range_or_color(state, click_node, x, down_prevented);
    pointer_.press_state_[mods.pointer_id] = state;
    arm_html_drag(click_node, x, y, mods);
}

void Interaction::pointer_button(const std::string& type, float x, float y,
                                 const std::string& button, int count, Pointer mods) {
    mods.x = x;
    mods.y = y;
    if (!button.empty() && button != "none") {
        mods.button = button;
    }
    mods.count = count > 0 ? count : 1;

    if (type == "mouseMoved") {
        pointer_move(x, y, mods);
        update_html_drag(x, y, mods);
        pan_move(x, y, mods);
        drag(x, y);
        return;
    }
    if (type == "mouseReleased") {
        const std::string released = button.empty() || button == "none" ? "left" : button;
        release(x, y, released, mods);
        pointer_move(x, y, mods);
        return;
    }
    if (type != "mousePressed") {
        return;
    }
    // Hover first so capture / click targets see the current hit.
    set_hover(host_.layout != nullptr ? node_of(host_.layout->hit_test(x, y)) : nullptr, mods);
    pointer_.last_move_valid_ = false;
    pointer_.probe_valid_ = false;
    if (button == "left" || button == "none" || button.empty()) {
        press(x, y, mods.count, mods);
        return;
    }
    // Non-left: contextmenu (right) / auxclick (middle and others); no left-button defaults.
    press_non_left(x, y, button, mods.count, mods);
}

void Interaction::pointer_contact(float x, float y, const Pointer& mods, bool pressed, bool released,
                                  bool cancelled) {
    if (host_.layout == nullptr || host_.document == nullptr) {
        return;
    }
    const std::int32_t id = mods.pointer_id;
    if (pressed) {
        // Implicit capture (Pointer Events: touch and pen capture on pointerdown) so the rest of
        // this contact's stream goes to the node it started on.
        html::Node* node = node_of(host_.layout->hit_test(x, y));
        set_capture(id, node);
        if (node != nullptr) {
            if (html::Node* down = handler_owner(node, "pointerdown")) {
                Pointer payload = mods;
                payload.x = x;
                payload.y = y;
                payload.button = "left";
                payload.count = mods.count > 0 ? mods.count : 1;
                fire(down, "pointerdown", nullptr, nullptr, nullptr, &payload);
            }
        }
        return;
    }
    html::Node* target = capture_for(id);
    if (released || cancelled) {
        const char* kind = cancelled ? "pointercancel" : "pointerup";
        if (target != nullptr) {
            if (html::Node* handler = handler_owner(target, kind)) {
                Pointer payload = mods;
                payload.x = x;
                payload.y = y;
                payload.button = cancelled ? "" : "left";
                fire(handler, kind, nullptr, nullptr, nullptr, &payload);
            }
        }
        clear_capture(id);
        return;
    }
    if (target == nullptr) {
        return;
    }
    if (html::Node* move = handler_owner(target, "pointermove")) {
        Pointer payload = mods;
        payload.x = x;
        payload.y = y;
        payload.button = "";
        fire(move, "pointermove", nullptr, nullptr, nullptr, &payload);
    }
}

void Interaction::pointer_cancel(float x, float y, const Pointer& mods) {
    cancel_html_drag();
    if (host_.scroll_drag != nullptr) {
        *host_.scroll_drag = nullptr;
    }
    text_.selecting_text_ = false;
    const auto pressed = pointer_.press_state_.find(mods.pointer_id);
    if (pressed != pointer_.press_state_.end()) {
        set_active(pressed->second.active_node, false);
        pointer_.press_state_.erase(pressed);
    }
    html::Node* target = capture_for(mods.pointer_id);
    if (target != nullptr) {
        if (html::Node* handler = handler_owner(target, "pointercancel")) {
            Pointer payload = mods;
            payload.x = x;
            payload.y = y;
            payload.button = "";
            fire(handler, "pointercancel", nullptr, nullptr, nullptr, &payload);
        }
    }
    clear_capture(mods.pointer_id);
}

}  // namespace input
}  // namespace zui
