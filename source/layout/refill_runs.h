#ifndef ZUI_LAYOUT_REFILL_RUNS_H
#define ZUI_LAYOUT_REFILL_RUNS_H

// Wave 1 of docs/RFC_MODEL.md. A `set_model` that keeps a row keeps that row's boxes, and the
// placement walk clears every box's `geo.runs`. For a box whose characters did not change and
// that holds a single line, the run can be rebuilt from the box's own text instead of laying the
// subtree out again.
//
// The conditions live in `can_refill_runs` alone; `LayoutEngine::refill_runs` walks a model
// host's box subtree and calls both. Nothing here reads a box's pinned line boxes: on the frame
// that needs the refill they are exactly what is missing.

#include <cstddef>

namespace zui {
namespace layout {

struct Box;
class LayoutContext;

/// The five conditions of docs/RFC_MODEL.md section 6: `geo.runs` empty with the geometry still
/// there, not painted by the parent's line, not an inline box whose text an ancestor line owns,
/// one text with no content of its own, and a single line at this box's own metric. A generated
/// (`::placeholder`) run is not one of these boxes.
bool can_refill_runs(const Box& box, LayoutContext& cx);

/// Writes `box.geo.runs` for the line `can_refill_runs` accepted, using the same per-glyph
/// advances and the same logical-to-physical conversion `paint_map.cpp`'s `fill_geometry` uses.
/// Returns the number of runs written. Call only when `can_refill_runs(box, cx)` is true.
std::size_t refill_runs(Box& box, LayoutContext& cx);

}  // namespace layout
}  // namespace zui

#endif
