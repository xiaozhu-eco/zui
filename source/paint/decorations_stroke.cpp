#include "paint/painter_internal.h"

#include "paint/display_list.h"

#include "perf/perf_budget.h"
#include "css/transition/transition.h"
#include "image/image.h"
#include "log/log.h"
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

using namespace painter_detail;

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Blink StyledStrokeData::SelectBestDashGap for a closed contour. The gap is the one
// nearest the preferred gap that still tiles the path with a whole number of dashes.
float select_best_dash_gap(float stroke_length, float dash_length, float gap_length) {
    const float available = stroke_length;
    const float min_num_dashes = std::floor(available / (dash_length + gap_length));
    const float max_num_dashes = min_num_dashes + 1.f;
    if (min_num_dashes < 1.f) {
        return gap_length;
    }
    const float min_gap = (stroke_length - min_num_dashes * dash_length) / min_num_dashes;
    const float max_gap = (stroke_length - max_num_dashes * dash_length) / max_num_dashes;
    if (max_gap <= 0.f || std::fabs(min_gap - gap_length) < std::fabs(max_gap - gap_length)) {
        return min_gap;
    }
    return max_gap;
}

// Open contour (one border side). Gaps are one fewer than dashes, so the side
// starts and ends on a dash. Blink uses this when the inner corner radius is 0.
float select_best_dash_gap_open(float stroke_length, float dash_length, float gap_length) {
    const float available = stroke_length + gap_length;
    const float min_num_dashes = std::floor(available / (dash_length + gap_length));
    const float max_num_dashes = min_num_dashes + 1.f;
    const float min_num_gaps = min_num_dashes - 1.f;
    const float max_num_gaps = max_num_dashes - 1.f;
    if (min_num_gaps < 1.f) {
        return gap_length;
    }
    const float min_gap = (stroke_length - min_num_dashes * dash_length) / min_num_gaps;
    if (max_num_gaps < 1.f) {
        return min_gap;
    }
    const float max_gap = (stroke_length - max_num_dashes * dash_length) / max_num_gaps;
    if (max_gap <= 0.f || std::fabs(min_gap - gap_length) < std::fabs(max_gap - gap_length)) {
        return min_gap;
    }
    return max_gap;
}

struct CenterSeg {
    enum Kind { H, V, Arc } kind = H;
    float len = 0.f;
    float x0 = 0.f;
    float y0 = 0.f;
    float x1 = 0.f;
    float y1 = 0.f;
    float a0 = 0.f;
    float a1 = 0.f;
    float rx = 0.f;
    float ry = 0.f;
};

float ellipse_span_len(float rx, float ry, float a0, float a1) {
    if (rx <= 0.f || ry <= 0.f || a1 <= a0) {
        return 0.f;
    }
    if (std::fabs(rx - ry) < 0.05f) {
        return (a1 - a0) * rx;
    }
    constexpr int kSteps = 16;
    const float da = (a1 - a0) / static_cast<float>(kSteps);
    float sum = 0.f;
    for (int i = 0; i < kSteps; ++i) {
        const float a = a0 + (static_cast<float>(i) + 0.5f) * da;
        const float sx = rx * std::sin(a);
        const float cy = ry * std::cos(a);
        sum += std::sqrt(sx * sx + cy * cy) * da;
    }
    return sum;
}

