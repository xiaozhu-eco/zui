#include "layout/grid/grid.h"
#include "layout/baseline.h"
#include "layout/grid/detail.h"

#include <cmath>
#include <limits>
#include <vector>

namespace zui {
namespace layout {
namespace {

void fill_subgrid_span(Box::SubgridSpan& span, const std::vector<Track>& tracks, int start,
                       int count, float gap) {
    span.set = true;
    span.gap = gap;
    span.tracks.clear();
    const int n = std::max(0, count);
    span.tracks.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const int idx = start + i;
        float size = 0.f;
        if (idx >= 0 && idx < static_cast<int>(tracks.size())) {
            size = tracks[static_cast<std::size_t>(idx)].base;
        }
        span.tracks.push_back(std::max(0.f, size));
    }
}

}  // namespace

namespace grid_detail {

void publish_subgrid_span(GridItem& item, const std::vector<Track>& columns, float column_gap,
                          const std::vector<Track>* rows, float row_gap) {
    if (item.box == nullptr) {
        return;
    }
    const LayoutStyle& child = style_of(*item.box);
    if (axis_is_subgrid(child.grid_template_columns)) {
        fill_subgrid_span(item.box->subgrid_columns, columns, item.col, item.col_span, column_gap);
    } else {
        item.box->subgrid_columns.set = false;
        item.box->subgrid_columns.tracks.clear();
    }
    if (rows == nullptr) {
        return;
    }
    if (axis_is_subgrid(child.grid_template_rows)) {
        fill_subgrid_span(item.box->subgrid_rows, *rows, item.row, item.row_span, row_gap);
    } else {
        item.box->subgrid_rows.set = false;
        item.box->subgrid_rows.tracks.clear();
    }
}

}  // namespace grid_detail

void measure_item_areas(Grid::Contributions& out, const LayoutKey& key, LayoutContext& cx,
                        WritingMode mode, Direction direction) {
    using namespace grid_detail;
    const LayoutStyle& style = *out.style;
    // Round two: the area. A stretch item is asked with a definite inline size, so it comes back
    // as wide as its columns and its block size becomes the row contribution.
    for (GridItem& item : out.items) {
        const float area_inline = area_extent(out.columns, item.col, item.col_span, out.column_gap);
        ConstraintSpace area_space = out.base_space;
        area_space.request.kind = RequestKind::Final;
        // A baseline-aligned item has to report its baselines: they are part of what the placement
        // compares, so they belong in the request (LAYOUT section 3).
        if (item.align_block == FlexAlign::Baseline ||
            item.align_block == FlexAlign::LastBaseline ||
            item.align_inline == FlexAlign::Baseline ||
            item.align_inline == FlexAlign::LastBaseline) {
            area_space.request.needs_baseline = true;
        }
        // CSS Grid: the containing block of a grid item is its grid area, so percentages on the
        // item (width, padding, margin) resolve against the area's inline size and not against
        // the container's padding box. The block axis stays indefinite for this round - the rows
        // are sized from what it returns - and becomes definite in round three.
        area_space.percentage.inline_size = AxisSize{true, area_inline};
        area_space.containing_block.inline_size = AxisSize{true, area_inline};
        // Percentage margins resolve against the containing block too, and that is the area: the
        // margin taken off the area below has to be the same number the item resolved for itself.
        item.margin =
            logical_margin(mode, direction, style_of(*item.box), AxisSize{true, area_inline});
        const float available_inline =
            std::max(0.f, area_inline - item.margin.inline_start - item.margin.inline_end);
        area_space.available.inline_size =
            item.stretch_inline ? AxisSize{true, available_inline} : AxisSize{false, 0};
        area_space.available.block_size = {false, 0};
        // The inline twin of `stretch_block` below: tell the item the container solved its inline
        // size and wants it filled. A `<button>` item is the one that has to hear it - without the
        // flag it exits through `button_fit` in `normal_flow` and stays content-wide. Only an
        // `auto` inline size fills: `justify-self: stretch` does not override a definite `width`,
        // and the area's inline size is already the percentage basis in this round.
        const LayoutStyle& area_style = style_of(*item.box);
        const bool child_width_auto =
            out.horizontal ? area_style.width_auto : area_style.height_auto;
        area_space.stretch_inline = item.stretch_inline && child_width_auto;
        // The block size this round reports is only a row contribution. A stretch (or percentage)
        // item is asked again once the rows are definite, and a row whose min and max are both
        // fixed never reads the contribution. Skipping the content-height ask leaves the painted
        // size to that later pass.
        const bool child_height_auto =
            area_style.height_auto || (area_style.height_percent && !out.block_known);
        const bool will_refit =
            (item.stretch_block && child_height_auto) || area_style.height_percent;
        const bool baseline_item = item.align_block == FlexAlign::Baseline ||
                                   item.align_block == FlexAlign::LastBaseline ||
                                   item.align_inline == FlexAlign::Baseline ||
                                   item.align_inline == FlexAlign::LastBaseline;
        bool rows_fixed = will_refit && !baseline_item && !key.space.allow_break &&
                          area_style.aspect_ratio <= 0.f &&
                          !axis_is_subgrid(area_style.grid_template_columns) &&
                          !axis_is_subgrid(area_style.grid_template_rows) && item.row >= 0 &&
                          item.row_span > 0;
        for (int i = item.row; rows_fixed && i < item.row + item.row_span; ++i) {
            if (i >= static_cast<int>(out.rows.size())) {
                rows_fixed = false;
                break;
            }
            const Track& track = out.rows[static_cast<std::size_t>(i)];
            if (track.min_intrinsic || track.max_intrinsic) {
                rows_fixed = false;
            }
        }
        if (rows_fixed) {
            item.area_deferred = true;
            continue;
        }
        publish_subgrid_span(item, out.columns, out.column_gap, nullptr, 0.f);
        cx.reset_ask_fuse(*item.box);
        item.final_result = cx.ask(*item.box, area_space);
        const float border_block =
            item.final_result != nullptr ? item.final_result->geometry.border_box_block : 0.f;
        item.used_inline = item.final_result != nullptr
                               ? item.final_result->geometry.border_box_inline
                               : item.min_inline;
        item.used_block = border_block;
        item.natural_block = border_block + item.margin.block_start + item.margin.block_end;
        // CSS Grid section 11.5: a row's *base* size follows the item's minimum contribution and
        // its max-content size is only the growth limit (section 11.6 Maximize Tracks fills the
        // base up to it with whatever free space the container has). Taking `natural_block` for
        // both made an auto row as tall as its content even when the container was shorter, so a
        // `min-height: 0` flex item (the zdb `.stage`) pushed its panes past the viewport instead
        // of letting the row stop at the container's definite height.
        item.min_block = item.natural_block;
        if (out.horizontal) {
            const LayoutStyle& item_style = style_of(*item.box);
            if (!item_style.min_height_auto) {
                float floor = 0.f;
                if (item_style.min_height_percent) {
                    if (out.block_known) {
                        floor = out.content_block * item_style.min_height_px / 100.f;
                    }
                } else {
                    floor = item_style.min_height_px;
                }
                item.min_block = std::max(0.f, floor) + item.margin.block_start +
                                 item.margin.block_end;
                item.min_block = std::min(item.min_block, item.natural_block);
            }
        }
    }

    // A baseline-aligned item needs more room than its own block size: the shift that puts its
    // baseline on the row's has to fit under it as well, so the row can end up taller than its
    // tallest item (CSS Grid section 11.5, CSS Box Alignment section 9). The shift is the same one
    // `place` applies, computed from the baselines the area round reported.
    {
        std::vector<float> row_baseline(out.rows.size(), std::numeric_limits<float>::quiet_NaN());
        auto baseline_edge = [](const GridItem& item) {
            const float baseline = item.final_result->geometry.baselines.first;
            return std::isfinite(baseline) ? baseline
                                           : item.final_result->geometry.border_box_block;
        };
        for (const GridItem& item : out.items) {
            if (item.align_block != FlexAlign::Baseline || item.final_result == nullptr ||
                item.row < 0 || static_cast<std::size_t>(item.row) >= row_baseline.size()) {
                continue;
            }
            const float offset = item.margin.block_start + baseline_edge(item);
            float& slot = row_baseline[static_cast<std::size_t>(item.row)];
            if (!std::isfinite(slot) || offset > slot) {
                slot = offset;
            }
        }
        for (GridItem& item : out.items) {
            if (item.align_block != FlexAlign::Baseline || item.final_result == nullptr ||
                item.row < 0 || static_cast<std::size_t>(item.row) >= row_baseline.size() ||
                !std::isfinite(row_baseline[static_cast<std::size_t>(item.row)])) {
                continue;
            }
            // The shift is part of both contributions: a row sized from the minimum has to leave
            // room for the packed baseline just like one sized from the max-content.
            const float shift = row_baseline[static_cast<std::size_t>(item.row)] -
                                (item.margin.block_start + baseline_edge(item));
            item.natural_block += shift;
            item.min_block += shift;
        }
    }
    const bool maximize_block =
        !key.space.available.block_size.definite && key.space.request.kind != RequestKind::MinContent;
    size_rows(out.rows, out.row_template, out.items, out.row_gap, out.block_known,
              out.content_block, style.align_content, maximize_block);
    if (!out.block_known) {
        out.content_block = sum_tracks(out.rows, out.row_gap);
        out.block_known = true;
    }

    // Round three: the block axis of the area is definite now, so the items whose block size
    // depended on it get one more ask - the ones that stretch to fill the area, and the ones with
    // a percentage height that had no basis until the rows were solved. Each round gets its own
    // two-ask budget (LAYOUT section 5), so this is not a third round of the same question.
    for (GridItem& item : out.items) {
        const LayoutStyle& child_style = style_of(*item.box);
        const bool child_height_auto =
            child_style.height_auto || (child_style.height_percent && !out.block_known);
        const bool stretch_now = item.stretch_block && child_height_auto;
        const bool percent_now = child_style.height_percent;
        // Rows are solved only now. A subgrid on either axis is asked again so its items
        // land on these tracks, including when its border box already matched the area.
        const bool subgrid_item = axis_is_subgrid(child_style.grid_template_columns) ||
                                  axis_is_subgrid(child_style.grid_template_rows);
        publish_subgrid_span(item, out.columns, out.column_gap, &out.rows, out.row_gap);
        if (!stretch_now && !percent_now && !subgrid_item) {
            continue;
        }
        if (item.final_result == nullptr && !item.area_deferred) {
            continue;
        }
        const float area_inline = area_extent(out.columns, item.col, item.col_span, out.column_gap);
        const float available_inline =
            std::max(0.f, area_inline - item.margin.inline_start - item.margin.inline_end);
        const float available_block =
            std::max(0.f, area_extent(out.rows, item.row, item.row_span, out.row_gap) -
                              item.margin.block_start - item.margin.block_end);
        const bool inline_matches =
            item.final_result != nullptr &&
            (!item.stretch_inline ||
             std::abs(available_inline - item.final_result->geometry.border_box_inline) <= 0.5f);
        const bool block_matches =
            item.final_result != nullptr &&
            std::abs(available_block - item.final_result->geometry.border_box_block) <= 0.5f;
        if (!subgrid_item && inline_matches && block_matches) {
            continue;
        }
        ConstraintSpace stretch_space = out.base_space;
        stretch_space.request.kind = RequestKind::Final;
        stretch_space.percentage.inline_size = AxisSize{true, area_inline};
        stretch_space.containing_block.inline_size = AxisSize{true, area_inline};
        stretch_space.percentage.block_size = AxisSize{true, available_block};
        stretch_space.containing_block.block_size = AxisSize{true, available_block};
        stretch_space.available.inline_size =
            item.stretch_inline ? AxisSize{true, available_inline} : AxisSize{false, 0};
        stretch_space.available.block_size = AxisSize{true, available_block};
        // A percentage height decides its own size; only an auto height is asked to fill.
        stretch_space.stretch_block = stretch_now;
        const bool child_width_auto =
            out.horizontal ? child_style.width_auto : child_style.height_auto;
        stretch_space.stretch_inline = item.stretch_inline && child_width_auto;
        cx.reset_ask_fuse(*item.box);
        if (const LayoutResult* stretched = cx.ask(*item.box, stretch_space)) {
            item.final_result = stretched;
        }
    }
}

}  // namespace layout
}  // namespace zui
