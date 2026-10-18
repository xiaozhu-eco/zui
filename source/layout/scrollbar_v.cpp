#include "layout/scrollbar_v.h"

#include "layout/overflow_clip.h"
#include "layout/scrollbar_metrics.h"

#include <algorithm>

namespace zui {
namespace layout {

ScrollbarV vertical_scrollbar(const Box& box, float thickness) {
    ScrollbarV sb;
    thickness = scrollbar_used_thickness(box.style(), thickness);
    if (thickness <= 0.f) {
        return sb;
    }
    if (box.style().overflow_y != OverflowMode::Auto &&
        box.style().overflow_y != OverflowMode::Scroll) {
        return sb;
    }
    // `scroll_height` is the scrollable extent (the client box plus its overflow), which is what
    // `scrollHeight` reports - the used content box is the client box and never overflows itself.
    const float content = box.geo.scroll_height;
    // `scrollHeight` includes the padding. The client box is the border box minus the borders.
    // Subtracting the padding as well made a single nowrap line, which fits, report a vertical
    // scrollbar whenever the box had padding.
    const float view = std::max(0.0f, box.geo.height - box.geo.border.vertical());
    if (content <= view + 0.5f && box.style().overflow_y != OverflowMode::Scroll) {
        return sb;
    }
    sb.visible = true;
    sb.track_w = thickness;
    sb.track_h = view;
    sb.track_x = box.geo.world_x + box.geo.width - box.geo.border.right - sb.track_w;
    sb.track_y = box.geo.world_y + box.geo.border.top;
    const float scroll = box.node != nullptr ? box.node->scroll_y : 0;
    const float max_scroll = std::max(0.0f, content - view);
    const float t = max_scroll > 0 ? std::max(0.0f, std::min(1.0f, scroll / max_scroll)) : 0.f;
    sb.thumb_h = scrollbar_thumb_extent(view, content);
    sb.thumb_y = sb.track_y + scrollbar_thumb_offset(view, content, t);
    sb.max_scroll = max_scroll;
    return sb;
}

ScrollbarV viewport_vertical_scrollbar(const Box& root, float viewport_w, float viewport_h,
                                       float thickness) {
    ScrollbarV sb;
    if (thickness <= 0.f) {
        return sb;
    }
    const OverflowMode mode_y = used_viewport_overflow(root, true);
    if (mode_y == OverflowMode::Hidden || mode_y == OverflowMode::Clip) {
        return sb;
    }
    if (root.geo.scroll_height <= viewport_h + 0.5f) {
        return sb;
    }
    const OverflowMode mode_x = used_viewport_overflow(root, false);
    const bool bars_x = mode_x != OverflowMode::Hidden && mode_x != OverflowMode::Clip;
    const bool horizontal = bars_x && root.geo.scroll_width > viewport_w + 0.5f;
    const float track_h = std::max(0.f, viewport_h - (horizontal ? thickness : 0.f));
    sb.visible = true;
    sb.track_w = thickness;
    sb.track_h = track_h;
    sb.track_x = viewport_w - thickness;
    sb.track_y = 0.f;
    const float content = std::max(root.geo.scroll_height, 1.f);
    const float ratio = std::min(1.f, track_h / content);
    sb.thumb_h = std::max(16.f, track_h * ratio);
    sb.max_scroll = std::max(0.f, root.geo.scroll_height - track_h);
    const float scroll = root.node != nullptr ? root.node->scroll_y : 0.f;
    const float t = sb.max_scroll > 0.f ? std::min(1.f, scroll / sb.max_scroll) : 0.f;
    sb.thumb_y = sb.track_y + t * (track_h - sb.thumb_h);
    return sb;
}

}  // namespace layout
}  // namespace zui
