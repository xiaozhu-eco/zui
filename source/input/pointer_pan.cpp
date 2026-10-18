#include "input/detail.h"
#include "input/interaction.h"

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

namespace {

layout::ScrollbarV scroller_bar_v(const InteractionHost& host, const layout::Box& box) {
    layout::ScrollbarV bar = layout::vertical_scrollbar(box, host.scrollbar_thickness);
    if (bar.visible || host.layout == nullptr || host.layout->root() != &box) {
        return bar;
    }
    const layout::Viewport vp = host.layout->viewport();
    return layout::viewport_vertical_scrollbar(box, vp.width, vp.height, host.scrollbar_thickness);
}

layout::ScrollbarH scroller_bar_h(const InteractionHost& host, const layout::Box& box) {
    layout::ScrollbarH bar = layout::horizontal_scrollbar(box, host.scrollbar_thickness);
    if (bar.visible || host.layout == nullptr || host.layout->root() != &box) {
        return bar;
    }
    const layout::Viewport vp = host.layout->viewport();
    return layout::viewport_horizontal_scrollbar(box, vp.width, vp.height, host.scrollbar_thickness);
}

bool uses_viewport_bar(const InteractionHost& host, const layout::Box& box, bool horizontal) {
    if (host.layout == nullptr || host.layout->root() != &box) {
        return false;
    }
    if (horizontal) {
        return !layout::horizontal_scrollbar(box, host.scrollbar_thickness).visible;
    }
    return !layout::vertical_scrollbar(box, host.scrollbar_thickness).visible;
}

}  // namespace

