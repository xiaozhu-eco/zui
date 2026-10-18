#ifndef ZUI_LAYOUT_CLIP_PATH_H
#define ZUI_LAYOUT_CLIP_PATH_H

#include <string_view>
#include <utility>
#include <vector>

namespace zui {
namespace layout {

/// Resolved `clip-path` against a border-box sized `box_w` × `box_h` (CSS Masking 1).
/// Coordinates are local to the border box's top-left.
struct ClipPathShape {
    enum class Kind { None, Inset, Circle, Ellipse, Polygon };
    Kind kind = Kind::None;
    float inset_t = 0;
    float inset_r = 0;
    float inset_b = 0;
    float inset_l = 0;
    float inset_round = 0;
    float cx = 0;
    float cy = 0;
    float rx = 0;
    float ry = 0;
    /// `polygon()` vertices. `evenodd` is the CSS fill-rule; the initial value is nonzero.
    std::vector<std::pair<float, float>> points;
    bool evenodd = false;
};

/// Parse `none` / `inset()` / `circle()` / `ellipse()` / `polygon()`. Other functions return
/// false (no clip).
bool parse_clip_path(std::string_view css, float box_w, float box_h, ClipPathShape& out);

/// Local-border-box hit test against a resolved shape.
bool point_in_clip_path(const ClipPathShape& shape, float box_w, float box_h, float x, float y);

/// CSS Shapes: strip a trailing `<shape-box>` token from `shape-outside` and parse the basic shape.
/// `url()` / `polygon()` / bare box keywords return false (rectangular float area).
bool parse_shape_outside(std::string_view css, float box_w, float box_h, ClipPathShape& out);

/// Horizontal span of `shape` over local y ∈ [y0, y1), expanded by `shape_margin`.
/// Returns false when the band misses the shape.
bool shape_inline_extent(const ClipPathShape& shape, float box_w, float box_h, float y0, float y1,
                         float shape_margin, float& out_left, float& out_right);

}  // namespace layout
}  // namespace zui

#endif
