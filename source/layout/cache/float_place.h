#ifndef ZUI_LAYOUT_CACHE_FLOAT_PLACE_H
#define ZUI_LAYOUT_CACHE_FLOAT_PLACE_H

#include "layout/cache/exclusion_space.h"
#include "layout/cache/layout_result.h"
#include "layout/clip_path.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace zui {
namespace layout {

/// CSS 2.1 section 9.5: where a float goes and how it shortens the lines that follow it. The block
/// flow and the multi-column container place floats the same way - a column is a formatting context
/// of its own (CSS Multi-column Layout section 7), so a float shortens the lines that follow it in
/// that column and nothing else - and both use this arithmetic.

inline bool clears(ClearSide side, FloatSide float_side) {
    if (side == ClearSide::Both) {
        return true;
    }
    if (side == ClearSide::Left) {
        return float_side == FloatSide::Left;
    }
    if (side == ClearSide::Right) {
        return float_side == FloatSide::Right;
    }
    return false;
}

/// The block position `clear` moves the box down to.
inline float cleared_block(const ExclusionSpace& exclusion, ClearSide side) {
    float bottom = 0;
    if (side == ClearSide::None) {
        return bottom;
    }
    for (const FloatRecord& band : exclusion.bands()) {
        if (clears(side, band.side)) {
            bottom = std::max(bottom, band.margin_box.y + band.margin_box.h);
        }
    }
    return bottom;
}

/// Bind CSS Shapes `shape-outside` / `shape-margin` onto a placed float. Float-float collision stays
/// rectangular; only line/BFC exclusion reads the shape.
inline void attach_shape_outside(FloatRecord& record, std::string_view shape_css,
                                 std::string_view margin_css) {
    record.shape = ClipPathShape{};
    record.shape_margin = 0;
    if (shape_css.empty() || shape_css == "none") {
        return;
    }
    ClipPathShape shape;
    if (!parse_shape_outside(shape_css, record.margin_box.w, record.margin_box.h, shape)) {
        return;
    }
    record.shape = shape;
    if (!margin_css.empty() && margin_css != "none") {
        char* end = nullptr;
        const std::string tmp(margin_css);
        const float v = std::strtof(tmp.c_str(), &end);
        if (end != tmp.c_str()) {
            std::string_view unit(end);
            while (!unit.empty() &&
                   std::isspace(static_cast<unsigned char>(unit.front()))) {
                unit.remove_prefix(1);
            }
            if (unit.empty() || unit == "px") {
                record.shape_margin = std::max(0.f, v);
            } else if (unit == "%") {
                const float basis =
                    std::sqrt((record.margin_box.w * record.margin_box.w +
                               record.margin_box.h * record.margin_box.h) *
                              0.5f);
                record.shape_margin = std::max(0.f, basis * v / 100.f);
            }
        }
    }
}

/// The inline range a box at `[y0, y1)` can use: the floats overlapping that band take their space
/// off the side they belong to. With `shape-outside`, only the shape contour excludes (CSS Shapes).
inline void float_limits(const ExclusionSpace& exclusion, float y0, float y1, float content_inline,
                         float& left, float& right) {
    left = 0;
    right = std::max(0.f, content_inline);
    if (y1 < y0) {
        y1 = y0;
    }
    for (const FloatRecord& band : exclusion.bands()) {
        const float top = band.margin_box.y;
        const float bottom = top + band.margin_box.h;
        if (bottom <= y0 || top >= y1) {
            continue;
        }
        if (band.shape.kind != ClipPathShape::Kind::None) {
            const float local_y0 = std::max(y0, top) - band.margin_box.y;
            const float local_y1 = std::min(y1, bottom) - band.margin_box.y;
            float shape_left = 0;
            float shape_right = 0;
            if (!shape_inline_extent(band.shape, band.margin_box.w, band.margin_box.h, local_y0,
                                     local_y1, band.shape_margin, shape_left, shape_right)) {
                continue;
            }
            if (band.side == FloatSide::Left) {
                left = std::max(left, band.margin_box.x + shape_right);
            } else {
                right = std::min(right, band.margin_box.x + shape_left);
            }
            continue;
        }
        if (band.side == FloatSide::Left) {
            left = std::max(left, band.margin_box.x + band.margin_box.w);
        } else {
            right = std::min(right, band.margin_box.x);
        }
    }
    if (right < left) {
        right = left;
    }
}
/// Place one float as high as it can go in `content_inline` without overlapping the floats already
/// in `exclusion`, starting at `y`. The returned margin box is in the same coordinates as the
/// exclusion space.
inline FloatRecord place_float(const LayoutResult& result, FloatSide side, float y,
                               float content_inline, const ExclusionSpace& exclusion,
                               bool pending) {
    const float margin_inline =
        result.geometry.margin.inline_start + result.geometry.margin.inline_end;
    const float margin_block = result.geometry.margin.block_start + result.geometry.margin.block_end;
    const float width = std::max(0.f, result.geometry.border_box_inline + margin_inline);
    const float height = std::max(0.f, result.geometry.border_box_block + margin_block);
    const float start = std::max(0.f, y);
    std::vector<float> candidates;
    candidates.push_back(start);
    for (const FloatRecord& band : exclusion.bands()) {
        const float bottom = band.margin_box.y + band.margin_box.h;
        if (bottom >= start - 0.01f) {
            candidates.push_back(std::max(start, bottom));
        }
    }
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end(),
                                 [](float a, float b) { return std::abs(a - b) < 0.01f; }),
                     candidates.end());
    bool found = false;
    float best_x = 0;
    float best_y = start;
    for (float top : candidates) {
        float left = 0;
        float right = content_inline;
        float_limits(exclusion, top, top + std::max(height, 0.01f), content_inline, left, right);
        if (left + width > right + 0.01f) {
            continue;
        }
        const float x = side == FloatSide::Right ? right - width : left;
        if (!found || top < best_y - 0.01f || (std::abs(top - best_y) <= 0.01f && x < best_x)) {
            found = true;
            best_x = std::max(0.f, x);
            best_y = top;
        }
    }
    if (!found) {
        best_y = candidates.empty() ? start : candidates.back();
        best_x = side == FloatSide::Right ? std::max(0.f, content_inline - width) : 0;
    }
    FloatRecord record;
    record.side = side;
    record.pending = pending;
    record.margin_box = {best_x, best_y, width, height};
    return record;
}

}  // namespace layout
}  // namespace zui

#endif