void Interaction::drag(float x, float y) {
    for (auto& entry : pointer_.press_state_) {
        html::Node* range = entry.second.range;
        if (range == nullptr) {
            continue;
        }
        if (apply_range_value(range, x)) {
            fire(range, "input");
        }
        return;
    }
    for (auto& entry : pointer_.press_state_) {
        html::Node* color = entry.second.color;
        if (color == nullptr) {
            continue;
        }
        if (apply_color_value(color, x)) {
            fire(color, "input");
        }
        return;
    }
    if (text_.selecting_text_ && text_.select_node_ != nullptr) {
        html::Node* origin = text_.select_node_;
        for (const auto& entry : pointer_.press_state_) {
            if (html::Node* captured = capture_for(entry.first)) {
                origin = captured;
                break;
            }
        }
        if (handler_owner(origin, "pointermove") == nullptr) {
            text_.drag_x_ = x;
            text_.drag_y_ = y;
            update_text_drag_focus();
        }
        return;
    }
    if (host_.scroll_drag == nullptr || *host_.scroll_drag == nullptr || host_.layout == nullptr) {
        return;
    }
    layout::Box* box = host_.layout->principal_box(*host_.scroll_drag);
    if (box == nullptr) {
        return;
    }
    bool moved = false;
    if (pointer_.drag_horizontal_) {
        const layout::ScrollbarH bar = scroller_bar_h(host_, *box);
        const float view = std::max(1.0f, bar.track_w);
        const float content = std::max(view, bar.max_scroll + view);
        const float travel = uses_viewport_bar(host_, *box, true)
                                 ? std::max(1.0f, bar.track_w - bar.thumb_w)
                                 : layout::scrollbar_travel(view, content);
        const float t = (x - pointer_.drag_origin_) / travel;
        moved = apply_scroll(
            *box, std::round(std::min(bar.max_scroll,
                                      std::max(0.0f, pointer_.drag_scroll0_ + t * bar.max_scroll))),
            box->node->scroll_y);
    } else {
        const layout::ScrollbarV bar = scroller_bar_v(host_, *box);
        // The thumb travels between the two arrow buttons, and the drag maps the pointer onto the
        // scroll range through that travel. Chrome records whole scroll offsets here, so round:
        // an 80px drag on the 200px bar over 900px of content is 80 * 700 / 128 = 437.5 -> 438,
        // which is what `thumb-drag`'s golden holds.
        const float view = std::max(1.0f, bar.track_h);
        const float content = std::max(view, bar.max_scroll + view);
        const float travel = uses_viewport_bar(host_, *box, false)
                                 ? std::max(1.0f, bar.track_h - bar.thumb_h)
                                 : layout::scrollbar_travel(view, content);
        const float t = (y - pointer_.drag_origin_) / travel;
        moved = apply_scroll(*box, box->node->scroll_x,
                             std::round(std::min(bar.max_scroll,
                                                 std::max(0.0f,
                                                          pointer_.drag_scroll0_ + t * bar.max_scroll))));
    }
    // A drag that lands on the offset the box already has publishes nothing: placing here would
    // find an empty mark set and walk the whole tree (docs/PERF_REFACTOR.md stage P).
    if (moved || !host_.layout->world_is_current()) {
        host_.layout->sync_world();
        if (host_.document != nullptr) {
            settle_content_visibility(*host_.layout, *host_.document, host_.scrollbar_thickness);
        }
    }
}
void Interaction::release(float x, float y, const std::string& button, const Pointer& mods) {
    text_.selecting_text_ = false;
    if (host_.scroll_drag != nullptr) {
        *host_.scroll_drag = nullptr;
    }
    Pointer payload = mods;
    payload.x = x;
    payload.y = y;
    payload.button = button;
    html::Node* captured = capture_for(mods.pointer_id);
    html::Node* origin =
        captured != nullptr ? captured : (host_.hovered != nullptr ? *host_.hovered : nullptr);
    if (origin != nullptr) {
        if (html::Node* up = handler_owner(origin, "pointerup")) {
            fire(up, "pointerup", nullptr, nullptr, nullptr, &payload);
        }
    }
    if (button == "left" || button.empty()) {
        const auto range_press = pointer_.press_state_.find(mods.pointer_id);
        if (range_press != pointer_.press_state_.end() && range_press->second.range != nullptr &&
            range_press->second.range->value != range_press->second.range_value0) {
            fire(range_press->second.range, "change");
        }
        if (range_press != pointer_.press_state_.end() && range_press->second.color != nullptr &&
            range_press->second.color->value != range_press->second.color_value0) {
            fire(range_press->second.color, "change");
        }
        // Drop and dragend land before click. A drag that passed the slop suppresses that click.
        end_html_drag(x, y, true);
        clear_capture(mods.pointer_id);
        // PR-INPUT-3: mouseup then click (browser order).
        finish_click(x, y, mods);
    }
}

html::Node* Interaction::capture_for(std::int32_t pointer_id) const {
    const auto it = pointer_.pointer_capture_.find(pointer_id);
    return it == pointer_.pointer_capture_.end() ? nullptr : it->second;
}

void Interaction::set_capture(std::int32_t pointer_id, html::Node* node) {
    if (node == nullptr) {
        clear_capture(pointer_id);
        return;
    }
    html::Node* previous = capture_for(pointer_id);
    if (previous == node) {
        return;
    }
    if (previous != nullptr) {
        fire(previous, "lostpointercapture");
    }
    pointer_.pointer_capture_[pointer_id] = node;
    fire(node, "gotpointercapture");
}

void Interaction::clear_capture(std::int32_t pointer_id) {
    html::Node* previous = capture_for(pointer_id);
    if (previous == nullptr) {
        return;
    }
    pointer_.pointer_capture_.erase(pointer_id);
    fire(previous, "lostpointercapture");
}

