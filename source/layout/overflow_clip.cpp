#include "layout/overflow_clip.h"

namespace zui {
namespace layout {

bool clips_overflow(const Box& box) {
    // `contain: paint` clips the descendants to the padding box exactly like a non-visible
    // `overflow` does (CSS Containment section 2.4), so the same clip serves both.
    if (box.style_ptr != nullptr && box.style().contain_paint) {
        return true;
    }
    auto clips = [](OverflowMode mode) {
        return mode == OverflowMode::Hidden || mode == OverflowMode::Clip ||
               mode == OverflowMode::Auto || mode == OverflowMode::Scroll;
    };
    return clips(box.style().overflow_y) || clips(box.style().overflow_x);
}

bool overflow_disjoint(const Box& box, float x, float y, float w, float h) {
    return box.geo.world_x + box.geo.width < x || box.geo.world_y + box.geo.height < y || box.geo.world_x > x + w ||
           box.geo.world_y > y + h;
}

OverflowMode used_viewport_overflow(const Box& root, bool block_axis) {
    if (root.style_ptr == nullptr) {
        return OverflowMode::Auto;
    }
    auto axis = [block_axis](const Box& box) {
        return block_axis ? box.style().overflow_y : box.style().overflow_x;
    };
    const OverflowMode root_mode = axis(root);
    if (root_mode == OverflowMode::Visible) {
        for (const Box* child : root.children) {
            if (child == nullptr || child->node == nullptr || child->style_ptr == nullptr) {
                continue;
            }
            if (child->node->tag == "body") {
                const OverflowMode body_mode = axis(*child);
                if (body_mode != OverflowMode::Visible) {
                    return body_mode;
                }
                break;
            }
        }
        return OverflowMode::Auto;
    }
    return root_mode;
}

}  // namespace layout
}  // namespace zui
