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
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zui {
namespace input {

using namespace input_detail;

bool hover_changes_style(const InteractionHost& host, const html::Node* old_chain,
                         const html::Node* new_chain) {
    if (!host.hover_styles) {
        return old_chain != nullptr || new_chain != nullptr;
    }
    for (const html::Node* cursor = new_chain; cursor != nullptr; cursor = cursor->parent) {
        if (host.hover_styles(*cursor)) {
            return true;
        }
    }
    for (const html::Node* cursor = old_chain; cursor != nullptr; cursor = cursor->parent) {
        if (host.hover_styles(*cursor)) {
            return true;
        }
    }
    return false;
}
void Interaction::set_hover(html::Node* node, const Pointer& mods) {
    if (host_.hovered == nullptr) {
        return;
    }
    html::Node*& hovered = *host_.hovered;
    if (node == hovered) {
        return;
    }
    html::Node* old = hovered;
    const bool restyle = hover_changes_style(host_, old, node);
    // The nodes the hover flags changed on are this change's dirty roots: a `:hover` compound only
    // matches the chain node itself and its descendants. Chains whose node no rule can match are
    // skipped, so `:hover` on a leaf does not drag the document in (docs/PERF.md section 2.4).
    std::vector<html::Node*> hover_roots;
    if (restyle) {
        const auto collect = [&](html::Node* head) {
            for (html::Node* cursor = head; cursor != nullptr; cursor = cursor->parent) {
                if (host_.hover_styles == nullptr || host_.hover_styles(*cursor)) {
                    hover_roots.push_back(cursor);
                }
            }
        };
        collect(old);
        collect(node);
    }
    // Selectors 4 section 6.2: `:hover` matches the designated element *and its ancestor chain*,
    // which is what makes `.card:hover .title` work while the pointer sits on a child. Every chain
    // node therefore carries the flag, and leaving hover clears the whole old chain.
    for (html::Node* cursor = old; cursor != nullptr; cursor = cursor->parent) {
        cursor->hovered = false;
    }
    for (html::Node* cursor = node; cursor != nullptr; cursor = cursor->parent) {
        cursor->hovered = true;
    }
    hovered = node;
    if (restyle) {
        note_restyle_roots(hover_roots);
    }

    // mouseleave/enter do not use the bubbling handler_owner walk: each node with the
    // attribute fires on itself only. mouseout/mouseover still use one handler_owner walk.
    if (!host_.dispatch) {
        return;
    }
    Pointer payload = mods;
    payload.button = "";

    std::vector<html::Node*> old_chain;
    for (html::Node* cursor = old; cursor != nullptr; cursor = cursor->parent) {
        old_chain.push_back(cursor);
    }
    std::vector<html::Node*> new_chain;
    for (html::Node* cursor = node; cursor != nullptr; cursor = cursor->parent) {
        new_chain.push_back(cursor);
    }
    html::Node* common = nullptr;
    {
        std::size_t i = old_chain.size();
        std::size_t j = new_chain.size();
        while (i > 0 && j > 0 && old_chain[i - 1] == new_chain[j - 1]) {
            common = old_chain[i - 1];
            --i;
            --j;
        }
    }

    // UI Events section 5.6 orders the four boundary events `mouseout`, `mouseleave`, `mouseover`,
    // `mouseenter`. This used to fire the non-bubbling pair first, so a page that binds both pairs
    // on one element saw enter/leave precede over/out - the reverse of the browser.
    if (old != nullptr) {
        if (html::Node* out = handler_owner(old, "mouseout")) {
            fire(out, "mouseout", nullptr, nullptr, nullptr, &payload);
        }
    }
    // Leave: old up to but not including common, innermost first.
    for (html::Node* n : old_chain) {
        if (n == common) {
            break;
        }
        if (html::handler_attribute(n, "pointerleave") != nullptr) {
            fire(n, "pointerleave", nullptr, nullptr, nullptr, &payload);
        }
        if (html::handler_attribute(n, "mouseleave") != nullptr) {
            fire(n, "mouseleave", nullptr, nullptr, nullptr, &payload);
        }
    }
    if (node != nullptr) {
        if (html::Node* over = handler_owner(node, "mouseover")) {
            fire(over, "mouseover", nullptr, nullptr, nullptr, &payload);
        }
    }
    // Enter: just below common down to new, outermost first.
    std::vector<html::Node*> enter;
    for (html::Node* n : new_chain) {
        if (n == common) {
            break;
        }
        enter.push_back(n);
    }
    for (auto it = enter.rbegin(); it != enter.rend(); ++it) {
        html::Node* n = *it;
        if (html::handler_attribute(n, "pointerenter") != nullptr) {
            fire(n, "pointerenter", nullptr, nullptr, nullptr, &payload);
        }
        if (html::handler_attribute(n, "mouseenter") != nullptr) {
            fire(n, "mouseenter", nullptr, nullptr, nullptr, &payload);
        }
    }
}
void Interaction::fire_pointer_move(float x, float y, const Pointer& mods) {
    if (!host_.dispatch) {
        return;
    }
    // Throttle: skip when the pointer has not moved since the last move fire.
    if (pointer_.last_move_valid_ && x == pointer_.last_move_x_ && y == pointer_.last_move_y_) {
        return;
    }
    pointer_.last_move_x_ = x;
    pointer_.last_move_y_ = y;
    pointer_.last_move_valid_ = true;

    html::Node* captured = capture_for(mods.pointer_id);
    html::Node* origin =
        captured != nullptr ? captured : (host_.hovered != nullptr ? *host_.hovered : nullptr);
    if (origin == nullptr) {
        return;
    }
    Pointer payload = mods;
    payload.x = x;
    payload.y = y;
    payload.button = "";
    html::Node* target = handler_owner(origin, "pointermove");
    if (target != nullptr) {
        fire(target, "pointermove", nullptr, nullptr, nullptr, &payload);
        return;
    }
    // Touch has no compatibility mousemove of its own; the primary contact's compat mouse events
    // are a later knife (PR-INPUT-3).
    if (mods.pointer_type == PointerType::Touch) {
        return;
    }
    target = handler_owner(origin, "mousemove");
    if (target != nullptr) {
        fire(target, "mousemove", nullptr, nullptr, nullptr, &payload);
    }
}
void Interaction::pointer_move(float x, float y, const Pointer& mods) {
    // The second segment `布局成本结构性修复方案.md` section 1.5 could not name: every frame
    // re-hits the tree from the root, and this call sits between the two `flush_style`s.
    zui::trace::Zone zone("pointer_move", zui::trace::Category::App);
    if (host_.layout == nullptr) {
        return;
    }
    const std::uint64_t gen = host_.layout->world_generation();
    // Same point, same buttons, same world: the element under the cursor has not changed, so the
    // probe (and the hover walk) can be skipped. This state is separate from `last_move_*`, which
    // `fire_pointer_move` owns as its own "the pointer did not move" throttle.
    if (pointer_.probe_valid_ && x == pointer_.probe_x_ && y == pointer_.probe_y_ && mods.buttons == pointer_.probe_mods_.buttons &&
        gen == pointer_.probe_world_gen_) {
        return;
    }
    pointer_.probe_x_ = x;
    pointer_.probe_y_ = y;
    pointer_.probe_mods_ = mods;
    pointer_.probe_world_gen_ = gen;
    pointer_.probe_valid_ = true;
    // :hover still tracks the hit under the pointer while capture redirects move events. A finger
    // is not a hover device: it does not steal the mouse's hover (PR-INPUT-2).
    if (mods.pointer_type != PointerType::Touch) {
        Pointer hover_mods = mods;
        hover_mods.x = x;
        hover_mods.y = y;
        html::Node* hit = node_of(host_.layout->hit_test(x, y));
        html::Node* hovered = host_.hovered != nullptr ? *host_.hovered : nullptr;
        // Keep `:hover` while the pointer is still inside the untransformed border *and* the box
        // as painted has left the point: a hover transform that moves the painted box off this
        // point would otherwise oscillate. A box whose painted border still holds the point is
        // only losing the hit to a node painted above it, and `:hover` follows that node.
        if (hit != hovered && hovered != nullptr && !under_node(hit, hovered) &&
            pointer_in_layout_border(*host_.layout, *hovered, x, y) &&
            !pointer_in_painted_border(*host_.layout, *hovered, x, y)) {
            hit = hovered;
        }
        set_hover(hit, hover_mods);
    }
    fire_pointer_move(x, y, mods);
}
void Interaction::sync_hover() {
    if (!pointer_.probe_valid_ || host_.layout == nullptr) {
        return;
    }
    if (host_.layout->world_generation() == pointer_.probe_world_gen_) {
        return;
    }
    Pointer mods = pointer_.probe_mods_;
    mods.x = pointer_.probe_x_;
    mods.y = pointer_.probe_y_;
    pointer_move(pointer_.probe_x_, pointer_.probe_y_, mods);
}

}  // namespace input
}  // namespace zui
