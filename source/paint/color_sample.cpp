#include "paint/color_internal.h"
#include "paint/display_list.h"
#include "paint/painter_internal.h"

#include "ascii.h"
#include "css/values.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace zui {
namespace paint {
using namespace color_detail;

bool gradient_image_for_box(std::string_view css, Rect area, Gradient& out,
                            const layout::Box* color_box) {
    css = trim(css);
    if (linear_gradient_for_box(css, area, out, color_box)) {
        return true;
    }
    if (repeating_linear_gradient_for_box(css, area, out, color_box)) {
        return true;
    }
    if (radial_gradient_for_box(css, area, out, color_box)) {
        return true;
    }
    if (repeating_radial_gradient_for_box(css, area, out, color_box)) {
        return true;
    }
    if (conic_gradient_for_box(css, area, out, color_box)) {
        return true;
    }
    return repeating_conic_gradient_for_box(css, area, out, color_box);
}

Color gradient_color_at(const Gradient& g, float x, float y) {
    float t = 0.f;
    if (g.kind == GradientKind::Conic) {
        // CSS: 0deg is up, clockwise. atan2(dx, -dy) is that angle in y-down device space.
        const float ang = std::atan2(x - g.x, -(y - g.y));
        t = (ang - g.angle) / (2.f * kPi);
        t -= std::floor(t);
    } else if (g.kind == GradientKind::Radial) {
        const float dx = x - g.x;
        const float dy = y - g.y;
        if (g.rx > 0.f && g.ry > 0.f) {
            t = std::sqrt((dx * dx) / (g.rx * g.rx) + (dy * dy) / (g.ry * g.ry));
        }
    } else {
        t = g.len > 0.0f ? ((x - g.x) * g.dx + (y - g.y) * g.dy) / g.len : 0.0f;
    }
    return lerp_stops(g, t);
}

void gradient_gpu_chunks(const Gradient& g, std::vector<GradientGpuChunk>& out) {
    out.clear();
    if (g.stops.empty()) {
        return;
    }
    auto pack = [](const GradientStop* begin, int n, bool clamp, float stroke_flag) {
        GradientGpuChunk chunk;
        chunk.grad_count = clamp ? -n : n;
        chunk.stroke_flag = stroke_flag;
        for (int i = 0; i < n; ++i) {
            chunk.off[i] = begin[i].offset;
            chunk.col[i * 4 + 0] = static_cast<float>(begin[i].color.r) / 255.f;
            chunk.col[i * 4 + 1] = static_cast<float>(begin[i].color.g) / 255.f;
            chunk.col[i * 4 + 2] = static_cast<float>(begin[i].color.b) / 255.f;
            chunk.col[i * 4 + 3] = static_cast<float>(begin[i].color.a) / 255.f;
        }
        return chunk;
    };
    const int total = static_cast<int>(g.stops.size());
    if (total <= 4) {
        out.push_back(pack(g.stops.data(), total, false, 0.f));
        return;
    }
    // Overlap on the shared boundary stop so each window is continuous; clamp keeps windows
    // from painting outside their t range (first: upper only, last: lower only, middle: both).
    for (int start = 0; start < total - 1;) {
        const int end = std::min(start + 3, total - 1);
        const int n = end - start + 1;
        const bool first = start == 0;
        const bool last = end == total - 1;
        const float flag = first ? -2.f : (last ? -1.f : 0.f);
        out.push_back(pack(g.stops.data() + start, n, true, flag));
        if (last) {
            break;
        }
        start = end;
    }
}

}  // namespace paint
}  // namespace zui
