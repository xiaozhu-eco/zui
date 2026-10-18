#include "ascii.h"
#include "input/detail.h"
#include "input/interaction.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace zui {
namespace input {

using namespace input_detail;

namespace {

constexpr float kHtmlDragSlopPx = 4.f;

std::string_view attr_of(const html::Node* node, const char* key) {
    if (node == nullptr || key == nullptr) {
        return {};
    }
    const auto it = node->attrs.find(key);
    return it == node->attrs.end() ? std::string_view{} : std::string_view(it->second);
}

bool equals_ci(std::string_view text, std::string_view lower) {
    return zui::equals_ci(text, lower);
}

enum class DragMode { No, Yes, Auto };

DragMode drag_mode(const html::Node* node) {
    const std::string_view raw = attr_of(node, "draggable");
    if (raw.empty() || equals_ci(raw, "auto")) {
        return DragMode::Auto;
    }
    if (equals_ci(raw, "true")) {
        return DragMode::Yes;
    }
    if (equals_ci(raw, "false")) {
        return DragMode::No;
    }
    return DragMode::Auto;
}

bool auto_draggable(const html::Node* node) {
    if (node == nullptr) {
        return false;
    }
    if (node->tag == "img") {
        return !attr_of(node, "src").empty();
    }
    if (node->tag == "a") {
        return !attr_of(node, "href").empty();
    }
    return false;
}

/// A control owns its gesture unless that control is itself the drag source (`draggable="true"`).
bool drag_blocker(const html::Node* node) {
    if (node == nullptr) {
        return false;
    }
    const std::string& tag = node->tag;
    return tag == "button" || tag == "input" || tag == "textarea" || tag == "select" ||
           tag == "a" || tag == "label";
}

html::Node* draggable_source(html::Node* hit) {
    for (html::Node* node = hit; node != nullptr; node = node->parent) {
        if (node->disabled && drag_blocker(node)) {
            return nullptr;
        }
        const DragMode mode = drag_mode(node);
        if (mode == DragMode::Yes) {
            return node;
        }
        // An `<a href>` / `<img>` in the auto state is itself the drag source. An explicit
        // `draggable="false"` on a control, or any other control, keeps the gesture (a click on
        // 上移 must not drag the card that contains it).
        if (mode == DragMode::Auto && auto_draggable(node)) {
            return node;
        }
        if (drag_blocker(node)) {
            return nullptr;
        }
    }
    return nullptr;
}

void append_plain(const html::Node* node, std::string& out) {
    if (node == nullptr) {
        return;
    }
    if (!node->text.empty() && (node->tag == "#text" || node->children.empty())) {
        if (!out.empty() && out.back() != ' ') {
            out.push_back(' ');
        }
        out.append(node->text);
    }
    for (const auto& child : node->children) {
        append_plain(child.get(), out);
    }
}

std::string plain_text(const html::Node* node) {
    std::string out;
    append_plain(node, out);
    trim_inplace(out);
    constexpr std::size_t kCap = 4096;
    if (out.size() > kCap) {
        out.resize(kCap);
        while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80) {
            out.pop_back();
        }
    }
    return out;
}

void fill_store(const html::Node& source, std::string& text, std::string& uri) {
    if (source.tag == "a") {
        uri = std::string(attr_of(&source, "href"));
        text = uri;
        return;
    }
    if (source.tag == "img") {
        uri = std::string(attr_of(&source, "src"));
        const std::string_view alt = attr_of(&source, "alt");
        text = alt.empty() ? uri : std::string(alt);
        return;
    }
    text = plain_text(&source);
    uri.clear();
}

DropEffect effect_from_modifiers(bool ctrl, bool shift, bool alt, bool meta) {
    // effectAllowed stays uninitialized, so every effect is legal. Modifiers pick the one the
    // handler sees during dragover: Ctrl/Command copies, Alt links, otherwise the gesture moves.
    if ((ctrl || meta) && !shift) {
        return DropEffect::Copy;
    }
    if (alt && !ctrl && !meta) {
        return DropEffect::Link;
    }
    return DropEffect::Move;
}

}  // namespace

Interaction::DragFeedback Interaction::drag_feedback() const {
    DragFeedback out;
    if (drag_.html_drag_.phase != HtmlDragPhase::Active) {
        return out;
    }
    out.active = true;
    out.x = drag_.html_drag_.x;
    out.y = drag_.html_drag_.y;
    out.start_x = drag_.html_drag_.start_x;
    out.start_y = drag_.html_drag_.start_y;
    out.source = drag_.html_drag_.source;
    return out;
}

bool Interaction::take_drag_feedback_dirty() {
    const bool dirty = drag_.drag_feedback_dirty_;
    drag_.drag_feedback_dirty_ = false;
    return dirty;
}

void Interaction::note_drag_image() {
    drag_.drag_feedback_dirty_ = true;
}

