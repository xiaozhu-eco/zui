#ifndef ZUI_SOURCE_LAYOUT_CARET_INTERNAL_H
#define ZUI_SOURCE_LAYOUT_CARET_INTERNAL_H

#include "layout/caret/caret_types.h"
#include "layout/box.h"
#include "layout/cache/layout_result.h"

#include <cstddef>
#include <vector>

namespace zui {
namespace layout {
namespace caret_detail {

const LayoutResult* pinned_of(const Box& box);
const Box* lines_owner(const Box& box);
float world_x_of(const Box& box, float local_x);
float world_y_of(const Box& box, float local_y);
bool span_is_vertical(const MappedSpan& span);

struct ClusterEdge {
    std::size_t byte = 0;
    float x = 0;
    float y = 0;
};

std::vector<ClusterEdge> cluster_edges(const Box& box, const EditMapping& mapping,
                                       const MappedSpan& span);
const MappedSpan* span_covering(const EditMapping& mapping, std::size_t offset,
                                CaretAffinity affinity);
bool line_has_mappable(const EditMapping& mapping, std::size_t line_index);

}  // namespace caret_detail
}  // namespace layout
}  // namespace zui

#endif
