#ifndef ZUI_LAYOUT_WORLD_PLACEMENT_H
#define ZUI_LAYOUT_WORLD_PLACEMENT_H

#include "layout/box.h"

namespace zui {
namespace layout {

struct PaintMapSession;

class WorldPlacement {
public:
    /// LAYOUT.md section 9 step B: `viewport_w` x `viewport_h` is the rectangle the culling test
    /// uses. A zero viewport keeps every box, which is what a caller with no window wants.
    void place(PaintMapSession& session, Box& root, float viewport_w, float viewport_h) const;
    /// docs/PERF_REFACTOR.md stage P: the same placement, but only the subtrees `mark_place_dirty`
    /// marked are walked. Callers that cannot account for every placement input that moved use
    /// `place` instead; this one is only sound when the marks are complete.
    void place_dirty(PaintMapSession& session, Box& root, float viewport_w, float viewport_h) const;
    /// Every box gets world geometry, culled or not. The observation seam (`frame::Access` and the
    /// layout unit tests) reads `getBoundingClientRect`-like rectangles for boxes below the fold,
    /// which the clipped walk never visits. `viewport_w` x `viewport_h` is stored as the visible
    /// rectangle for the paint that follows; it does not cull. Pass zero when there is no frame.
    void place_all(PaintMapSession& session, Box& root, float viewport_w, float viewport_h) const;
};

}  // namespace layout
}  // namespace zui

#endif
