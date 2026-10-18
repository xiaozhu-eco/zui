#include "layout/caret/caret_service.h"
#include "layout/caret/caret_internal.h"

#include "layout/box.h"

#include <algorithm>
#include <vector>

namespace zui {
namespace layout {

using namespace caret_detail;

CaretRect get_rect(const Box& box, const EditMapping& mapping, std::size_t offset,
                   CaretAffinity affinity) {
    CaretRect rect;
    if (mapping.text == nullptr || mapping.spans.empty()) {
        return rect;
    }
    const Box& geo = *lines_owner(box);
    offset = std::min(offset, mapping.text->size());
    const MappedSpan* span = span_covering(mapping, offset, affinity);
    if (span == nullptr || !line_has_mappable(mapping, span->line_index)) {
        return rect;
    }

    const std::vector<ClusterEdge> edges = cluster_edges(geo, mapping, *span);
    if (edges.empty()) {
        return rect;
    }
    const ClusterEdge* chosen = &edges.front();
    for (const ClusterEdge& edge : edges) {
        if (edge.byte <= offset) {
            chosen = &edge;
        } else {
            break;
        }
    }
    if (offset >= span->byte_end) {
        chosen = &edges.back();
    }

    const float font_box = span->font_ascent + span->font_descent;
    if (font_box <= 0.f) {
        return rect;
    }
    rect.ok = true;
    rect.x = chosen->x;
    rect.y = chosen->y;
    if (span_is_vertical(*span)) {
        rect.width = font_box;
        rect.height = 1.f;
    } else {
        rect.width = 1.f;
        rect.height = font_box;
    }
    return rect;
}

}  // namespace layout
}  // namespace zui
