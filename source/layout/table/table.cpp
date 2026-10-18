#include "layout/table/table.h"
#include "layout/baseline.h"
#include "layout/table/detail.h"


#include "layout/box.h"
#include "layout/cache/box_cache.h"
#include "layout/cache/fragment_cut.h"
#include "layout/cache/layout_context.h"
#include "layout/cache/layout_key.h"
#include "layout/cache/logical_axes.h"
#include "layout/cache/overflow.h"
#include "layout/cache/used_length.h"
#include "layout/layout_style.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace zui {
namespace layout {

using namespace table_detail;

LayoutResult layout_table(Box& box, const LayoutKey& key, LayoutContext& cx) {
    LayoutResult out;
    bool converged = true;
    out.converged = true;
    // A table box's margins do not collapse with anything (CSS 2.1 section 8.3.1).
    out.margin_strut.blocks_margin_collapse = true;
    const LayoutStyle& style = style_of(box);
    const WritingMode mode = style.writing_mode;
    const Direction direction = style.direction;
    const AxisSize width_basis = physical_width_basis(mode, key.space.percentage);
    const AxisSize height_basis = physical_height_basis(mode, key.space.percentage);
    const Edges margin = logical_margin(mode, direction, style, key.space.percentage.inline_size);
    const Edges padding = padding_edges(mode, direction, style, key.space.percentage.inline_size);
    const Edges border = border_edges(mode, direction, style);
    const float pad_inline = padding.inline_start + padding.inline_end;
    const float pad_block = padding.block_start + padding.block_end;
    const float chrome_inline = pad_inline + border.inline_start + border.inline_end;
    const float chrome_block = pad_block + border.block_start + border.block_end;
    const float self_margin_inline = margin.inline_start + margin.inline_end;
    const float self_margin_block = margin.block_start + margin.block_end;
    out.geometry.margin = margin;
    out.geometry.border = border;
    out.geometry.padding = padding;
    out.margin_strut.start = margin.block_start;
    out.margin_strut.end = margin.block_end;

    bool width_auto = false;
    bool height_auto = false;
    const float width_used =
        used_main(style.width_auto, style.width_percent, style.width_px, width_basis, &width_auto);
    const float height_used = used_main(style.height_auto, style.height_percent, style.height_px,
                                        height_basis, &height_auto);
    width_auto = width_auto || (style.width_percent && !width_basis.definite);
    height_auto = height_auto || (style.height_percent && !height_basis.definite);
    const bool intrinsic_min = style.width_intrinsic == IntrinsicSize::MinContent;
    const bool intrinsic_max = style.width_intrinsic == IntrinsicSize::MaxContent;
    const bool intrinsic_fit = style.width_intrinsic == IntrinsicSize::FitContent;
    // `width: min-content | max-content | fit-content` is not a used length: the table's own
    // intrinsic size solves it below, from the columns it just measured.
    const bool intrinsic = intrinsic_min || intrinsic_max || intrinsic_fit;
    // `min-width` / `max-width` clamp the used inline size (CSS Sizing section 5). The table
    // resolves its own inline size below instead of going through `normal_flow`'s clamp, so the
    // two declarations have to be read here. Measured: `width: max-content; min-width: 100%`
    // (example-zdb's `.ztable`) is the containing block's 852px in Chrome and was 0 in the engine,
    // and `build/scratch/table-min-percent.html` pins the same for `min-width: 60%` (240px of a
    // 400px containing block) while a plain `width: max-content` table stays 0 on both ends.
    bool min_width_auto = false;
    bool min_height_auto = false;
    bool max_width_none = false;
    bool max_height_none = false;
    const float min_width =
        used_main(style, LengthProperty::MinWidth, width_basis, &min_width_auto);
    const float min_height =
        used_main(style, LengthProperty::MinHeight, height_basis, &min_height_auto);
    const float max_width =
        used_main(style, LengthProperty::MaxWidth, width_basis, &max_width_none);
    const float max_height =
        used_main(style, LengthProperty::MaxHeight, height_basis, &max_height_none);
    float min_inline = 0.f;
    float max_inline = 1.0e9f;
    bool min_inline_auto = true;
    physical_size_to_logical(mode, min_width, min_height, min_width_auto, min_height_auto,
                             &min_inline, nullptr, &min_inline_auto, nullptr);
    physical_size_to_logical(mode, max_width, max_height, max_width_none, max_height_none,
                             &max_inline, nullptr, nullptr, nullptr);
    const bool flex_inline = box.is_flex_item && key.space.available.inline_size.definite;
    const bool flex_block = box.is_flex_item && key.space.available.block_size.definite;
    const bool grid_stretch =
        box.is_grid_item && key.space.stretch_block && key.space.available.block_size.definite;
    float specified_inline = 0;
    bool inline_specified = false;
    if (flex_inline) {
        specified_inline =
            std::max(0.f, key.space.available.inline_size.px - self_margin_inline - chrome_inline);
        inline_specified = true;
    } else if (!width_auto && !intrinsic) {
        specified_inline = content_from_specified(width_used, style.border_box, chrome_inline);
        inline_specified = true;
    }

    Model model = build_model(box);
    const int columns = model.columns;

    // Contributions: a cell's min-content and max-content inline sizes decide how wide its column
    // has to be (CSS 2.1 section 17.5.2.2, the min/max constraint widths).
    const AxisSize percent_inline = key.space.percentage.inline_size;
    ConstraintSpace base_cell_space;
    base_cell_space.percentage.inline_size =
        inline_specified || percent_inline.definite
            ? AxisSize{true, inline_specified ? specified_inline : percent_inline.px}
            : AxisSize{false, 0};
    base_cell_space.percentage.block_size = key.space.percentage.block_size;
    base_cell_space.containing_block.inline_size = base_cell_space.percentage.inline_size;
    base_cell_space.containing_block.block_size = key.space.percentage.block_size;
    base_cell_space.containing_block.fixed_is_viewport =
        key.space.containing_block.fixed_is_viewport;
    base_cell_space.icb = key.space.icb;
    base_cell_space.writing_mode = mode;
    base_cell_space.direction = direction;
    base_cell_space.exclusion = key.space.exclusion;
    // The cells are asked with the strip's key for the same reason the grid asks its items that way
    // (LAYOUT.md section 12): the table consumes the fragmentainer itself and breaks between rows,
    // so a cell must not be told it has already consumed part of its own block axis.
    base_cell_space.allow_break = key.space.allow_break;

    // A cell whose every column is collapsed disappears with those columns (CSS 2.1 搂17.5.5).
    for (Row& row : model.rows) {
        for (Cell& cell : row.cells) {
            bool any_open = false;
            for (int c = cell.col; c < cell.col + cell.colspan && c < columns; ++c) {
                if (!column_collapsed(model, c)) {
                    any_open = true;
                    break;
                }
            }
            if (!any_open && cell.colspan > 0) {
                cell.collapsed = true;
            }
        }
    }

    for (Row& row : model.rows) {
        for (Cell& cell : row.cells) {
            if (cell.box == nullptr || cell.collapsed) {
                continue;
            }
            const LayoutStyle& cell_style = style_of(*cell.box);
            ConstraintSpace min_space =
                child_space(base_cell_space, cell_style, *cell.box, 0.f, 0.f, false, false,
                            RequestKind::MinContent, Axis::Inline);
            min_space.available.inline_size = {false, 0};
            const LayoutResult* min_result = cx.ask(*cell.box, min_space);
            cell.min_inline = min_result != nullptr ? min_result->geometry.border_box_inline : 0.f;
            cx.keep_latest_ask(*cell.box);
            cx.reset_ask_fuse(*cell.box);
            if (min_result != nullptr && min_result->intrinsic_inline_equal) {
                // The max-content border box is this one. A second walk would store the same size.
                cell.max_inline = cell.min_inline;
            } else {
                ConstraintSpace max_space =
                    child_space(base_cell_space, cell_style, *cell.box, 0.f, 0.f, false, false,
                                RequestKind::MaxContent, Axis::Inline);
                max_space.available.inline_size = {false, 0};
                const LayoutResult* max_result = cx.ask(*cell.box, max_space);
                cell.max_inline =
                    max_result != nullptr ? max_result->geometry.border_box_inline : 0.f;
                cx.keep_latest_ask(*cell.box);
                cx.reset_ask_fuse(*cell.box);
            }
            if (cell.colspan == 1 && cell.col < columns && !column_collapsed(model, cell.col)) {
                const std::size_t col = static_cast<std::size_t>(cell.col);
                model.col_min[col] = std::max(model.col_min[col], cell.min_inline);
                model.col_max[col] = std::max(model.col_max[col], cell.max_inline);
                // The width a cell declares names the column, it is not the cell's own used size
                // (CSS 2.1 section 17.5.2.2). A percentage outranks a length, and the widest
                // declaration in the column wins either way (measured on `build/table_dist.html`:
                // Chrome resolves t26/t27 to the percentage, t28 to the larger percentage, and
                // t29 to the larger length).
                if (cell_style.width_percent) {
                    model.col_percent[col] = std::max(model.col_percent[col], cell_style.width_px);
                } else if (!cell_style.width_auto) {
                    model.col_length[col] = std::max(model.col_length[col], cell_style.width_px);
                }
            }
        }
    }
    for (int span = 2; span <= columns; ++span) {
        for (Row& row : model.rows) {
            for (Cell& cell : row.cells) {
                if (cell.collapsed || cell.colspan != span) {
                    continue;
                }
                int visible_in_span = 0;
                for (int c = cell.col; c < cell.col + span && c < columns; ++c) {
                    if (!column_collapsed(model, c)) {
                        ++visible_in_span;
                    }
                }
                const float spacing =
                    model.spacing_inline * static_cast<float>(std::max(0, visible_in_span - 1));
                raise_span(model.col_min, cell.col, span, cell.min_inline - spacing,
                           &model.col_collapsed);
                raise_span(model.col_max, cell.col, span, cell.max_inline - spacing,
                           &model.col_collapsed);
            }
        }
    }
    for (int c = 0; c < columns; ++c) {
        if (column_collapsed(model, c)) {
            model.col_min[static_cast<std::size_t>(c)] = 0.f;
            model.col_max[static_cast<std::size_t>(c)] = 0.f;
            model.col_specified[static_cast<std::size_t>(c)] = false;
            continue;
        }
        model.col_max[static_cast<std::size_t>(c)] = std::max(
            model.col_max[static_cast<std::size_t>(c)], model.col_min[static_cast<std::size_t>(c)]);
    }

    const float spacing_inline = model.spacing_inline;
    const float spacing_block = model.spacing_block;
    int visible_columns = 0;
    for (int c = 0; c < columns; ++c) {
        if (!column_collapsed(model, c)) {
            ++visible_columns;
        }
    }
    const float between_columns =
        visible_columns > 1 ? spacing_inline * static_cast<float>(visible_columns - 1) : 0.f;
    const float grid_min = span_sum(model.col_min, 0, columns) + between_columns;
    const float grid_max = span_sum(model.col_max, 0, columns) + between_columns;
    // The table's content box carries the border spacing at both edges too.
    const float edge_spacing = visible_columns > 0 ? spacing_inline * 2.f : 0.f;
    float grid_target = grid_max;
    if (inline_specified) {
        grid_target = std::max(0.f, specified_inline - edge_spacing);
    } else if (intrinsic_min) {
        grid_target = grid_min;
    } else if (intrinsic_max) {
        grid_target = grid_max;
    } else if (key.space.available.inline_size.definite) {
        const float available =
            std::max(0.f, key.space.available.inline_size.px - self_margin_inline - chrome_inline -
                              edge_spacing);
        grid_target = std::min(std::max(grid_min, available), std::max(grid_max, grid_min));
    }
    // The clamps land on `grid_target`, so a table the declarations widen hands the extra space to
    // the column algorithm - CSS 2.1 section 17.5.2.2 says the extra width is distributed over the
    // columns, and a table with no columns simply keeps it. Both are on the box `box-sizing`
    // names, and `grid_target` excludes the border spacing the content box carries at both edges.
    if (!min_inline_auto) {
        const float floor = content_from_specified(min_inline, style.border_box, chrome_inline);
        grid_target = std::max(grid_target, std::max(0.f, floor - edge_spacing));
    }
    if (!max_width_none) {
        const float cap = content_from_specified(max_inline, style.border_box, chrome_inline);
        grid_target = std::min(grid_target, std::max(0.f, cap - edge_spacing));
    }
    float content_inline = grid_target + edge_spacing;
    (void)intrinsic_fit;

    // Column widths: the fixed algorithm takes the named widths and splits what is left equally,
    // the auto one grows every column from its minimum towards its maximum. Collapsed columns
    // stay at used width 0 and take none of the free space.
    if (style.table_fixed && inline_specified && columns > 0) {
        for (int c = 0; c < columns; ++c) {
            const std::size_t col = static_cast<std::size_t>(c);
            if (column_collapsed(model, c)) {
                model.col_used[col] = 0.f;
                continue;
            }
            model.col_used[col] = model.col_specified[col] ? model.col_max[col] : 0.f;
        }
        float named = span_sum(model.col_used, 0, columns);
        int autos = 0;
        for (int c = 0; c < columns; ++c) {
            if (!column_collapsed(model, c) && !model.col_specified[static_cast<std::size_t>(c)]) {
                ++autos;
            }
        }
        const float room = std::max(0.f, grid_target - between_columns - named);
        if (autos > 0) {
            for (int c = 0; c < columns; ++c) {
                const std::size_t col = static_cast<std::size_t>(c);
                if (!column_collapsed(model, c) && !model.col_specified[col]) {
                    model.col_used[col] = room / static_cast<float>(autos);
                }
            }
        } else if (visible_columns > 0) {
            named = std::max(1.f, named);
            for (int c = 0; c < columns; ++c) {
                const std::size_t col = static_cast<std::size_t>(c);
                if (column_collapsed(model, c)) {
                    model.col_used[col] = 0.f;
                    continue;
                }
                model.col_used[col] = grid_target * model.col_used[col] / named;
            }
        }
    } else {
        // Column widths, auto layout (CSS 2.1 section 17.5.2.2). A column a declaration names - a
        // percentage or a length, on a cell or on a `<col>` - is sized by that declaration, and the
        // rest share what is left. Both distributions here are measured against headless Chrome
        // (`build/table_dist.html`, pinned by the difftest `table-auto-distribution` case): the
        // excess goes to the undeclared columns in proportion to their max-content widths, and a
        // table narrower than its columns takes the difference off them in proportion to how far
        // each sits above its min-content, stopping there.
        const float track_area = std::max(0.f, grid_target - between_columns);
        std::vector<float> wanted(static_cast<std::size_t>(columns), 0.f);
        float percent_total = 0.f;
        int last_percent = -1;
        for (int c = 0; c < columns; ++c) {
            const std::size_t col = static_cast<std::size_t>(c);
            if (column_collapsed(model, c) || model.col_percent[col] <= 0.f) {
                continue;
            }
            percent_total += model.col_percent[col];
            last_percent = c;
        }
        // Percentages that add up to more than 100% come off the last column that declares one
        // (measured: `build/table_dist.html` t15, 80% + 50% resolves to 80% + 20%).
        if (percent_total > 100.f && last_percent >= 0) {
            const std::size_t col = static_cast<std::size_t>(last_percent);
            model.col_percent[col] =
                std::max(0.f, model.col_percent[col] - (percent_total - 100.f));
        }
        auto declared = [&](int c) {
            const std::size_t col = static_cast<std::size_t>(c);
            return model.col_percent[col] > 0.f || model.col_length[col] >= 0.f;
        };
        bool any_undeclared = false;
        for (int c = 0; c < columns; ++c) {
            if (!column_collapsed(model, c) && !declared(c)) {
                any_undeclared = true;
                break;
            }
        }
        auto shares_extra = [&](int c) {
            return !column_collapsed(model, c) && (any_undeclared ? !declared(c) : true);
        };
        // Blink's table layout works in 1/64px units and truncates each share, handing the leftover
        // to the last column that takes part. A float distribution lands within 1/64 of Chrome but
        // not on it, and the L1 channel compares used lengths at 0.01px (measured on
        // `build/table_dist.html`: t1/t2/t6/t30 land exactly on Chrome's columns this way).
        auto unit = [](float value) { return std::round(value * 64.f) / 64.f; };
        auto unit_down = [](float value) { return std::floor(value * 64.f) / 64.f; };
        // What each column asks for: its declaration - a percentage of the grid or a length,
        // floored by the content's min-content width - and its own max-content width when it
        // declares nothing. `base` is where the column sits before the difference below is shared:
        // a declared column starts at what it asked for, the others at their min-content width.
        std::vector<float> band(static_cast<std::size_t>(columns), 0.f);
        std::vector<float> base(static_cast<std::size_t>(columns), 0.f);
        float band_total = 0.f;
        for (int c = 0; c < columns; ++c) {
            const std::size_t col = static_cast<std::size_t>(c);
            if (column_collapsed(model, c)) {
                continue;
            }
            float preferred = model.col_max[col];
            if (model.col_percent[col] > 0.f) {
                preferred = track_area * model.col_percent[col] / 100.f;
            } else if (model.col_length[col] >= 0.f) {
                preferred = model.col_length[col];
            }
            band[col] = std::max(preferred, model.col_min[col]);
            base[col] = declared(c) ? band[col] : model.col_min[col];
            band_total += band[col];
        }
        if (track_area + 0.01f >= band_total) {
            // Wider than every column's max-content: the columns keep it and the extra goes to the
            // columns that declare no width, in proportion to their max-content (falls back to all
            // of them when every column declares one) - measured: t1/t5/t9/t16/t18/t20/t21/t23.
            const float track_units = unit(track_area);
            float band_units = 0.f;
            for (int c = 0; c < columns; ++c) {
                const std::size_t col = static_cast<std::size_t>(c);
                if (column_collapsed(model, c)) {
                    continue;
                }
                band[col] = unit(band[col]);
                band_units += band[col];
            }
            float weight = 0.f;
            int sharers = 0;
            int last_sharer = -1;
            for (int c = 0; c < columns; ++c) {
                if (!shares_extra(c)) {
                    continue;
                }
                weight += band[static_cast<std::size_t>(c)];
                ++sharers;
                last_sharer = c;
            }
            const float excess = track_units - band_units;
            float handed = 0.f;
            for (int c = 0; c < columns; ++c) {
                const std::size_t col = static_cast<std::size_t>(c);
                if (column_collapsed(model, c)) {
                    continue;
                }
                wanted[col] = band[col];
                if (!shares_extra(c)) {
                    continue;
                }
                float share = unit_down(excess / static_cast<float>(std::max(1, sharers)));
                if (weight > 0.f) {
                    share = c == last_sharer ? excess - handed
                                             : unit_down(excess * band[col] / weight);
                }
                handed += share;
                wanted[col] += share;
            }
        } else {
            // Between the columns' min-content widths and their max-content widths every column
            // sits where its own room puts it: the difference from the sum of the bases is shared
            // in proportion to the room a column has left towards its max-content (measured: t11),
            // and a table narrower than the bases takes the difference off them in proportion to
            // how far each sits above its min-content (measured: t15/t17/t25).
            float base_total = 0.f;
            for (int c = 0; c < columns; ++c) {
                if (column_collapsed(model, c)) {
                    continue;
                }
                base_total += base[static_cast<std::size_t>(c)];
            }
            const float delta = track_area - base_total;
            if (delta >= 0.f) {
                // The room a column still has towards its max-content decides how much of the
                // difference it takes (measured: t11, whose columns land on Chrome's 36.5625 and
                // 23.796875 only with the 1/64px truncation above).
                const float delta_units = unit(track_area) - unit(base_total);
                float weight = 0.f;
                int takers = 0;
                int last_taker = -1;
                for (int c = 0; c < columns; ++c) {
                    const std::size_t col = static_cast<std::size_t>(c);
                    if (column_collapsed(model, c)) {
                        continue;
                    }
                    weight += std::max(0.f, unit(band[col]) - unit(base[col]));
                    if (std::max(0.f, band[col] - base[col]) > 0.f) {
                        ++takers;
                        last_taker = c;
                    }
                }
                float handed = 0.f;
                for (int c = 0; c < columns; ++c) {
                    const std::size_t col = static_cast<std::size_t>(c);
                    if (column_collapsed(model, c)) {
                        continue;
                    }
                    wanted[col] = base[col];
                    if (weight <= 0.f || std::max(0.f, band[col] - base[col]) <= 0.f) {
                        continue;
                    }
                    const float room = std::max(0.f, unit(band[col]) - unit(base[col]));
                    const float share = c == last_taker
                                            ? delta_units - handed
                                            : unit_down(delta_units * room / weight);
                    handed += share;
                    wanted[col] += share;
                }
            } else {
                float deficit = -delta;
                for (int c = 0; c < columns; ++c) {
                    const std::size_t col = static_cast<std::size_t>(c);
                    wanted[col] = column_collapsed(model, c) ? 0.f : base[col];
                }
                std::vector<bool> frozen(static_cast<std::size_t>(columns), false);
                while (deficit > 0.01f) {
                    float slack = 0.f;
                    for (int c = 0; c < columns; ++c) {
                        const std::size_t col = static_cast<std::size_t>(c);
                        if (column_collapsed(model, c) || frozen[col]) {
                            continue;
                        }
                        slack += std::max(0.f, wanted[col] - model.col_min[col]);
                    }
                    if (slack <= 0.f) {
                        break;
                    }
                    const float step = std::min(deficit, slack);
                    for (int c = 0; c < columns; ++c) {
                        const std::size_t col = static_cast<std::size_t>(c);
                        if (column_collapsed(model, c) || frozen[col]) {
                            continue;
                        }
                        wanted[col] -=
                            step * std::max(0.f, wanted[col] - model.col_min[col]) / slack;
                    }
                    deficit -= step;
                    for (int c = 0; c < columns; ++c) {
                        const std::size_t col = static_cast<std::size_t>(c);
                        if (!column_collapsed(model, c) && !frozen[col] &&
                            wanted[col] <= model.col_min[col] + 0.01f) {
                            wanted[col] = model.col_min[col];
                            frozen[col] = true;
                        }
                    }
                }
            }
        }
        for (int c = 0; c < columns; ++c) {
            const std::size_t col = static_cast<std::size_t>(c);
            model.col_used[col] = column_collapsed(model, c) ? 0.f : wanted[col];
        }
        // The used width of a table is at least the smallest width its columns can take, so a
        // table capped below that grows to fit its content (measured: `build/table_dist.html` t7,
        // `width: 60px` over 68.03px of min-content is 68.03px wide in Chrome).
        if (grid_target < grid_min) {
            grid_target = grid_min;
            content_inline = grid_target + edge_spacing;
        }
    }
    TablePlace place;
    place.box = &box;
    place.key = &key;
    place.cx = &cx;
    place.out = &out;
    place.style = &style;
    place.mode = mode;
    place.direction = direction;
    place.padding = padding;
    place.border = border;
    place.chrome_inline = chrome_inline;
    place.chrome_block = chrome_block;
    place.self_margin_block = self_margin_block;
    place.height_auto = height_auto;
    place.height_used = height_used;
    place.flex_block = flex_block;
    place.grid_stretch = grid_stretch;
    place.model = &model;
    place.base_cell_space = base_cell_space;
    place.spacing_inline = spacing_inline;
    place.spacing_block = spacing_block;
    place.edge_spacing = edge_spacing;
    place.grid_target = grid_target;
    place.content_inline = content_inline;
    layout_table_place(place);
    return out;
}

}  // namespace layout
}  // namespace zui
