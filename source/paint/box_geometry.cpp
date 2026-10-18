#include "paint/painter_internal.h"

#include "paint/display_list.h"

#include "perf/perf_budget.h"
#include "css/transition/transition.h"
#include "image/image.h"
#include "layout/clip_path.h"
#include "layout/overflow_clip.h"
#include "layout/paint_map.h"
#include "layout/paint_transform.h"
#include "layout/scrollbar_h.h"
#include "layout/scrollbar_v.h"
#include "layout/stacking.h"

#include "text/font_chain.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace zui {
namespace paint {
namespace painter_detail {
/// CSS Backgrounds 3 section 5.1: percentage radii refer to the corresponding dimension of the
/// border box (x→width, y→height). Adjacent corners that sum past a side are scaled by one factor
/// applied to all eight radii: horizontal sums on top/bottom, vertical sums on left/right.
CornerRadii resolve_border_radii(const layout::LayoutStyle& style, float width, float height) {
    auto corner_x = [&](float value, bool percent) {
        if (!percent) {
            return std::max(0.f, value);
        }
        return std::max(0.f, value * 0.01f * width);
    };
    auto corner_y = [&](float value, bool percent) {
        if (!percent) {
            return std::max(0.f, value);
        }
        return std::max(0.f, value * 0.01f * height);
    };
    float tl = corner_x(style.border_top_left_radius, style.border_top_left_radius_percent);
    float tr = corner_x(style.border_top_right_radius, style.border_top_right_radius_percent);
    float br = corner_x(style.border_bottom_right_radius, style.border_bottom_right_radius_percent);
    float bl = corner_x(style.border_bottom_left_radius, style.border_bottom_left_radius_percent);
    float tl_y = corner_y(style.border_top_left_radius_y, style.border_top_left_radius_y_percent);
    float tr_y = corner_y(style.border_top_right_radius_y, style.border_top_right_radius_y_percent);
    float br_y =
        corner_y(style.border_bottom_right_radius_y, style.border_bottom_right_radius_y_percent);
    float bl_y =
        corner_y(style.border_bottom_left_radius_y, style.border_bottom_left_radius_y_percent);
    auto side_factor = [](float a, float b, float len) {
        if (a + b > len && a + b > 0.f && len > 0.f) {
            return len / (a + b);
        }
        return 1.f;
    };
    float f = 1.f;
    f = std::min(f, side_factor(tl, tr, width));
    f = std::min(f, side_factor(tr_y, br_y, height));
    f = std::min(f, side_factor(br, bl, width));
    f = std::min(f, side_factor(bl_y, tl_y, height));
    return CornerRadii{tl * f, tr * f, br * f, bl * f, tl_y * f, tr_y * f, br_y * f, bl_y * f};
}

CornerRadii inset_radii(CornerRadii radii, float inset) {
    auto shrink = [&](float r) { return std::max(0.f, r - inset); };
    return CornerRadii{shrink(radii.tl),   shrink(radii.tr),   shrink(radii.br),
                       shrink(radii.bl),   shrink(radii.tl_y), shrink(radii.tr_y),
                       shrink(radii.br_y), shrink(radii.bl_y)};
}

/// CSS Backgrounds 3 §5.3: padding-edge radii shrink by the two adjacent border widths.
CornerRadii inset_radii_sides(CornerRadii radii, float top, float right, float bottom, float left) {
    return CornerRadii{std::max(0.f, radii.tl - left),     std::max(0.f, radii.tr - right),
                       std::max(0.f, radii.br - right),    std::max(0.f, radii.bl - left),
                       std::max(0.f, radii.tl_y - top),    std::max(0.f, radii.tr_y - top),
                       std::max(0.f, radii.br_y - bottom), std::max(0.f, radii.bl_y - bottom)};
}

Rect border_rect(const layout::Box& box, float dx, float dy) {
    return Rect{box.geo.world_x + dx, box.geo.world_y + dy, box.geo.width, box.geo.height};
}

Rect padding_rect(const layout::Box& box, float dx, float dy) {
    return Rect{box.geo.world_x + dx + box.geo.border.left,
                box.geo.world_y + dy + box.geo.border.top,
                std::max(0.0f, box.geo.width - box.geo.border.horizontal()),
                std::max(0.0f, box.geo.height - box.geo.border.vertical())};
}

Rect content_rect(const layout::Box& box, float dx, float dy) {
    return Rect{box.geo.world_x + dx + box.geo.border.left + box.geo.padding.left,
                box.geo.world_y + dy + box.geo.border.top + box.geo.padding.top,
                std::max(0.0f, box.geo.content_width), std::max(0.0f, box.geo.content_height)};
}
}  // namespace painter_detail
}  // namespace paint
}  // namespace zui
