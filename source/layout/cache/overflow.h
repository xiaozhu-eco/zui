#ifndef ZUI_LAYOUT_CACHE_OVERFLOW_H
#define ZUI_LAYOUT_CACHE_OVERFLOW_H

#include "layout/cache/edges.h"
#include "layout/cache/logical_axes.h"
#include "layout/cache/logical_point.h"
#include "layout/cache/rect.h"
#include "layout/overflow_mode.h"

#include <algorithm>

namespace zui {
namespace layout {

// Used overflow rectangles. The CSS overflow keywords live in OverflowMode.
struct Overflow {
    Rect ink;
    Rect scrollable;
    bool clipped_by_paint_contain = false;
};

/// `Rect{0, 0, 0, 0}` is the "nothing collected yet" value, not a rectangle at the origin: keeping
/// it in a union would drag every other rectangle to (0, 0). `LAYOUT.md` section 4 lets an overflow
/// rectangle start away from its origin and go negative, so only a zero *area* can mean empty.
inline bool rect_empty(const Rect& r) { return r.w <= 0.f || r.h <= 0.f; }

inline void rect_include(Rect& into, const Rect& add) {
    if (rect_empty(add)) {
        return;
    }
    if (rect_empty(into)) {
        into = add;
        return;
    }
    const float x0 = std::min(into.x, add.x);
    const float y0 = std::min(into.y, add.y);
    const float x1 = std::max(into.x + into.w, add.x + add.w);
    const float y1 = std::max(into.y + into.h, add.y + add.h);
    into = Rect{x0, y0, x1 - x0, y1 - y0};
}

/// A box's ink includes its own border box by definition (`LAYOUT.md` section 4): the union starts
/// there and grows to whatever the layout put inside. A box whose formatter has not written the
/// rectangle yet reports its border box, which is also the honest answer for a leaf.
inline Rect ink_with_border_box(const Overflow& overflow, float border_box_inline,
                                float border_box_block) {
    Rect ink = overflow.ink;
    rect_include(ink, Rect{0.f, 0.f, border_box_inline, border_box_block});
    return ink;
}

/// A child's ink in its container's frame. `cb_offset` is relative to the containing block's
/// padding box, so the container's border edges turn it into a border-box origin; the child's own
/// ink is measured from *its* border box (`LAYOUT.md` section 4).
///
/// A child in an orthogonal writing mode reports its extent in its own axes, so the container
/// reads the magnitude through the same physical round trip the scrollable overflow uses
/// (`LAYOUT.md` section 3). Two flows in the same direction - the common case - map exactly; an
/// orthogonal child keeps its own border box as the base, which is the one input the round trip
/// can carry.
///
/// docs/PERF.md I5's relayout wall: one rectangle of a child, in the child's border-box coordinates,
/// clipped to that border box when the child is a scroll container. A box with `overflow` other
/// than `visible` paints only inside its own box, so what it clipped away is not something the
/// parent paints; letting it through would make every change inside the wall look like a change of
/// the parent's own rectangle. Used for the child's `ink` and for its `scrollable` extent.
///
/// The clip edge is the *border* box, one step looser than the padding box the painter clips to, so
/// this can only under-clip - it never drops ink the browser would paint.
inline Rect wall_clipped_rect(const Rect& ink, float child_border_inline, float child_border_block,
                             bool child_clips) {
    if (!child_clips || rect_empty(ink)) {
        return ink;
    }
    const float x0 = std::max(ink.x, 0.f);
    const float y0 = std::max(ink.y, 0.f);
    const float x1 = std::min(ink.x + ink.w, child_border_inline);
    const float y1 = std::min(ink.y + ink.h, child_border_block);
    return Rect{x0, y0, std::max(0.f, x1 - x0), std::max(0.f, y1 - y0)};
}

/// Whether a box with this pair of `overflow` keywords clips what it lays out, which is what makes
/// it a relayout wall (docs/PERF.md I5).
inline bool clips_overflow(OverflowMode inline_axis, OverflowMode block_axis) {
    return inline_axis != OverflowMode::Visible || block_axis != OverflowMode::Visible;
}

inline Rect child_ink_in_box(const Overflow& child_overflow, float child_border_inline,
                             float child_border_block, WritingMode parent_mode,
                             WritingMode child_mode, const Edges& border,
                             const LogicalPoint& cb_offset, bool child_clips) {
    Rect ink = ink_with_border_box(child_overflow, child_border_inline, child_border_block);
    ink = wall_clipped_rect(ink, child_border_inline, child_border_block, child_clips);
    if (axes_orthogonal(parent_mode, child_mode)) {
        const PhysicalSize physical = logical_size_to_physical(child_mode, ink.w, ink.h);
        const LogicalSize back =
            physical_size_to_logical(parent_mode, physical.width, physical.height);
        ink = Rect{0.f, 0.f, back.inline_size, back.block_size};
    }
    return Rect{border.inline_start + cb_offset.inline_offset + ink.x,
                border.block_start + cb_offset.block_offset + ink.y, ink.w, ink.h};
}

/// LAYOUT.md section 4 and section 9: `scrollWidth` / `scrollHeight` start from the client box
/// (the padding box with the reserved scrollbar gutters taken out) and grow to wherever the
/// content reaches. A block child's border box already ends where Chrome's `scrollWidth` ends;
/// the trailing inline padding is added only for a line box (`pad_inline_end`). The block axis
/// still keeps the scroll container's own end padding, which is what `scrollHeight` includes.
inline Rect scrollable_extent(float padding_inline_start, float padding_inline_end,
                              float padding_block_start, float padding_block_end,
                              float client_content_inline, float client_content_block,
                              float content_inline, float content_block, bool scroll_container,
                              bool pad_inline_end = true) {
    const float client_inline = padding_inline_start + client_content_inline + padding_inline_end;
    const float client_block = padding_block_start + client_content_block + padding_block_end;
    const float extent_inline =
        padding_inline_start + std::max(client_content_inline, content_inline) +
        (scroll_container && pad_inline_end ? padding_inline_end : 0.f);
    const float extent_block = padding_block_start +
                               std::max(client_content_block, content_block) +
                               (scroll_container ? padding_block_end : 0.f);
    return Rect{0.f, 0.f, std::max(client_inline, extent_inline),
                std::max(client_block, extent_block)};
}

}  // namespace layout
}  // namespace zui

#endif