void point_on_seg(const CenterSeg& seg, float local, float& x, float& y) {
    if (seg.kind == CenterSeg::H) {
        const float t = seg.len > 0.f ? local / seg.len : 0.f;
        x = seg.x0 + (seg.x1 - seg.x0) * t;
        y = seg.y0;
        return;
    }
    if (seg.kind == CenterSeg::V) {
        const float t = seg.len > 0.f ? local / seg.len : 0.f;
        x = seg.x0;
        y = seg.y0 + (seg.y1 - seg.y0) * t;
        return;
    }
    float lo = seg.a0;
    float hi = seg.a1;
    for (int i = 0; i < 14; ++i) {
        const float mid = 0.5f * (lo + hi);
        if (ellipse_span_len(seg.rx, seg.ry, seg.a0, mid) < local) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    const float a = 0.5f * (lo + hi);
    x = seg.x0 + seg.rx * std::cos(a);
    y = seg.y0 + seg.ry * std::sin(a);
}

void point_at_path(const std::vector<CenterSeg>& segs, float s, float& x, float& y) {
    float cursor = 0.f;
    for (std::size_t i = 0; i < segs.size(); ++i) {
        const CenterSeg& seg = segs[i];
        if (s <= cursor + seg.len || i + 1 == segs.size()) {
            const float local = std::max(0.f, std::min(seg.len, s - cursor));
            point_on_seg(seg, local, x, y);
            return;
        }
        cursor += seg.len;
    }
    x = 0.f;
    y = 0.f;
}

void paint_seg_span(DisplayList& out, const CenterSeg& seg, float local0, float local1,
                    float thickness, Color color) {
    if (local1 - local0 <= 0.05f) {
        return;
    }
    if (seg.kind == CenterSeg::Arc) {
        const float step = std::max(1.5f, thickness * 0.35f);
        for (float d = local0; d < local1; d += step) {
            float x = 0.f;
            float y = 0.f;
            point_on_seg(seg, d, x, y);
            out.fill_rect(Rect{x - thickness * 0.5f, y - thickness * 0.5f, thickness, thickness},
                          color, thickness * 0.5f);
        }
        float x = 0.f;
        float y = 0.f;
        point_on_seg(seg, local1, x, y);
        out.fill_rect(Rect{x - thickness * 0.5f, y - thickness * 0.5f, thickness, thickness},
                      color, thickness * 0.5f);
        return;
    }
    float x_a = 0.f;
    float y_a = 0.f;
    float x_b = 0.f;
    float y_b = 0.f;
    point_on_seg(seg, local0, x_a, y_a);
    point_on_seg(seg, local1, x_b, y_b);
    if (seg.kind == CenterSeg::H) {
        const float lo = std::min(x_a, x_b);
        const float hi = std::max(x_a, x_b);
        out.fill_rect(Rect{lo, seg.y0 - thickness * 0.5f, hi - lo, thickness}, color);
        return;
    }
    const float lo = std::min(y_a, y_b);
    const float hi = std::max(y_a, y_b);
    out.fill_rect(Rect{seg.x0 - thickness * 0.5f, lo, thickness, hi - lo}, color);
}

void paint_path_range(DisplayList& out, const std::vector<CenterSeg>& segs, float s0, float s1,
                      float thickness, Color color) {
    float cursor = 0.f;
    for (const CenterSeg& seg : segs) {
        const float next = cursor + seg.len;
        const float a = std::max(s0, cursor);
        const float b = std::min(s1, next);
        if (b - a > 0.05f) {
            paint_seg_span(out, seg, a - cursor, b - cursor, thickness, color);
        }
        cursor = next;
        if (cursor >= s1) {
            break;
        }
    }
}

void fit_pair(float& a, float& b, float span) {
    if (span <= 0.f) {
        a = 0.f;
        b = 0.f;
        return;
    }
    const float sum = a + b;
    if (sum > span) {
        a *= span / sum;
        b *= span / sum;
    }
}


}  // namespace

