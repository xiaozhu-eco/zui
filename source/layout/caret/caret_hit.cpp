#include "layout/caret/caret_service.h"
#include "layout/caret/caret_internal.h"

#include "layout/box.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace zui {
namespace layout {

using namespace caret_detail;

CaretHit hit_test(const Box& box, const EditMapping& mapping, float x, float y) {
    CaretHit hit;
    if (mapping.text == nullptr || mapping.spans.empty()) {
        return hit;
    }
    // Spans are stored in lines_owner border-box space (make_edit_mapping); never use a nested
    // inline's world origin or soft-wrap Y bands land on the wrong visual line.
    const Box& geo = *lines_owner(box);

    std::size_t best_line = mapping.spans.front().line_index;
    float best_dy = std::numeric_limits<float>::max();
    for (const MappedSpan& span : mapping.spans) {
        const float font_box = span.font_ascent + span.font_descent;
        if (span_is_vertical(span)) {
            const float left = world_x_of(geo, span.local_x);
            const float width = std::max(font_box, 1.f);
            const float mid = left + width * 0.5f;
            if (x >= left && x < left + width) {
                best_line = span.line_index;
                best_dy = 0.f;
                break;
            }
            const float dx = std::fabs(x - mid);
            if (dx < best_dy) {
                best_dy = dx;
                best_line = span.line_index;
            }
            continue;
        }
        const float top = world_y_of(geo, span.local_y + span.ascent - span.font_ascent);
        const float height = font_box;
        const float mid = top + height * 0.5f;
        if (y >= top && y < top + std::max(height, 1.f)) {
            best_line = span.line_index;
            best_dy = 0.f;
            break;
        }
        const float dy = std::fabs(y - mid);
        if (dy < best_dy) {
            best_dy = dy;
            best_line = span.line_index;
        }
    }
    if (!line_has_mappable(mapping, best_line)) {
        return hit;
    }

    float best_dx = std::numeric_limits<float>::max();
    std::size_t best_off = 0;
    bool best_trailing = false;
    bool found = false;

    for (const MappedSpan& span : mapping.spans) {
        if (span.line_index != best_line) {
            continue;
        }
        const std::vector<ClusterEdge> edges = cluster_edges(geo, mapping, span);
        const float axis_x = span.axis_x;
        const float axis_y = span.axis_y;
        const auto inline_of = [&](float px, float py) { return px * axis_x + py * axis_y; };
        if (edges.size() < 2) {
            const float dx = std::fabs(inline_of(x, y) - inline_of(edges.empty() ? 0.f : edges[0].x,
                                                                   edges.empty() ? 0.f : edges[0].y));
            if (!found || dx < best_dx) {
                best_dx = dx;
                best_off = span.byte_begin;
                best_trailing = false;
                found = true;
            }
            continue;
        }
        const float here = inline_of(x, y);
        for (std::size_t i = 0; i + 1 < edges.size(); ++i) {
            const float c0 = inline_of(edges[i].x, edges[i].y);
            const float c1 = inline_of(edges[i + 1].x, edges[i + 1].y);
            const float mid = (c0 + c1) * 0.5f;
            // LTR inline grows with the byte. An RTL run's logical byte grows the other way,
            // so the midpoint test follows whichever edge is further along that run.
            const bool toward_end = (c1 >= c0) ? (here >= mid) : (here <= mid);
            const std::size_t at = toward_end ? edges[i + 1].byte : edges[i].byte;
            const float px = toward_end ? c1 : c0;
            const float dx = std::fabs(here - px);
            const float lo = std::min(c0, c1);
            const float hi = std::max(c0, c1);
            if (!found || dx < best_dx - 1e-4f ||
                (std::fabs(dx - best_dx) <= 1e-4f && here >= lo && here <= hi && toward_end)) {
                best_dx = dx;
                best_off = at;
                best_trailing = toward_end && at == edges[i + 1].byte && at != edges[i].byte;
                found = true;
            }
        }
    }

    if (!found) {
        return hit;
    }
    hit.ok = true;
    hit.offset = best_off;
    hit.trailing = best_trailing;
    hit.affinity = CaretAffinity::Downstream;
    // Soft-wrap shared offset: hitting the previous visual line's end → Upstream; hitting the
    // next visual line's start → Downstream.
    for (const MappedSpan& s : mapping.spans) {
        if (s.line_index != best_line) {
            continue;
        }
        if (s.byte_end == best_off) {
            for (const MappedSpan& nxt : mapping.spans) {
                if (nxt.byte_begin == best_off && nxt.line_index == best_line + 1) {
                    hit.affinity = CaretAffinity::Upstream;
                    break;
                }
            }
        }
        if (s.byte_begin == best_off) {
            for (const MappedSpan& prev : mapping.spans) {
                if (prev.byte_end == best_off && prev.line_index + 1 == best_line) {
                    hit.affinity = CaretAffinity::Downstream;
                    break;
                }
            }
        }
    }
    return hit;
}

}  // namespace layout
}  // namespace zui