void Interaction::reset_html_drag() {
    const bool was_active = drag_.html_drag_.phase == HtmlDragPhase::Active;
    drag_.html_drag_ = HtmlDrag{};
    if (was_active) {
        note_drag_image();
    }
}

bool Interaction::fire_drag(html::Node* target, std::string_view kind) {
    if (target == nullptr || drag_.html_drag_.source == nullptr) {
        return false;
    }
    Drag payload;
    payload.x = drag_.html_drag_.x;
    payload.y = drag_.html_drag_.y;
    payload.source_id = drag_.html_drag_.source_id;
    payload.text = drag_.html_drag_.text;
    payload.uri = drag_.html_drag_.uri;
    payload.effect = drag_.html_drag_.effect;
    payload.allowed = EffectAllowed::Uninitialized;
    return fire(target, kind, nullptr, nullptr, nullptr, nullptr, &payload, drag_.html_drag_.source);
}

bool Interaction::insert_dropped_text(html::Node* edit) {
    if (edit == nullptr || drag_.html_drag_.text.empty() || !is_editable(edit)) {
        return false;
    }
    std::size_t at = edit->value.size();
    if (focused() == edit) {
        at = static_cast<std::size_t>(std::max(edit->caret, 0));
        if (at > edit->value.size()) {
            at = edit->value.size();
        }
    }
    edit->value.insert(at, drag_.html_drag_.text);
    edit->text = edit->value;
    edit->caret = static_cast<int>(at + drag_.html_drag_.text.size());
    if (host_.layout != nullptr) {
        host_.layout->sync_text(edit);
        host_.layout->mark_needs_layout(edit);
    }
    if (host_.mark_geometry) {
        host_.mark_geometry();
    }
    note_restyle_roots({edit});
    fire(edit, "input");
    drag_.html_drag_.effect = DropEffect::Copy;
    return true;
}

void Interaction::arm_html_drag(html::Node* hit, float x, float y, const Pointer& mods) {
    if (drag_.html_drag_.phase != HtmlDragPhase::Idle) {
        return;
    }
    html::Node* source = draggable_source(hit);
    if (source == nullptr) {
        return;
    }
    drag_.html_drag_.phase = HtmlDragPhase::Pending;
    drag_.html_drag_.pointer_id = mods.pointer_id;
    drag_.html_drag_.source = source;
    drag_.html_drag_.start_x = x;
    drag_.html_drag_.start_y = y;
    drag_.html_drag_.x = x;
    drag_.html_drag_.y = y;
}

