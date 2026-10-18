#include "layout/table/detail.h"

#include "layout/cache/logical_axes.h"

#include <algorithm>

// Table geometry: the used edges a table part gets, the column track geometry (collapsed tracks,
// spacing, span distribution), and the constraint space handed to a child.

namespace zui {
namespace layout {
namespace table_detail {

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
    const float top =
        style.margin_top_auto
            ? 0.f
            : length_or_percent(style.margin_top, style.margin_top_percent, px, known);
    const float right =
        style.margin_right_auto
            ? 0.f
            : length_or_percent(style.margin_right, style.margin_right_percent, px, known);
    const float bottom =
        style.margin_bottom_auto
            ? 0.f
            : length_or_percent(style.margin_bottom, style.margin_bottom_percent, px, known);
    const float left =
        style.margin_left_auto
            ? 0.f
            : length_or_percent(style.margin_left, style.margin_left_percent, px, known);
    return physical_edges_to_logical(mode, direction, top, right, bottom, left);
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

TablePart part_of(const Box& box) { return style_of(box).table_part; }

bool group_part(TablePart part) {
    return part == TablePart::RowGroup || part == TablePart::HeaderGroup ||
           part == TablePart::FooterGroup;
}

float span_sum(const std::vector<float>& values, int col, int span) {
    float total = 0;
    for (int c = col; c < col + span && c < static_cast<int>(values.size()); ++c) {
        total += values[static_cast<std::size_t>(c)];
    }
    return total;
}

bool column_collapsed(const Model& model, int col) {
    return col >= 0 && col < static_cast<int>(model.col_collapsed.size()) &&
           model.col_collapsed[static_cast<std::size_t>(col)];
}

float track_inline_start(const Model& model, int col, float spacing_inline) {
    float x = 0;
    int visible = 0;
    const int columns = model.columns;
    for (int c = 0; c < col && c < columns; ++c) {
        if (column_collapsed(model, c)) {
            continue;
        }
        if (visible > 0) {
            x += spacing_inline;
        }
        x += model.col_used[static_cast<std::size_t>(c)];
        ++visible;
    }
    if (visible > 0 && col < columns && !column_collapsed(model, col)) {
        x += spacing_inline;
    }
    return x;
}

float span_width_visible(const Model& model, int col, int span, float spacing_inline) {
    float total = 0;
    int visible = 0;
    for (int c = col; c < col + span && c < model.columns; ++c) {
        if (column_collapsed(model, c)) {
            continue;
        }
        if (visible > 0) {
            total += spacing_inline;
        }
        total += model.col_used[static_cast<std::size_t>(c)];
        ++visible;
    }
    return total;
}

void raise_span(std::vector<float>& values, int col, int span, float target,
                const std::vector<bool>* collapsed) {
    float total = 0;
    for (int c = col; c < col + span && c < static_cast<int>(values.size()); ++c) {
        if (collapsed != nullptr && c < static_cast<int>(collapsed->size()) &&
            (*collapsed)[static_cast<std::size_t>(c)]) {
            continue;
        }
        total += values[static_cast<std::size_t>(c)];
    }
    const float deficit = target - total;
    if (deficit <= 0.f || span <= 0) {
        return;
    }
    float weight = 0;
    for (int c = col; c < col + span && c < static_cast<int>(values.size()); ++c) {
        if (collapsed != nullptr && c < static_cast<int>(collapsed->size()) &&
            (*collapsed)[static_cast<std::size_t>(c)]) {
            continue;
        }
        weight += std::max(1.f, values[static_cast<std::size_t>(c)]);
    }
    if (weight <= 0.f) {
        return;
    }
    for (int c = col; c < col + span && c < static_cast<int>(values.size()); ++c) {
        if (collapsed != nullptr && c < static_cast<int>(collapsed->size()) &&
            (*collapsed)[static_cast<std::size_t>(c)]) {
            continue;
        }
        const float share = std::max(1.f, values[static_cast<std::size_t>(c)]) / weight;
        values[static_cast<std::size_t>(c)] += deficit * share;
    }
}

ConstraintSpace child_space(const ConstraintSpace& parent, const LayoutStyle& style, const Box& box,
                            float inline_size, float block_size, bool block_definite,
                            bool stretch_block, RequestKind kind, Axis axis) {
    ConstraintSpace space;
    const float margin_inline =
        logical_margin(style.writing_mode, style.direction, style, {true, inline_size})
            .inline_start +
        logical_margin(style.writing_mode, style.direction, style, {true, inline_size}).inline_end;
    space.available.inline_size = {true, std::max(0.f, inline_size - margin_inline)};
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
    space.exclusion = parent.exclusion;
    space.fragmentation = parent.fragmentation;
    space.allow_break = parent.allow_break;
    space.stretch_block = stretch_block;
    space.request.kind = kind;
    space.request.axis = axis;
    (void)box;
    return space;
}

}  // namespace table_detail
}  // namespace layout
}  // namespace zui
