#ifndef ZUI_LAYOUT_PAINT_MAP_H
#define ZUI_LAYOUT_PAINT_MAP_H

#include "layout/cache/rect.h"

#include <cstddef>

namespace zui {
namespace layout {

struct Box;

/// LAYOUT.md section 9 step B maps a window that is the viewport grown by this many pixels on each
/// side, and step D's retained listing covers the same window. The margin is what lets a scroll
/// through it replay a listing instead of re-recording one: the geometry a listing was recorded
/// with is still there when the scroll moves by less than this. A jump past it costs one ordinary
/// frame, which is also what a frame that changed anything costs.
constexpr float kRetainedMargin = 192.f;

/// Counters for LAYOUT.md section 9 step B. `mapped` is how many boxes the walk wrote world
/// coordinates for, `culled` how many subtrees it skipped whole. They are the observation channel
/// the scaling check reads: a scroll frame's cost has to follow `mapped`, not the tree. Both count
/// the walk's window, which is the viewport plus `kRetainedMargin`; `Geometry::culled` is the
/// flag painting and hit testing gate on, and it is still measured against the visible rectangle.
struct PaintStats {
    std::size_t mapped = 0;
    std::size_t culled = 0;
};

/// Viewport, cull, and counters for one layout session's paint map. The storage lives on
/// `LayoutEngine`; `map_paint` writes the session it is given.
struct PaintMapSession {
    Rect visible{};
    Rect window{};
    bool cull = true;
    PaintStats stats{};
    bool partial = false;
    std::uint64_t place_gen = 0;
    bool cull_wiped = false;
};

PaintStats paint_stats();

/// True when the last paint map parked a culled descendant at the sentinel because that
/// descendant had never been given a rectangle. A walk that only translated descendants
/// already published by an earlier map leaves this false: their border boxes are current,
/// and `Geometry::culled` on the subtree root still keeps paint and hit testing out.
bool paint_cull_incomplete();

/// The visible rectangle the last mapping used. A retained listing walks a window that is that
/// rectangle grown by `kRetainedMargin`, so anything that paints has to know the geometry it is
/// about to read was mapped against the same window - the painter will not record a listing
/// otherwise. Empty only when the caller passed a zero viewport (a box tree built with no window).
Rect paint_viewport();

// Fills Box::geo from the pinned layout result, placement, scroll, and transform.
// LayoutResult and Placement stay free of world coordinates.
//
// LAYOUT.md section 9 step B: a subtree that cannot reach the visible rectangle is skipped whole -
// no world coordinates, no glyph runs, and `Geometry::culled` set so the painter and the hit tester
// leave it alone. The rectangle is `viewport_w` x `viewport_h` at the origin.
void map_paint(PaintMapSession& session, Box& root, float viewport_w, float viewport_h);

// docs/PERF_REFACTOR.md stage P: the same walk, but only the subtrees the last frame marked with
// `mark_place_dirty` are mapped. A box whose own placement inputs and whose ancestors' did not
// change keeps the world geometry (and the `Frame` the walk published for it) from the last walk,
// so a scroll or a local layout does not pay for the whole page. The caller may only use this when
// it knows the flags cover every placement input that moved; `map_paint` remains the fallback for
// a first frame, a viewport change, and any frame the caller cannot account for.
void map_paint_dirty(PaintMapSession& session, Box& root, float viewport_w, float viewport_h);

// The same walk with the visibility test off: every box gets its world coordinates. `viewport_w`
// x `viewport_h` is still stored as the visible rectangle so the paint that follows can record a
// retained listing against that window. It is not a cull window: a zero size (the layout tests
// and the trace export, which have no frame) leaves the rectangle empty and still publishes every
// box. Culling stays off either way, so a box below the fold keeps a rectangle observation can read.
void map_paint_every_box(PaintMapSession& session, Box& root, float viewport_w = 0.f,
                         float viewport_h = 0.f);
/// Point `paint_viewport` / `paint_stats` at this session without walking. The frame publishes
/// the layout it is about to paint, including when that layout did not remap this frame.
void publish_paint_map(PaintMapSession& session);

// Hit test in the same coordinate walk. A singular transform misses the subtree.
Box* hit_mapped(Box* root, float x, float y);

}  // namespace layout
}  // namespace zui

#endif