void Interaction::update_html_drag(float x, float y, const Pointer& mods) {
    if (drag_.html_drag_.phase == HtmlDragPhase::Idle || mods.pointer_id != drag_.html_drag_.pointer_id) {
        return;
    }
    drag_.html_drag_.x = x;
    drag_.html_drag_.y = y;
    drag_.html_drag_.ctrl = mods.ctrl;
    drag_.html_drag_.shift = mods.shift;
    drag_.html_drag_.alt = mods.alt;
    drag_.html_drag_.meta = mods.meta;
    if (drag_.html_drag_.phase == HtmlDragPhase::Pending) {
        const float dx = x - drag_.html_drag_.start_x;
        const float dy = y - drag_.html_drag_.start_y;
        if (dx * dx + dy * dy < kHtmlDragSlopPx * kHtmlDragSlopPx) {
            return;
        }
        html::Node* source = drag_.html_drag_.source;
        if (source == nullptr) {
            reset_html_drag();
            return;
        }
        drag_.html_drag_.phase = HtmlDragPhase::Active;
        drag_.html_drag_suppress_click_ = true;
        drag_.html_drag_.source_id = source->id;
        fill_store(*source, drag_.html_drag_.text, drag_.html_drag_.uri);
        const auto pressed = pointer_.press_state_.find(mods.pointer_id);
        if (pressed != pointer_.press_state_.end()) {
            pressed->second.scroller = nullptr;
            pressed->second.panning = false;
        }
        drag_.html_drag_.effect = DropEffect::None;
        note_drag_image();
        const std::uint64_t stamp = html::dom_stamp(host_.document);
        if (html::Node* start = handler_owner(source, "dragstart")) {
            if (fire_drag(start, "dragstart") || html::dom_stamp(host_.document) != stamp ||
                drag_.html_drag_.source == nullptr) {
                reset_html_drag();
                return;
            }
        }
        if (drag_.html_drag_.phase != HtmlDragPhase::Active) {
            return;
        }
    }
    if (drag_.html_drag_.source == nullptr || host_.layout == nullptr) {
        reset_html_drag();
        return;
    }
    note_drag_image();
    const std::uint64_t stamp = html::dom_stamp(host_.document);
    if (html::Node* drag = handler_owner(drag_.html_drag_.source, "drag")) {
        if (fire_drag(drag, "drag")) {
            drag_.html_drag_.effect = DropEffect::None;
            drag_.html_drag_.accepted = false;
        }
        if (html::dom_stamp(host_.document) != stamp || drag_.html_drag_.source == nullptr) {
            reset_html_drag();
            return;
        }
    }
    drag_.html_drag_.hit = node_of(host_.layout->hit_test(x, y));
    html::Node* zone = handler_owner(drag_.html_drag_.hit, "dragover");
    html::Node* edit = editable_from(drag_.html_drag_.hit);
    if (zone != drag_.html_drag_.zone) {
        if (drag_.html_drag_.zone != nullptr) {
            if (html::Node* leave = handler_owner(drag_.html_drag_.zone, "dragleave")) {
                fire_drag(leave, "dragleave");
            }
            if (html::dom_stamp(host_.document) != stamp || drag_.html_drag_.source == nullptr) {
                reset_html_drag();
                return;
            }
        }
        drag_.html_drag_.zone = zone;
        if (zone != nullptr) {
            if (html::Node* enter = handler_owner(zone, "dragenter")) {
                fire_drag(enter, "dragenter");
            }
            if (html::dom_stamp(host_.document) != stamp || drag_.html_drag_.source == nullptr) {
                reset_html_drag();
                return;
            }
        }
    }
    if (zone != nullptr) {
        drag_.html_drag_.effect = effect_from_modifiers(drag_.html_drag_.ctrl, drag_.html_drag_.shift, drag_.html_drag_.alt,
                                                  drag_.html_drag_.meta);
        const bool prevented = fire_drag(zone, "dragover");
        if (html::dom_stamp(host_.document) != stamp || drag_.html_drag_.source == nullptr) {
            reset_html_drag();
            return;
        }
        if (prevented) {
            drag_.html_drag_.accepted = true;
        } else if (edit != nullptr && !drag_.html_drag_.text.empty()) {
            drag_.html_drag_.accepted = true;
            drag_.html_drag_.effect = DropEffect::Copy;
        } else {
            drag_.html_drag_.accepted = false;
            drag_.html_drag_.effect = DropEffect::None;
        }
        return;
    }
    if (edit != nullptr && !drag_.html_drag_.text.empty()) {
        drag_.html_drag_.accepted = true;
        drag_.html_drag_.effect = DropEffect::Copy;
        return;
    }
    drag_.html_drag_.accepted = false;
    drag_.html_drag_.effect = DropEffect::None;
}

void Interaction::end_html_drag(float x, float y, bool allow_drop) {
    if (drag_.html_drag_.phase == HtmlDragPhase::Idle) {
        return;
    }
    if (drag_.html_drag_.phase == HtmlDragPhase::Pending) {
        reset_html_drag();
        return;
    }
    // One last drag / dragover at the release point, so the drop sees the element under the
    // pointer rather than the previous move.
    Pointer mods;
    mods.pointer_id = drag_.html_drag_.pointer_id;
    mods.ctrl = drag_.html_drag_.ctrl;
    mods.shift = drag_.html_drag_.shift;
    mods.alt = drag_.html_drag_.alt;
    mods.meta = drag_.html_drag_.meta;
    mods.x = x;
    mods.y = y;
    update_html_drag(x, y, mods);
    if (drag_.html_drag_.phase != HtmlDragPhase::Active) {
        return;
    }
    const std::uint64_t stamp = html::dom_stamp(host_.document);
    html::Node* edit = editable_from(drag_.html_drag_.hit);
    bool dropped = false;
    if (allow_drop && drag_.html_drag_.accepted) {
        if (html::Node* drop = handler_owner(drag_.html_drag_.hit, "drop")) {
            dropped = fire_drag(drop, "drop");
        }
        if (html::dom_stamp(host_.document) != stamp || drag_.html_drag_.source == nullptr) {
            reset_html_drag();
            return;
        }
        if (dropped) {
            // The handler accepted the drop. Keep the effect dragover negotiated.
        } else if (edit != nullptr && !drag_.html_drag_.text.empty()) {
            insert_dropped_text(edit);
        } else {
            drag_.html_drag_.effect = DropEffect::None;
        }
    } else {
        drag_.html_drag_.effect = DropEffect::None;
    }
    if (html::dom_stamp(host_.document) != stamp || drag_.html_drag_.source == nullptr) {
        reset_html_drag();
        return;
    }
    if (html::Node* end = handler_owner(drag_.html_drag_.source, "dragend")) {
        fire_drag(end, "dragend");
    }
    reset_html_drag();
}

void Interaction::cancel_html_drag() {
    drag_.html_drag_.accepted = false;
    end_html_drag(drag_.html_drag_.x, drag_.html_drag_.y, false);
}

}  // namespace input
}  // namespace zui
