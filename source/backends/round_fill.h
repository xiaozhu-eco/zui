#ifndef ZUI_BACKENDS_ROUND_FILL_H
#define ZUI_BACKENDS_ROUND_FILL_H

#include <algorithm>
#include <cmath>

namespace zui {
namespace backends {

template <typename Tri>
void fill_round_rect(float x, float y, float w, float h, float rad, Tri&& tri) {
    auto rect = [&](float x0, float y0, float x1, float y1) {
        tri(x0, y0, x1, y0, x1, y1);
        tri(x0, y0, x1, y1, x0, y1);
    };
    rad = std::min(rad, std::min(w, h) * 0.5f);
    if (rad <= 0.5f) {
        rect(x, y, x + w, y + h);
        return;
    }
    const float x0 = x;
    const float y0 = y;
    const float x1 = x + w;
    const float y1 = y + h;
    rect(x0 + rad, y0, x1 - rad, y1);
    rect(x0, y0 + rad, x0 + rad, y1 - rad);
    rect(x1 - rad, y0 + rad, x1, y1 - rad);
    const int n = 12;
    const float pi = 3.14159265f;
    auto corner = [&](float cx, float cy, float a0) {
        for (int i = 0; i < n; ++i) {
            const float t0 = a0 + (pi * 0.5f) * (static_cast<float>(i) / static_cast<float>(n));
            const float t1 = a0 + (pi * 0.5f) * (static_cast<float>(i + 1) / static_cast<float>(n));
            tri(cx, cy, cx + std::cos(t0) * rad, cy + std::sin(t0) * rad, cx + std::cos(t1) * rad,
                cy + std::sin(t1) * rad);
        }
    };
    corner(x1 - rad, y0 + rad, -pi * 0.5f);
    corner(x0 + rad, y0 + rad, pi);
    corner(x0 + rad, y1 - rad, pi * 0.5f);
    corner(x1 - rad, y1 - rad, 0);
}

/// Border ring. Square strokes overlap at the corners, matching the previous quads.
/// Rounded strokes share a corner center for the outer and inner arcs.
template <typename Tri>
void stroke_round_rect(float x, float y, float w, float h, float rad, float thickness, Tri&& tri) {
    auto rect = [&](float x0, float y0, float x1, float y1) {
        if (x1 <= x0 || y1 <= y0) {
            return;
        }
        tri(x0, y0, x1, y0, x1, y1);
        tri(x0, y0, x1, y1, x0, y1);
    };
    if (w <= 0 || h <= 0 || thickness <= 0) {
        return;
    }
    const float t = std::min(thickness, std::min(w, h) * 0.5f);
    rad = std::min(rad, std::min(w, h) * 0.5f);
    if (rad <= 0.5f) {
        rect(x, y, x + w, y + t);
        rect(x, y + h - t, x + w, y + h);
        rect(x, y, x + t, y + h);
        rect(x + w - t, y, x + w, y + h);
        return;
    }
    const float x0 = x;
    const float y0 = y;
    const float x1 = x + w;
    const float y1 = y + h;
    rect(x0 + rad, y0, x1 - rad, y0 + t);
    rect(x0 + rad, y1 - t, x1 - rad, y1);
    rect(x0, y0 + rad, x0 + t, y1 - rad);
    rect(x1 - t, y0 + rad, x1, y1 - rad);
    const float ri = std::max(0.0f, rad - t);
    const int n = 12;
    const float pi = 3.14159265f;
    auto corner = [&](float cx, float cy, float a0) {
        for (int i = 0; i < n; ++i) {
            const float t0 = a0 + (pi * 0.5f) * (static_cast<float>(i) / static_cast<float>(n));
            const float t1 = a0 + (pi * 0.5f) * (static_cast<float>(i + 1) / static_cast<float>(n));
            const float ox0 = cx + std::cos(t0) * rad;
            const float oy0 = cy + std::sin(t0) * rad;
            const float ox1 = cx + std::cos(t1) * rad;
            const float oy1 = cy + std::sin(t1) * rad;
            const float ix0 = cx + std::cos(t0) * ri;
            const float iy0 = cy + std::sin(t0) * ri;
            const float ix1 = cx + std::cos(t1) * ri;
            const float iy1 = cy + std::sin(t1) * ri;
            tri(ox0, oy0, ox1, oy1, ix1, iy1);
            tri(ox0, oy0, ix1, iy1, ix0, iy0);
        }
    };
    corner(x1 - rad, y0 + rad, -pi * 0.5f);
    corner(x0 + rad, y0 + rad, pi);
    corner(x0 + rad, y1 - rad, pi * 0.5f);
    corner(x1 - rad, y1 - rad, 0);
}

}  // namespace backends
}  // namespace zui

#endif