bool Interaction::press_scrollbar(layout::Box* hit, float x, float y) {
    layout::Box* bar_box = nullptr;
    bool axis_horizontal = false;
    if (layout::Box* root = host_.layout->root()) {
        const layout::ScrollbarV viewport_v = scroller_bar_v(host_, *root);
        if (viewport_v.visible &&
            layout::point_in_rect(x, y, viewport_v.track_x, viewport_v.track_y, viewport_v.track_w,
                                  viewport_v.track_h)) {
            bar_box = root;
            axis_horizontal = false;
        } else {
            const layout::ScrollbarH viewport_h = scroller_bar_h(host_, *root);
            if (viewport_h.visible && layout::point_in_rect(x, y, viewport_h.track_x,
                                                           viewport_h.track_y, viewport_h.track_w,
                                                           viewport_h.track_h)) {
                bar_box = root;
                axis_horizontal = true;
            }
        }
    }
    if (bar_box == nullptr) {
        for (layout::Box* box = hit; box != nullptr; box = box->parent) {
            const layout::ScrollbarV vertical = scroller_bar_v(host_, *box);
            if (vertical.visible && layout::point_in_rect(x, y, vertical.track_x, vertical.track_y,
                                                          vertical.track_w, vertical.track_h)) {
                bar_box = box;
                axis_horizontal = false;
                break;
            }
            const layout::ScrollbarH horizontal = scroller_bar_h(host_, *box);
            if (horizontal.visible &&
                layout::point_in_rect(x, y, horizontal.track_x, horizontal.track_y,
                                      horizontal.track_w, horizontal.track_h)) {
                bar_box = box;
                axis_horizontal = true;
                break;
            }
        }
    }
    if (bar_box != nullptr && bar_box->node != nullptr && host_.scroll_drag != nullptr) {
        text_.select_node_ = nullptr;
        if (axis_horizontal) {
            const layout::ScrollbarH bar = scroller_bar_h(host_, *bar_box);
            *host_.scroll_drag = bar_box->node;
            pointer_.drag_horizontal_ = true;
            pointer_.drag_origin_ = x;
            if (layout::point_in_rect(x, y, bar.thumb_x, bar.track_y, bar.thumb_w, bar.track_h)) {
                pointer_.drag_scroll0_ = bar_box->geo.scroll_x;
            } else if (bar.max_scroll > 0) {
                // A classic track click pages. It does not jump the thumb to the pointer.
                const float page = std::max(kArrowScrollPx, bar.track_w * kPageFraction);
                const float next = x < bar.thumb_x ? bar_box->node->scroll_x - page
                                                   : bar_box->node->scroll_x + page;
                if (apply_scroll(*bar_box, next, bar_box->node->scroll_y) ||
                    !host_.layout->world_is_current()) {
                    host_.layout->sync_world();
                    if (host_.document != nullptr) {
                        settle_content_visibility(*host_.layout, *host_.document,
                                                  host_.scrollbar_thickness);
                    }
                }
                pointer_.drag_scroll0_ = bar_box->node->scroll_x;
            }
        } else {
            const layout::ScrollbarV bar = scroller_bar_v(host_, *bar_box);
            *host_.scroll_drag = bar_box->node;
            pointer_.drag_horizontal_ = false;
            pointer_.drag_origin_ = y;
            if (layout::point_in_rect(x, y, bar.track_x, bar.thumb_y, bar.track_w, bar.thumb_h)) {
                pointer_.drag_scroll0_ = bar_box->geo.scroll_y;
            } else if (bar.max_scroll > 0) {
                const float page = std::max(kArrowScrollPx, bar.track_h * kPageFraction);
                const float next = y < bar.thumb_y ? bar_box->node->scroll_y - page
                                                   : bar_box->node->scroll_y + page;
                if (apply_scroll(*bar_box, bar_box->node->scroll_x, next) ||
                    !host_.layout->world_is_current()) {
                    host_.layout->sync_world();
                    if (host_.document != nullptr) {
                        settle_content_visibility(*host_.layout, *host_.document,
                                                  host_.scrollbar_thickness);
                    }
                }
                pointer_.drag_scroll0_ = bar_box->node->scroll_y;
            }
        }
        return true;
    }
    return false;
}

