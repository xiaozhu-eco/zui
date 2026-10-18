#include "layout/world_placement.h"

#include "input/interaction.h"
#include "layout/paint_map.h"

namespace zui {
namespace layout {

void WorldPlacement::place(PaintMapSession& session, Box& root, float viewport_w,
                           float viewport_h) const {
    // Scroll anchoring runs on content-box placements restored before this call; if an
    // adjustment moves a scroller, map_paint must see the new scroll offsets.
    (void)input::adjust_scroll_anchors(root);
    map_paint(session, root, viewport_w, viewport_h);
}

void WorldPlacement::place_dirty(PaintMapSession& session, Box& root, float viewport_w,
                                 float viewport_h) const {
    // The anchoring pass can move a scroller of its own accord; it marks the box it moved, and
    // those marks are consumed by the walk that follows.
    (void)input::adjust_scroll_anchors(root);
    map_paint_dirty(session, root, viewport_w, viewport_h);
}

void WorldPlacement::place_all(PaintMapSession& session, Box& root, float viewport_w,
                               float viewport_h) const {
    (void)input::adjust_scroll_anchors(root);
    map_paint_every_box(session, root, viewport_w, viewport_h);
}

}  // namespace layout
}  // namespace zui
