#ifndef ZUI_LAYOUT_CACHE_STICKY_H
#define ZUI_LAYOUT_CACHE_STICKY_H

#include "layout/cache/axis_size.h"
#include "layout/cache/logical_axes.h"
#include "layout/cache/logical_point.h"
#include "layout/cache/used_length.h"
#include "layout/layout_style.h"

namespace zui {
namespace layout {

/// Sticky shift against the nearest scrollport. Origins and sizes are in that scrollport's
/// **logical** padding-box coordinates. Physical insets (`top`/`right`/`bottom`/`left`) map
/// through the sticky box's writing-mode; scroll offsets must already be logical
/// (`scroll_x`/`scroll_y` remapped by the caller). When both ends of a logical axis are set, the
/// start side wins. `cb_origin_in_scrollport` / `cb_*` clamp the shift inside the sticky
/// containing block.
inline LogicalPoint sticky_shift(const LayoutStyle& style, LogicalPoint origin_in_scrollport,
                                 float child_inline, float child_block, float scrollport_inline,
                                 float scrollport_block, float scroll_inline, float scroll_block,
                                 LogicalPoint cb_origin_in_scrollport, float cb_inline,
                                 float cb_block) {
    LogicalPoint shift;
    if (!style.position_sticky) {
        return shift;
    }
    const WritingMode mode = style.writing_mode;
    const Direction direction = style.direction;
    const PhysicalSize scrollport_phys =
        logical_size_to_physical(mode, scrollport_inline, scrollport_block);
    const AxisSize width_basis{true, scrollport_phys.width};
    const AxisSize height_basis{true, scrollport_phys.height};

    bool top_auto = style.top_auto;
    bool right_auto = style.right_auto;
    bool bottom_auto = style.bottom_auto;
    bool left_auto = style.left_auto;
    const float top =
        top_auto ? 0.f : used_edge(style.top_px, style.top_percent, height_basis);
    const float right =
        right_auto ? 0.f : used_edge(style.right_px, style.right_percent, width_basis);
    const float bottom =
        bottom_auto ? 0.f : used_edge(style.bottom_px, style.bottom_percent, height_basis);
    const float left =
        left_auto ? 0.f : used_edge(style.left_px, style.left_percent, width_basis);

    bool inline_start_auto = true;
    bool inline_end_auto = true;
    bool block_start_auto = true;
    bool block_end_auto = true;
    {
        Edges flags;
        set_logical_edge(flags, mode, direction, top_auto ? 1.f : 0.f, right_auto ? 1.f : 0.f,
                         bottom_auto ? 1.f : 0.f, left_auto ? 1.f : 0.f);
        inline_start_auto = flags.inline_start > 0.f;
        inline_end_auto = flags.inline_end > 0.f;
        block_start_auto = flags.block_start > 0.f;
        block_end_auto = flags.block_end > 0.f;
    }
    const Edges insets = physical_edges_to_logical(mode, direction, top, right, bottom, left);

    if (!block_start_auto || !block_end_auto) {
        float delta = 0.f;
        if (!block_start_auto) {
            delta = scroll_block + insets.block_start - origin_in_scrollport.block_offset;
        } else {
            delta = scroll_block + scrollport_block - insets.block_end - child_block -
                    origin_in_scrollport.block_offset;
        }
        if (delta < 0) {
            delta = 0;
        }
        float limit = cb_origin_in_scrollport.block_offset + cb_block - child_block -
                      origin_in_scrollport.block_offset;
        if (limit < 0) {
            limit = 0;
        }
        if (delta > limit) {
            delta = limit;
        }
        shift.block_offset = delta;
    }

    if (!inline_start_auto || !inline_end_auto) {
        float delta = 0.f;
        if (!inline_start_auto) {
            delta = scroll_inline + insets.inline_start - origin_in_scrollport.inline_offset;
        } else {
            delta = scroll_inline + scrollport_inline - insets.inline_end - child_inline -
                    origin_in_scrollport.inline_offset;
        }
        if (delta < 0) {
            delta = 0;
        }
        float limit = cb_origin_in_scrollport.inline_offset + cb_inline - child_inline -
                      origin_in_scrollport.inline_offset;
        if (limit < 0) {
            limit = 0;
        }
        if (delta > limit) {
            delta = limit;
        }
        shift.inline_offset = delta;
    }
    return shift;
}

/// Direct-child convenience: scroller padding box is both scrollport and containing block.
inline LogicalPoint sticky_shift(const LayoutStyle& style, LogicalPoint cb_offset, float child_inline,
                                 float child_block, float cb_inline, float cb_block,
                                 float scroll_inline, float scroll_block) {
    return sticky_shift(style, cb_offset, child_inline, child_block, cb_inline, cb_block,
                        scroll_inline, scroll_block, LogicalPoint{}, cb_inline, cb_block);
}

}  // namespace layout
}  // namespace zui

#endif
