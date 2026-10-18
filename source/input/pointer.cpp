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

namespace input_detail {
html::Node* node_of(layout::Box* hit) {
    for (layout::Box* box = hit; box != nullptr; box = box->parent) {
        if (box->node == nullptr) {
            continue;
        }
        // Generated `::before` / `::after` / `::marker` nodes are not in the DOM child list.
        // Chrome reports the originating element for hits on the marker; walk to it.
        html::Node* n = box->node;
        while (n != nullptr && n->tag.size() >= 2 && n->tag[0] == ':' && n->tag[1] == ':') {
            n = n->parent;
        }
        return n;
    }
    return nullptr;
}

html::Node* editable_from(html::Node* node) {
    for (html::Node* cursor = node; cursor != nullptr; cursor = cursor->parent) {
        if (is_editable(cursor)) {
            return cursor;
        }
    }
    return nullptr;
}

/// The control a press focuses when the hit is not one itself: an `<img>` or `<i>` inside a
/// `<button>` reaches it here, the way a press on the button's own label does. A disabled control
/// stops the walk - nothing outside it takes the focus either.
html::Node* focus_candidate_from(html::Node* node) {
    for (html::Node* cursor = node; cursor != nullptr; cursor = cursor->parent) {
        if (is_focusable(cursor)) {
            return cursor->disabled ? nullptr : cursor;
        }
        if (cursor->disabled && (cursor->tag == "button" || cursor->tag == "input" ||
                                 cursor->tag == "select" || cursor->tag == "textarea")) {
            return nullptr;
        }
    }
    return nullptr;
}

/// EVENT.md section 4.3: from the origin, walk up to the document root and take the first
/// element that carries `on<kind>`. One firing runs one handler; this is not bubbling.
html::Node* handler_owner(html::Node* origin, std::string_view kind) {
    for (html::Node* cursor = origin; cursor != nullptr; cursor = cursor->parent) {
        if (html::handler_attribute(cursor, kind) != nullptr) {
            return cursor;
        }
    }
    return nullptr;
}


bool under_node(const html::Node* node, const html::Node* root) {
    for (const html::Node* cursor = node; cursor != nullptr; cursor = cursor->parent) {
        if (cursor == root) {
            return true;
        }
    }
    return false;
}

/// True when (@p x, @p y) sits in the node's untransformed layout border box. Hit-testing uses the
/// paint Aff2, so a `:hover { transform }` that slides the box off the pointer would otherwise
/// drop `:hover` on the next probe and the control flickers (motion Play, hello `.primary`).
bool pointer_in_layout_border(layout::LayoutEngine& layout, html::Node& node, float x, float y) {
    layout::Box* box = layout.principal_box(&node);
    if (box == nullptr || box->geo.width <= 0.f || box->geo.height <= 0.f) {
        return false;
    }
    using layout::paint_detail::Aff2;
    using layout::paint_detail::local_transform;
    using layout::paint_detail::xform_of;
    Aff2 paint;
    paint.a = box->paint_a;
    paint.b = box->paint_b;
    paint.c = box->paint_c;
    paint.d = box->paint_d;
    paint.e = box->paint_e;
    paint.f = box->paint_f;
    const Aff2 local = local_transform(box->geo.width, box->geo.height,
                                       xform_of(*box, box->geo.width, box->geo.height));
    Aff2 inv_local;
    if (!local.inverse(inv_local)) {
        return false;
    }
    const Aff2 layout_to_world = inv_local.then(paint);
    Aff2 world_to_layout;
    if (!layout_to_world.inverse(world_to_layout)) {
        return false;
    }
    float lx = 0.f;
    float ly = 0.f;
    world_to_layout.apply(x, y, lx, ly);
    return lx >= 0.f && ly >= 0.f && lx < box->geo.width && ly < box->geo.height;
}

/// True when (@p x, @p y) sits inside the border box as painted: the `map_box` Aff2 including the
/// box's own `transform`. The hit walk trusts the same Aff2, so a fresh hit that is a different
/// node while this answers true is a node painted *above* the box, and `:hover` belongs to that
/// node (the title bar over a scrolled preview), not to the box underneath. When this answers
/// false, the box itself moved off the point and `pointer_in_layout_border` still holds it.
bool pointer_in_painted_border(layout::LayoutEngine& layout, html::Node& node, float x, float y) {
    layout::Box* box = layout.principal_box(&node);
    if (box == nullptr || box->geo.width <= 0.f || box->geo.height <= 0.f) {
        return false;
    }
    using layout::paint_detail::Aff2;
    Aff2 paint;
    paint.a = box->paint_a;
    paint.b = box->paint_b;
    paint.c = box->paint_c;
    paint.d = box->paint_d;
    paint.e = box->paint_e;
    paint.f = box->paint_f;
    Aff2 world_to_box;
    if (!paint.inverse(world_to_box)) {
        return false;
    }
    float lx = 0.f;
    float ly = 0.f;
    world_to_box.apply(x, y, lx, ly);
    return lx >= 0.f && ly >= 0.f && lx < box->geo.width && ly < box->geo.height;
}

layout::Box* nearest_scroller(layout::Box* hit) {
    for (layout::Box* box = hit; box != nullptr; box = box->parent) {
        if (box->style_ptr == nullptr) {
            continue;
        }
        const layout::OverflowMode y = box->style().overflow_y;
        const layout::OverflowMode x = box->style().overflow_x;
        if (y == layout::OverflowMode::Auto || y == layout::OverflowMode::Scroll ||
            x == layout::OverflowMode::Auto || x == layout::OverflowMode::Scroll) {
            return box;
        }
    }
    return nullptr;
}


float view_height(const layout::Box& box) {
    return std::max(0.0f, box.geo.height - box.geo.border.vertical());
}

/// The only attribute `Event::data()` reaches: `data-<name>` on the element that fired.
std::string_view node_data_attribute(const void* element, std::string_view name) {
    const auto* node = static_cast<const html::Node*>(element);
    if (node == nullptr || name.empty()) {
        return {};
    }
    std::string key("data-");
    key.append(name);
    const auto it = node->attrs.find(key);
    return it == node->attrs.end() ? std::string_view{} : std::string_view(it->second);
}

}  // namespace input_detail

}  // namespace input
}  // namespace zui
