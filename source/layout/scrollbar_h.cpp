#include "layout/scrollbar_h.h"

#include "layout/overflow_clip.h"
#include "layout/scrollbar_metrics.h"
#include "layout/scrollbar_v.h"

#include <algorithm>

namespace zui {
namespace layout {

ScrollbarH horizontal_scrollbar(const Box& box, float thickness) {
    ScrollbarH sb;
    thickness = scrollbar_used_thickness(box.style(), thickness);
    if (thickness <= 0.f) {
        return sb;
    }
    if (box.style().overflow_x != OverflowMode::Auto &&
        box.style().overflow_x != OverflowMode::Scroll) {
        return sb;
    }
    const float content = box.geo.scroll_width;
    const float view = std::max(0.0f, box.geo.width - box.geo.border.horizontal());
    if (content <= view + 0.5f && box.style().overflow_x != OverflowMode::Scroll) {
        return sb;
    }
    sb.visible = true;
    sb.track_h = thickness;
    sb.track_w = view;
    sb.track_y = box.geo.world_y + box.geo.height - box.geo.border.bottom - sb.track_h;
    sb.track_x = box.geo.world_x + box.geo.border.left;
    const float scroll = box.node != nullptr ? box.node->scroll_x : 0;
    const float max_scroll = std::max(0.0f, content - view);
    // RTL uses a negative scrollLeft range (Interaction::scroll_range): 0 at the inline-start
    // (right) edge, -extent at the end. Map that onto the thumb the same way LTR maps 0..extent.
    const bool rtl = box.style().direction == Direction::Rtl;
    float t = 0.f;
    if (max_scroll > 0.f) {
        t = rtl ? (scroll + max_scroll) / max_scroll : scroll / max_scroll;
        t = std::max(0.f, std::min(1.f, t));
    }
    sb.thumb_w = scrollbar_thumb_extent(view, content);
    sb.thumb_x = sb.track_x + scrollbar_thumb_offset(view, content, t);
    sb.max_scroll = max_scroll;
    return sb;
}

ScrollbarH viewport_horizontal_scrollbar(const Box& root, float viewport_w, float viewport_h,
                                         float thickness) {
    ScrollbarH sb;
    if (thickness <= 0.f) {
        return sb;
    }
    const OverflowMode mode_x = used_viewport_overflow(root, false);
    if (mode_x == OverflowMode::Hidden || mode_x == OverflowMode::Clip) {
        return sb;
    }
    if (root.geo.scroll_width <= viewport_w + 0.5f) {
        return sb;
    }
    const OverflowMode mode_y = used_viewport_overflow(root, true);
    const bool bars_y = mode_y != OverflowMode::Hidden && mode_y != OverflowMode::Clip;
    const bool vertical = bars_y && root.geo.scroll_height > viewport_h + 0.5f;
    const float track_w = std::max(0.f, viewport_w - (vertical ? thickness : 0.f));
    sb.visible = true;
    sb.track_h = thickness;
    sb.track_w = track_w;
    sb.track_x = 0.f;
    sb.track_y = viewport_h - thickness;
    const float content = std::max(root.geo.scroll_width, 1.f);
    const float ratio = std::min(1.f, track_w / content);
    sb.thumb_w = std::max(16.f, track_w * ratio);
    sb.max_scroll = std::max(0.f, root.geo.scroll_width - track_w);
    const float scroll = root.node != nullptr ? root.node->scroll_x : 0.f;
    const float t = sb.max_scroll > 0.f ? std::min(1.f, scroll / sb.max_scroll) : 0.f;
    sb.thumb_x = sb.track_x + t * (track_w - sb.thumb_w);
    return sb;
}

}  // namespace layout
}  // namespace zui