void Interaction::pan_move(float x, float y, const Pointer& mods) {
    if (host_.layout == nullptr) {
        return;
    }
    const auto it = pointer_.press_state_.find(mods.pointer_id);
    if (it == pointer_.press_state_.end()) {
        return;
    }
    PressState& state = it->second;
    if (state.scroller == nullptr) {
        return;
    }
    if (!state.panning) {
        const float dx = x - state.x;
        const float dy = y - state.y;
        if (dx * dx + dy * dy < kPanSlopPx * kPanSlopPx) {
            return;
        }
        state.panning = true;
        // The gesture became a scroll: the press is over, so `:active` goes away with it.
        set_active(state.active_node, false);
        state.active_node = nullptr;
        // Pointer Events: a gesture the engine turns into a scroll cancels the pointer, so the page
        // stops receiving this contact's moves and no click is produced on release.
        if (html::Node* cap = capture_for(mods.pointer_id)) {
            if (html::Node* cancel = handler_owner(cap, "pointercancel")) {
                Pointer payload = mods;
                payload.x = x;
                payload.y = y;
                payload.button = "";
                fire(cancel, "pointercancel", nullptr, nullptr, nullptr, &payload);
            }
        }
        clear_capture(mods.pointer_id);
    }
    layout::Box* box = host_.layout->principal_box(state.scroller);
    if (box == nullptr || box->node == nullptr) {
        return;
    }
    float sx = box->node->scroll_x;
    float sy = box->node->scroll_y;
    if (state.allow_x) {
        sx = state.scroll_x0 - (x - state.x);
    }
    if (state.allow_y) {
        sy = state.scroll_y0 - (y - state.y);
    }
    if (apply_scroll(*box, sx, sy) || !host_.layout->world_is_current()) {
        host_.layout->sync_world();
        if (host_.document != nullptr) {
            settle_content_visibility(*host_.layout, *host_.document, host_.scrollbar_thickness);
        }
    }
}

void Interaction::set_active(html::Node* node, bool on) {
    std::vector<html::Node*> changed;
    for (html::Node* n = node; n != nullptr; n = n->parent) {
        // A sheet with no `:active` rule can never see this bit, so setting it would restyle for
        // nothing (the same gate `:hover` uses; docs/PERF.md section 2.4).
        if (on && host_.active_styles != nullptr && !host_.active_styles(*n)) {
            continue;
        }
        if (n->active == on) {
            continue;
        }
        n->active = on;
        changed.push_back(n);
    }
    if (!changed.empty()) {
        note_restyle_roots(changed);
    }
}

void Interaction::touch_action_axes(const html::Node* node, const html::Node* scroller, bool& allow_x,
                                    bool& allow_y) {
    allow_x = true;
    allow_y = true;
    for (const html::Node* n = node; n != nullptr; n = n->parent) {
        const std::string* value = n->style("touch-action");
        if (value != nullptr && !value->empty()) {
            // CSS Pointer Events: a space-separated token list. `auto` / `manipulation` allow
            // both pan axes, `none` / `pinch-zoom` neither, `pan-*` pick one. Unknown tokens are
            // ignored (the whole declaration then leaves the axes alone).
            bool ax = false;
            bool ay = false;
            bool recognized = false;
            std::string token;
            const std::string& raw = *value;
            for (std::size_t i = 0; i <= raw.size(); ++i) {
                const bool end = i == raw.size();
                const char ch = end ? ' ' : raw[i];
                if (!end && !std::isspace(static_cast<unsigned char>(ch))) {
                    token.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
                    continue;
                }
                if (token.empty()) {
                    continue;
                }
                if (token == "auto" || token == "manipulation") {
                    ax = true;
                    ay = true;
                } else if (token == "pan-x" || token == "pan-left" || token == "pan-right") {
                    ax = true;
                } else if (token == "pan-y" || token == "pan-up" || token == "pan-down") {
                    ay = true;
                } else if (token != "none" && token != "pinch-zoom") {
                    token.clear();
                    continue;  // unknown token: leave this declaration ineffective
                }
                recognized = true;
                token.clear();
            }
            if (recognized) {
                allow_x = allow_x && ax;
                allow_y = allow_y && ay;
            }
        }
        if (n == scroller) {
            break;
        }
    }
}

}  // namespace input
}  // namespace zui