namespace painter_detail {

bool paint_rounded_centerline_stroke(DisplayList& out, Rect outer, CornerRadii outer_radii,
                                     float thickness, Color color, bool dotted) {
    if (thickness <= 0.5f || outer.w <= thickness || outer.h <= thickness ||
        color_is_transparent(color)) {
        return false;
    }
    const float half = thickness * 0.5f;
    const float left = outer.x + half;
    const float top = outer.y + half;
    const float right = outer.x + outer.w - half;
    const float bottom = outer.y + outer.h - half;
    if (right - left < 1.f || bottom - top < 1.f) {
        return false;
    }
    CornerRadii cr = inset_radii(outer_radii, half);
    fit_pair(cr.tl, cr.tr, right - left);
    fit_pair(cr.bl, cr.br, right - left);
    fit_pair(cr.tl_y, cr.bl_y, bottom - top);
    fit_pair(cr.tr_y, cr.br_y, bottom - top);

    std::vector<CenterSeg> segs;
    auto push_h = [&](float x0, float x1, float y) {
        const float len = std::fabs(x1 - x0);
        if (len <= 0.05f) {
            return;
        }
        CenterSeg seg;
        seg.kind = CenterSeg::H;
        seg.len = len;
        seg.x0 = x0;
        seg.y0 = y;
        seg.x1 = x1;
        seg.y1 = y;
        segs.push_back(seg);
    };
    auto push_v = [&](float y0, float y1, float x) {
        const float len = std::fabs(y1 - y0);
        if (len <= 0.05f) {
            return;
        }
        CenterSeg seg;
        seg.kind = CenterSeg::V;
        seg.len = len;
        seg.x0 = x;
        seg.y0 = y0;
        seg.x1 = x;
        seg.y1 = y1;
        segs.push_back(seg);
    };
    auto push_arc = [&](float cx, float cy, float rx, float ry, float a0, float a1) {
        if (rx <= 0.05f || ry <= 0.05f) {
            return;
        }
        const float len = ellipse_span_len(rx, ry, a0, a1);
        if (len <= 0.05f) {
            return;
        }
        CenterSeg seg;
        seg.kind = CenterSeg::Arc;
        seg.len = len;
        seg.x0 = cx;
        seg.y0 = cy;
        seg.a0 = a0;
        seg.a1 = a1;
        seg.rx = rx;
        seg.ry = ry;
        segs.push_back(seg);
    };

    // Clockwise from the top edge, just after the top-left corner. That is Skia rrect
    // start index 0, which PathBuilder::AddRoundedRect uses.
    push_h(left + cr.tl, right - cr.tr, top);
    push_arc(right - cr.tr, top + cr.tr_y, cr.tr, cr.tr_y, -kPi * 0.5f, 0.f);
    push_v(top + cr.tr_y, bottom - cr.br_y, right);
    push_arc(right - cr.br, bottom - cr.br_y, cr.br, cr.br_y, 0.f, kPi * 0.5f);
    push_h(right - cr.br, left + cr.bl, bottom);
    push_arc(left + cr.bl, bottom - cr.bl_y, cr.bl, cr.bl_y, kPi * 0.5f, kPi);
    push_v(bottom - cr.bl_y, top + cr.tl_y, left);
    push_arc(left + cr.tl, top + cr.tl_y, cr.tl, cr.tl_y, kPi, kPi * 1.5f);
    if (segs.empty()) {
        return false;
    }
    float path_len = 0.f;
    for (const CenterSeg& seg : segs) {
        path_len += seg.len;
    }

    const bool thin_dot = dotted && thickness <= 3.f;
    const bool as_dash = !dotted || thin_dot;
    float on = 0.f;
    float off = 0.f;
    bool round = false;
    bool solid = false;
    if (as_dash) {
        on = thickness;
        off = thickness;
        if (!dotted) {
            on *= thickness >= 3.f ? 2.f : 3.f;
            off *= thickness >= 3.f ? 1.f : 2.f;
        }
        if (path_len <= on * 2.f) {
            solid = true;
        } else {
            float two = 2.f * on + off + off;
            if (path_len <= two) {
                const float scale = path_len / two;
                on *= scale;
                off *= scale;
            } else if (!dotted) {
                off = select_best_dash_gap(path_len, on, off);
            }
        }
    } else {
        round = true;
        const float per = thickness * 2.f;
        if (path_len < per) {
            off = per;
        } else {
            const float gap = select_best_dash_gap(path_len, thickness, thickness);
            off = gap + thickness - 0.01f;
        }
        on = 0.f;
    }
    if (solid) {
        out.stroke_rect(outer, color, thickness, outer_radii);
        return true;
    }
    if (round) {
        auto dot_at = [&](float s) {
            float x = 0.f;
            float y = 0.f;
            point_at_path(segs, s, x, y);
            out.fill_rect(Rect{x - half, y - half, thickness, thickness}, color, half);
        };
        if (off >= path_len) {
            dot_at(0.f);
            return true;
        }
        const float limit = path_len - off * 0.5f;
        for (float s = 0.f; s < limit; s += off) {
            dot_at(s);
        }
        return true;
    }
    // A corner whose outer radius is at most the border width has no inner arc.
    // Edge dashes that side as an open stroke of the border-box length (dashes on
    // both ends, gap chosen to fit) and bends the end dash around the corner.
    // A positive inner radius stays on the closed centerline below.
    const bool inner_clamped = outer_radii.tl <= thickness || outer_radii.tr <= thickness ||
                               outer_radii.br <= thickness || outer_radii.bl <= thickness ||
                               outer_radii.tl_y <= thickness || outer_radii.tr_y <= thickness ||
                               outer_radii.br_y <= thickness || outer_radii.bl_y <= thickness;
    if (inner_clamped) {
        float side_on = thickness;
        float side_pref = thickness;
        if (!dotted) {
            side_on *= thickness >= 3.f ? 2.f : 3.f;
            side_pref *= thickness >= 3.f ? 1.f : 2.f;
        }
        auto paint_horizontal = [&](float y_outer, float length, float r0, float r1) {
            float son = side_on;
            float soff = side_pref;
            if (length <= son * 2.f) {
                out.fill_rect(Rect{outer.x, y_outer, length, thickness}, color);
                return;
            }
            const float two = 2.f * son + soff;
            if (length <= two) {
                const float scale = length / two;
                son *= scale;
                soff *= scale;
            } else if (!dotted) {
                soff = select_best_dash_gap_open(length, son, soff);
            }
            const float period = son + soff;
            if (period <= 0.5f) {
                return;
            }
            const int n = std::min(4096, static_cast<int>(std::ceil(length / period)) + 1);
            for (int i = 0; i < n; ++i) {
                const float s0 = static_cast<float>(i) * period;
                if (s0 >= length - 0.05f) {
                    break;
                }
                const float s1 = std::min(length, s0 + son);
                const float a = std::max(s0, r0);
                const float b = std::min(s1, length - r1);
                if (b - a > 0.05f) {
                    out.fill_rect(Rect{outer.x + a, y_outer, b - a, thickness}, color);
                }
            }
        };
        auto paint_vertical = [&](float x_outer, float length, float r0, float r1) {
            float son = side_on;
            float soff = side_pref;
            if (length <= son * 2.f) {
                out.fill_rect(Rect{x_outer, outer.y, thickness, length}, color);
                return;
            }
            const float two = 2.f * son + soff;
            if (length <= two) {
                const float scale = length / two;
                son *= scale;
                soff *= scale;
            } else if (!dotted) {
                soff = select_best_dash_gap_open(length, son, soff);
            }
            const float period = son + soff;
            if (period <= 0.5f) {
                return;
            }
            const int n = std::min(4096, static_cast<int>(std::ceil(length / period)) + 1);
            for (int i = 0; i < n; ++i) {
                const float s0 = static_cast<float>(i) * period;
                if (s0 >= length - 0.05f) {
                    break;
                }
                const float s1 = std::min(length, s0 + son);
                const float a = std::max(s0, r0);
                const float b = std::min(s1, length - r1);
                if (b - a > 0.05f) {
                    out.fill_rect(Rect{x_outer, outer.y + a, thickness, b - a}, color);
                }
            }
        };
        paint_horizontal(outer.y, outer.w, outer_radii.tl, outer_radii.tr);
        paint_horizontal(outer.y + outer.h - thickness, outer.w, outer_radii.bl, outer_radii.br);
        paint_vertical(outer.x, outer.h, outer_radii.tl_y, outer_radii.bl_y);
        paint_vertical(outer.x + outer.w - thickness, outer.h, outer_radii.tr_y, outer_radii.br_y);
        for (const CenterSeg& seg : segs) {
            if (seg.kind == CenterSeg::Arc) {
                paint_seg_span(out, seg, 0.f, seg.len, thickness, color);
            }
        }
        return true;
    }
    const float period = on + off;
    if (period <= 0.5f) {
        return false;
    }
    const int n = std::min(4096, static_cast<int>(std::ceil(path_len / period)) + 1);
    for (int i = 0; i < n; ++i) {
        const float s0 = static_cast<float>(i) * period;
        if (s0 >= path_len - 0.05f) {
            break;
        }
        const float s1 = std::min(path_len, s0 + on);
        paint_path_range(out, segs, s0, s1, thickness, color);
    }
    return true;
}

}  // namespace painter_detail
}  // namespace paint
}  // namespace zui
