#include "input/detail.h"
#include "input/interaction.h"

#include "html/selection.h"
#include "perf/perf_budget.h"
#include "layout/point.h"
#include "layout/scrollbar_h.h"
#include "layout/scrollbar_metrics.h"
#include "layout/scrollbar_v.h"
#include "log/log.h"
#include "trace/trace.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zui {
namespace input {

using namespace input_detail;

namespace input_detail {
 
}  // namespace input_detail

bool Interaction::fire(html::Node* target, std::string_view kind, const Key* key, const Drop* drop,
                       const Wheel* wheel, const Pointer* pointer, const Drag* drag,
                       const void* drag_origin) {
    if (target == nullptr) {
        return false;
    }
    // HTML dirty-value flag: any user `input` on the focused editable arms change-on-blur.
    if (kind == "input" && is_editable(target) && focused() == target) {
        text_.edit_dirty_ = true;
    }
    if (!host_.dispatch) {
        return false;
    }
    const std::string* handler = html::handler_attribute(target, kind);
    if (handler == nullptr) {
        return false;
    }
    // The legacy payload channel: a control's `value` attribute, else its text. Identity of a
    // row belongs in `data-*` and is read with Event::data(). EVENT.md section 6.1: a keydown
    // or beforeunload firing carries no value at all, so a handler never reads the focused
    // control's old text by accident. Drop paths live on is_drop(), wheel deltas on is_wheel(),
    // pointer coords on is_pointer(), and an element drag on is_drag().
    const bool carries_value = kind == "click" || kind == "input" || kind == "change";
    const std::string value =
        carries_value ? (target->value.empty() ? target->text : target->value) : std::string{};
    Event event(kind, *handler, target->id, value, target, &node_data_attribute, key, drop, wheel,
                pointer, drag, drag_origin);
    {
        trace::Zone zone("input.dispatch", trace::Category::Input);
        ZUI_TRACE("[input] {} -> #{} handler={}", std::string(kind), target->id, *handler);
        host_.dispatch(event);
    }
    perf::note_input_event();
    return event.default_prevented();
}
bool Interaction::key_event(html::Node* origin, std::string_view name, const Key& modifiers) {
    if (host_.document == nullptr || host_.document->root == nullptr || name.empty()) {
        return false;
    }
    // EVENT.md section 4.3: a keydown starts at the focused element, and with nothing focused it
    // starts at <body> - so `<body onkeydown>` works as a global shortcut before the first click
    // and while a text control has the focus, without stopping at the body element.
    html::Node* start =
        origin != nullptr ? origin : html::find_tag(host_.document->root.get(), "body");
    if (start == nullptr) {
        return false;
    }
    Key payload = modifiers;
    payload.key = name;
    html::Node* target = handler_owner(start, "keydown");
    return target != nullptr && fire(target, "keydown", &payload);
}
bool Interaction::fire_from(html::Node* origin, std::string_view kind) {
    html::Node* target = handler_owner(origin, kind);
    return target != nullptr && fire(target, kind, nullptr, nullptr);
}
bool Interaction::file_drop(float x, float y, const Drop& drop) {
    if (host_.document == nullptr || host_.document->root == nullptr || drop.count == 0) {
        return false;
    }
    html::Node* start = nullptr;
    if (host_.layout != nullptr) {
        start = node_of(host_.layout->hit_test(x, y));
    }
    if (start == nullptr) {
        start = html::find_tag(host_.document->root.get(), "body");
    }
    if (start == nullptr) {
        return false;
    }
    html::Node* target = handler_owner(start, "drop");
    return target != nullptr && fire(target, "drop", nullptr, &drop);
}
namespace {

float key_attr_number(const html::Node* node, const char* key, float fallback) {
    if (node == nullptr || key == nullptr) {
        return fallback;
    }
    const auto it = node->attrs.find(key);
    if (it == node->attrs.end() || it->second.empty()) {
        return fallback;
    }
    char* end = nullptr;
    const float v = std::strtof(it->second.c_str(), &end);
    return end == it->second.c_str() ? fallback : v;
}

bool key_has_attr(const html::Node* node, const char* key) {
    if (node == nullptr || key == nullptr) {
        return false;
    }
    const auto it = node->attrs.find(key);
    return it != node->attrs.end() && !it->second.empty();
}

std::string key_format_scalar(float value) {
    const float nearest = std::round(value);
    if (std::fabs(value - nearest) < 1e-3f && std::fabs(nearest) < 1.0e7f) {
        return std::to_string(static_cast<long long>(std::llround(value)));
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(value));
    return buf;
}

/// ArrowUp / ArrowDown on `<input type=number>`. Empty starts at 0. @return true when it changed.
bool step_number(html::Node* node, float direction) {
    float current = 0.f;
    if (!node->value.empty()) {
        char* end = nullptr;
        const float parsed = std::strtof(node->value.c_str(), &end);
        if (end != node->value.c_str()) {
            current = parsed;
        }
    }
    float step = key_attr_number(node, "step", 1.f);
    if (!(step > 0.f) || (key_has_attr(node, "step") && node->attrs.find("step")->second == "any")) {
        step = 1.f;
    }
    float next = current + direction * step;
    if (key_has_attr(node, "min")) {
        next = std::max(next, key_attr_number(node, "min", next));
    }
    if (key_has_attr(node, "max")) {
        next = std::min(next, key_attr_number(node, "max", next));
    }
    const std::string text = key_format_scalar(next);
    if (text == node->value) {
        return false;
    }
    node->value = text;
    node->text = text;
    node->attrs["value"] = text;
    html::note_paint_change_at(node);
    return true;
}

}  // namespace

bool Interaction::key(const std::string& type, std::string_view key, const Key& modifiers) {
    if (host_.document == nullptr || host_.document->root == nullptr) {
        return false;
    }
    if (type == "keyUp") {
        if (key.empty()) {
            return false;
        }
        html::Node* focused = host_.focused != nullptr ? *host_.focused : nullptr;
        html::Node* start =
            focused != nullptr ? focused : html::find_tag(host_.document->root.get(), "body");
        if (start == nullptr) {
            return false;
        }
        Key payload = modifiers;
        payload.key = key;
        html::Node* target = handler_owner(start, "keyup");
        return target != nullptr && fire(target, "keyup", &payload);
    }
    if (type != "keyDown" || host_.layout == nullptr) {
        return false;
    }
    // HTML drag-and-drop: Escape cancels the gesture and does not reach the page shortcut.
    if (key == "Escape" && drag_.html_drag_.phase != HtmlDragPhase::Idle) {
        cancel_html_drag();
        return true;
    }
    // HOST PR-15: Escape dismisses an open <select> before any app shortcut (e.g. close dialog).
    if (key == "Escape" && pointer_.open_select_ != nullptr) {
        close_select_popup();
        return true;
    }
    html::Node* focused = host_.focused != nullptr ? *host_.focused : nullptr;
    // Section 7: the handler runs before the engine's default action for the key. Activation,
    // implicit submit, and the editing shortcuts below skip their default when that keydown
    // called prevent_default. Tab inside a modal dialog does too. A handler that rebuilds the
    // tree (kanban's add-card) frees `focused`; the defaults below must not touch it.
    const std::uint64_t dom_before = html::dom_stamp(host_.document);
    const bool prevented = key_event(focused, key, modifiers);
    if (html::dom_stamp(host_.document) != dom_before) {
        return prevented;
    }
    if (key == "Tab") {
        // An open modal dialog replaces document Tab order. prevent_default skips that move.
        html::Node* modal = html::top_modal_dialog(host_.document->root.get());
        const std::vector<html::Node*> order =
            tab_order(modal != nullptr ? modal : host_.document->root.get());
        if (modal != nullptr && prevented) {
            return true;
        }
        if (order.empty() || host_.focused == nullptr) {
            return prevented;
        }
        std::size_t next = 0;
        bool found = false;
        for (std::size_t i = 0; i < order.size(); ++i) {
            if (order[i] == focused) {
                found = true;
                next = modifiers.shift ? (i + order.size() - 1) % order.size()
                                       : (i + 1) % order.size();
                break;
            }
        }
        if (!found) {
            next = modifiers.shift ? order.size() - 1 : 0;
        }
        focus(order[next], /*keyboard_modality=*/true);
        return prevented;
    }
    // `<input type=number>` arrows step the scalar. prevent_default skips the step.
    if (focused != nullptr && !focused->disabled && html::input_type_is(focused, "number") &&
        (key == "ArrowUp" || key == "ArrowDown") && !modifiers.composing) {
        if (!prevented && step_number(focused, key == "ArrowUp" ? 1.f : -1.f)) {
            // The value is the control's text run. App::set syncs that box; this default must too,
            // or the glyphs built at load stay on the previous number.
            if (host_.layout != nullptr) {
                host_.layout->sync_text(focused);
                host_.layout->mark_needs_layout(focused);
            }
            if (host_.mark_geometry) {
                host_.mark_geometry();
            }
            fire(focused, "input");
            fire(focused, "change");
        }
        return prevented;
    }
    // Space / Enter activates a focused file input (same sugar as click).
    if (focused != nullptr && is_file_input(focused) && (key == " " || key == "Enter") &&
        !modifiers.composing) {
        activate_file_input(focused);
        return prevented;
    }
    if (focused != nullptr && is_editable(focused)) {
        seed_content_editable(focused);
        auto note_edit = [&] {
            if (host_.layout != nullptr) {
                host_.layout->sync_text(focused);
                host_.layout->mark_needs_layout(focused);
            }
            if (host_.mark_geometry) {
                host_.mark_geometry();
            }
            note_restyle_roots({focused});
            fire(focused, "input");
        };
        // PR-5b: Ctrl/Cmd+A selects the whole editable value (text mirrored first).
        if (!modifiers.composing && (modifiers.ctrl || modifiers.meta) &&
            (key == "a" || key == "A")) {
            text_.selecting_text_ = false;
            commit_edit_selection(focused, 0, focused->value.size());
            return prevented;
        }
        // Enter in a single-line field submits its `<form>` unless the keydown cancelled it.
        // A textarea's Enter is a line break (apply_editing), not a submit.
        if (!modifiers.composing && key == "Enter" && submits_on_enter(focused)) {
            if (!prevented) {
                if (html::Node* form = form_ancestor(focused)) {
                    submit_form(form, nullptr);
                }
            }
            return prevented;
        }
        // Bold / italic inside a contenteditable host. The mark lives on the subtree.
        if (!modifiers.composing && (modifiers.ctrl || modifiers.meta) &&
            is_content_editable(focused) && (key == "b" || key == "B" || key == "i" || key == "I")) {
            if (!prevented) {
                toggle_inline_mark(focused, (key == "b" || key == "B") ? 1 : 2);
            }
            return prevented;
        }
        // Copy / cut / paste dispatch before the clipboard changes. prevent_default leaves both
        // the editor and the text clipboard alone.
        if (!modifiers.composing && (modifiers.ctrl || modifiers.meta)) {
            const bool is_c = key == "c" || key == "C";
            const bool is_x = key == "x" || key == "X";
            const bool is_v = key == "v" || key == "V";
            if (is_c || is_x || is_v) {
                Key payload = modifiers;
                payload.key = key;
                const bool blocked = fire(focused, is_c ? "copy" : is_x ? "cut" : "paste", &payload);
                if (blocked || !host_.clipboard_text || !host_.set_clipboard_text) {
                    return prevented;
                }
            }
        }
        if (!modifiers.composing && (modifiers.ctrl || modifiers.meta) && host_.clipboard_text &&
            host_.set_clipboard_text) {
            const bool is_c = key == "c" || key == "C";
            const bool is_x = key == "x" || key == "X";
            const bool is_v = key == "v" || key == "V";
            focused->text = focused->value;
            const html::SelectionRange sel = host_.document->selection;
            const bool range_on_focus = sel.node == focused && !html::selection_empty(sel) &&
                                        sel.end <= focused->text.size();
            if (is_c || is_x) {
                // With nothing selected there is nothing to take: the clipboard and the value
                // both stay put (Chrome). A whole-value copy / cut would read text the shortcut
                // never named; Ctrl+A is the range that asks for all of it.
                if (range_on_focus) {
                    host_.set_clipboard_text(focused->text.substr(sel.start, sel.end - sel.start));
                    if (is_x) {
                        if (is_content_editable(focused)) {
                            html::delete_flatten_range(focused, sel.start, sel.end);
                            html::refresh_edit_value(focused);
                        } else {
                            focused->value.erase(sel.start, sel.end - sel.start);
                            focused->text = focused->value;
                        }
                        commit_edit_selection(focused, sel.start, sel.start);
                        text_.selecting_text_ = false;
                        note_edit();
                    }
                }
            } else if (is_v) {
                ClipboardText clip = host_.clipboard_text();
                if (clip.status == ClipboardStatus::Ok && !clip.text.empty()) {
                    // A paste edits the value where the caret is, over the range when one is
                    // selected. Writing the whole value threw away everything the user had
                    // typed - the caret's own text is what the clipboard replaces.
                    std::size_t at = static_cast<std::size_t>(std::max(focused->caret, 0));
                    at = std::min(at, focused->value.size());
                    const std::size_t from = range_on_focus ? sel.start : at;
                    const std::size_t to = range_on_focus ? sel.end : from;
                    if (is_content_editable(focused)) {
                        if (from < to) {
                            html::delete_flatten_range(focused, from, to);
                        }
                        html::insert_text_at(focused, from, clip.text);
                        html::refresh_edit_value(focused);
                    } else {
                        focused->value.replace(from, to - from, clip.text);
                        focused->text = focused->value;
                    }
                    const std::size_t caret = from + clip.text.size();
                    commit_edit_selection(focused, caret, caret);
                    text_.selecting_text_ = false;
                    note_edit();
                }
            }
        }
        return prevented;
    }
    if (!modifiers.composing && host_.document != nullptr &&
        (key == "ArrowLeft" || key == "ArrowRight") &&
        (focused == nullptr || !is_editable(focused))) {
        if (move_document_caret(key, modifiers.shift)) {
            return prevented;
        }
    }
    // Enter / Space activate a focused button (and Enter a focused link). Space must not scroll.
    if (focused != nullptr && !modifiers.composing &&
        (is_activation_target(focused) ||
         (focused->tag == "a" && key == "Enter" &&
          focused->attrs.find("href") != focused->attrs.end())) &&
        (key == "Enter" || (key == " " && is_activation_target(focused)))) {
        if (!prevented) {
            activate_control(focused);
        }
        return prevented;
    }
    // Number and the other non-text single-line inputs are not `is_editable`, but Enter in a
    // form still submits them.
    if (focused != nullptr && !modifiers.composing && key == "Enter" && submits_on_enter(focused)) {
        if (!prevented) {
            if (html::Node* form = form_ancestor(focused)) {
                submit_form(form, nullptr);
            }
        }
        return prevented;
    }
    layout::Box* origin = focused != nullptr ? host_.layout->principal_box(focused) : nullptr;
    layout::Box* box = scroller_from(origin);
    float view_w = -1.f;
    float view_h = -1.f;
    if (box != nullptr) {
        document_scroll_view(*host_.layout, *box, host_.scrollbar_thickness, view_w, view_h);
    }
    if (box == nullptr || box->node == nullptr) {
        return prevented;
    }
    float dx = 0;
    float dy = 0;
    const float port = view_h >= 0.f ? view_h : view_height(*box);
    const float page = std::max(kArrowScrollPx, port * kPageFraction);
    if (key == "ArrowDown") {
        dy = kArrowScrollPx;
    } else if (key == "ArrowUp") {
        dy = -kArrowScrollPx;
    } else if (key == "ArrowRight") {
        dx = kArrowScrollPx;
    } else if (key == "ArrowLeft") {
        dx = -kArrowScrollPx;
    } else if (key == "PageDown" || key == " ") {
        dy = page;
    } else if (key == "PageUp") {
        dy = -page;
    } else {
        return prevented;
    }
    // A key at the end of the range moves nothing, and a frame with no placement to publish must
    // not fall back to a whole-tree one (docs/PERF_REFACTOR.md stage P).
    if (apply_scroll(*box, box->node->scroll_x + dx, box->node->scroll_y + dy, view_w, view_h) ||
        !host_.layout->world_is_current()) {
        host_.layout->sync_world();
        if (host_.document != nullptr) {
            settle_content_visibility(*host_.layout, *host_.document, host_.scrollbar_thickness);
        }
    }
    return prevented;
}

}  // namespace input
}  // namespace zui
