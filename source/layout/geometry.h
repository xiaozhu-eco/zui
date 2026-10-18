#ifndef ZUI_LAYOUT_GEOMETRY_H
#define ZUI_LAYOUT_GEOMETRY_H

#include "layout/physical_edges.h"
#include "layout/text_run.h"

#include <vector>

namespace zui {
namespace layout {

/// One fragment of a box the layout split across fragmentainers, resolved to world coordinates
/// for paint, hit testing and the trace (LAYOUT.md section 12). `paint_dx` / `paint_dy` are the
/// offsets to add to the box's own world origin while painting this fragment, so that the slice
/// lines up with the fragmentainer.
struct FragmentBox {
    float world_x = 0;
    float world_y = 0;
    float width = 0;
    float height = 0;
    float paint_dx = 0;
    float paint_dy = 0;
    // Where this fragment starts inside the box's own block axis. A line box belongs to the
    // fragment whose slice contains it, and that is how the trace reports the line's position.
    float block_offset = 0;
    // Strip coverage for descendant slicing (LAYOUT.md section 12). Equals `height` except when
    // a fragment was opened for a float that outruns the box's own content: `height` is clipped
    // to the content box (Chrome's client rect, possibly 0) while this keeps the column band so
    // a taller float still receives one client rect per column.
    float coverage_height = 0;
};

// Paint snapshot. World coordinates are filled after layout and are not a cache key.
struct Geometry {
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;
    float content_width = 0;
    float content_height = 0;
    // Browser-like scrollable extent (`scrollWidth` / `scrollHeight`): the padding box grown to
    // the scrollable overflow. Filled from LayoutResult::overflow.scrollable (LAYOUT.md section 4)
    // and kept apart from content_width/height, which describe the content box itself.
    float scroll_width = 0;
    float scroll_height = 0;
    PhysicalEdges margin;
    PhysicalEdges border;
    PhysicalEdges padding;
    float baseline = 0;
    float world_x = 0;
    float world_y = 0;
    /// Axis-aligned bounds of the border box after `transform` (CSS getBoundingClientRect).
    /// Equal to `(world_x, world_y, width, height)` when the paint transform is a pure translate.
    float aabb_x = 0;
    float aabb_y = 0;
    float aabb_w = 0;
    float aabb_h = 0;
    float scroll_x = 0;
    float scroll_y = 0;
    std::vector<TextRun> runs;
    // Empty when the box fits in one fragment. When it does not, this holds every fragment in
    // order and `union_*` is the border-box union over them, which is what a browser reports for
    // `getBoundingClientRect` on a fragmented element.
    std::vector<FragmentBox> fragments;
    float union_x = 0;
    float union_y = 0;
    float union_w = 0;
    float union_h = 0;
    /// A box that does not hold fragments of its own but spans several fragments of an ancestor
    /// reports the same union; `fragments` stays empty because paint already clips it through that
    /// ancestor. Only the trace reads this.
    bool union_across = false;
    /// LAYOUT.md section 9 step B: this box's subtree was skipped because it cannot reach the
    /// visible rectangle. The geometry of a culled box is cleared instead of left at the previous
    /// frame's world coordinates, so neither the painter nor a hit can pick it up by accident.
    bool culled = false;
};

}  // namespace layout
}  // namespace zui

#endif
