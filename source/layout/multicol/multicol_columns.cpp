#include "layout/multicol/detail.h"

#include "layout/cache/float_place.h"
#include "layout/cache/overflow.h"
#include "layout/cache/used_length.h"

#include <algorithm>
#include <cmath>

namespace zui {
namespace layout {
namespace multicol_detail {

const LayoutStyle& style_of(const Box& box) {
    static const LayoutStyle kDefault{};
    return box.style_ptr != nullptr ? box.style() : kDefault;
}

float length_or_percent(float value, bool is_percent, float basis, bool basis_known) {
    if (!is_percent) {
        return value;
    }
    return basis_known ? basis * value / 100.f : 0.f;
}

float content_from_specified(float specified, bool border_box, float chrome) {
    return border_box ? std::max(0.f, specified - chrome) : specified;
}

Edges logical_margin(WritingMode mode, Direction direction, const LayoutStyle& style,
                     const AxisSize& basis) {
    const bool known = basis.definite;
    const float px = known ? basis.px : 0.f;
    auto edge = [&](float value, bool percent, bool is_auto) {
        return is_auto ? 0.f : length_or_percent(value, percent, px, known);
    };
    return physical_edges_to_logical(
        mode, direction, edge(style.margin_top, style.margin_top_percent, style.margin_top_auto),
        edge(style.margin_right, style.margin_right_percent, style.margin_right_auto),
        edge(style.margin_bottom, style.margin_bottom_percent, style.margin_bottom_auto),
        edge(style.margin_left, style.margin_left_percent, style.margin_left_auto));
}

Edges padding_edges(WritingMode mode, Direction direction, const LayoutStyle& style,
                    const AxisSize& basis) {
    const bool known = basis.definite;
    const float px = known ? basis.px : 0.f;
    return physical_edges_to_logical(
        mode, direction, length_or_percent(style.padding_top, style.padding_top_percent, px, known),
        length_or_percent(style.padding_right, style.padding_right_percent, px, known),
        length_or_percent(style.padding_bottom, style.padding_bottom_percent, px, known),
        length_or_percent(style.padding_left, style.padding_left_percent, px, known));
}

Edges border_edges(WritingMode mode, Direction direction, const LayoutStyle& style) {
    return physical_edges_to_logical(mode, direction, style.border_top, style.border_right,
                                     style.border_bottom, style.border_left);
}

bool out_of_flow(const LayoutStyle& style) {
    return style.position_absolute || style.position_fixed;
}

bool scrolls(const LayoutStyle& style) {
    return style.overflow_x != OverflowMode::Visible || style.overflow_y != OverflowMode::Visible;
}

ConstraintSpace child_space(const ConstraintSpace& parent, const LayoutStyle& style,
                            float inline_size, bool block_definite, float block_size,
                            const std::shared_ptr<const ExclusionSpace>& exclusion,
                            const LogicalPoint& bfc_origin) {
    ConstraintSpace space;
    const Edges child_margin =
        logical_margin(style.writing_mode, style.direction, style, {true, inline_size});
    space.available.inline_size = {
        true, std::max(0.f, inline_size - child_margin.inline_start - child_margin.inline_end)};
    space.available.block_size =
        block_definite ? AxisSize{true, std::max(0.f, block_size)} : AxisSize{false, 0};
    space.percentage.inline_size = {true, std::max(0.f, inline_size)};
    space.percentage.block_size =
        block_definite ? AxisSize{true, std::max(0.f, block_size)} : parent.percentage.block_size;
    space.containing_block.inline_size = space.percentage.inline_size;
    space.containing_block.block_size = space.percentage.block_size;
    space.containing_block.fixed_is_viewport = parent.containing_block.fixed_is_viewport;
    space.icb = parent.icb;
    space.writing_mode = style.writing_mode;
    space.direction = style.direction;
    space.exclusion = exclusion;
    space.bfc_origin = bfc_origin;
    space.fragmentation = parent.fragmentation;
    space.allow_break = parent.allow_break;
    space.request.kind = RequestKind::Final;
    space.request.axis = Axis::Block;
    return space;
}

/// CSS Multi-column Layout section 3.4 (the `column-count` / `column-width` pseudo-algorithm), as
/// the used column count and the used column width. The columns always fill the container: the
/// width is `(available - (N-1) * gap) / N` once N is known, which is what Chrome reports.
Columns solve_columns(const LayoutStyle& style, float available, float gap) {
    Columns out;
    const float usable = std::max(0.f, available);
    const int specified_count = style.column_count > 0 ? style.column_count : 0;
    const bool width_auto = style.column_width_auto || style.column_width <= 0.f;
    auto fill = [&](int count) {
        count = std::max(1, count);
        out.count = count;
        out.width = std::max(0.f, (usable - gap * static_cast<float>(count - 1)) /
                                      static_cast<float>(count));
    };
    if (specified_count == 0 && width_auto) {
        fill(1);
        return out;
    }
    if (width_auto) {
        fill(specified_count);
        return out;
    }
    const float step = style.column_width + gap;
    const int fits = step > 0.f ? static_cast<int>(std::floor((usable + gap) / step)) : 1;
    // With both specified the count wins, unless the specified width cannot fit that many times.
    fill(specified_count == 0 ? std::max(1, fits) : std::min(specified_count, std::max(1, fits)));
    return out;
}

}  // namespace multicol_detail
}  // namespace layout
}  // namespace zui
