#ifndef ZUI_SOURCE_LAYOUT_MULTICOL_DETAIL_H
#define ZUI_SOURCE_LAYOUT_MULTICOL_DETAIL_H

#include "layout/box.h"
#include "layout/cache/exclusion_space.h"
#include "layout/cache/layout_context.h"
#include "layout/cache/layout_key.h"
#include "layout/cache/logical_axes.h"
#include "layout/layout_style.h"

#include <memory>

namespace zui {
namespace layout {
namespace multicol_detail {

const LayoutStyle& style_of(const Box& box);
float length_or_percent(float value, bool is_percent, float basis, bool basis_known);
float content_from_specified(float specified, bool border_box, float chrome);
Edges logical_margin(WritingMode mode, Direction direction, const LayoutStyle& style,
                     const AxisSize& basis);
Edges padding_edges(WritingMode mode, Direction direction, const LayoutStyle& style,
                    const AxisSize& basis);
Edges border_edges(WritingMode mode, Direction direction, const LayoutStyle& style);
bool out_of_flow(const LayoutStyle& style);
bool scrolls(const LayoutStyle& style);
ConstraintSpace child_space(const ConstraintSpace& parent, const LayoutStyle& style,
                            float inline_size, bool block_definite, float block_size,
                            const std::shared_ptr<const ExclusionSpace>& exclusion,
                            const LogicalPoint& bfc_origin);

struct Columns {
    int count = 1;
    float width = 0;
};

Columns solve_columns(const LayoutStyle& style, float available, float gap);

}  // namespace multicol_detail
}  // namespace layout
}  // namespace zui

#endif
