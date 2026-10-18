// Grid layout entry point: run the three phases in order and retry with the block scrollbar
// gutter reserved when the rows overflow an `auto`/`scroll` container.

#include "layout/grid/grid.h"
#include "layout/baseline.h"
#include "layout/cache/logical_axes.h"
#include "layout/grid/detail.h"
#include "layout/scrollbar_v.h"

namespace zui {
namespace layout {



Grid::Plan Grid::solve(const Contributions& contributions, const LayoutKey& key) {
    Plan plan;
    static const LayoutStyle kEmptyStyle;
    const LayoutStyle& style = contributions.style != nullptr ? *contributions.style : kEmptyStyle;
    plan.margin = contributions.margin;
    plan.border = contributions.border;
    plan.padding = contributions.padding;
    plan.column_gap = contributions.column_gap;
    plan.row_gap = contributions.row_gap;
    plan.explicit_columns = contributions.explicit_columns;
    plan.explicit_rows = contributions.explicit_rows;
    plan.column_line_names = contributions.column_line_names;
    plan.row_line_names = contributions.row_line_names;
    plan.column_origin = contributions.column_origin;
    plan.row_origin = contributions.row_origin;
    plan.horizontal = contributions.horizontal;
    plan.items = contributions.items;
    plan.columns = contributions.columns;
    plan.rows = contributions.rows;
    plan.incoming = key.space.fragmentation.incoming;

    // The rows were solved inside measure for the stretch round; solving again keeps this function
    // the single place that decides the sizes.
    // The same constraint the measure round used: an indefinite available size is a max-content
    // constraint (CSS Grid section 12.6), so Maximize Tracks runs with infinite free space.
    const bool maximize_inline =
        !key.space.available.inline_size.definite && key.space.request.kind != RequestKind::MinContent;
    const bool maximize_block =
        !key.space.available.block_size.definite && key.space.request.kind != RequestKind::MinContent;
    size_columns(plan.columns, contributions.column_template, plan.items, plan.column_gap,
                 contributions.inline_known, contributions.column_space, style.justify_content,
                 maximize_inline);
    plan.content_inline = contributions.inline_known ? contributions.content_inline
                                                     : sum_tracks(plan.columns, plan.column_gap);
    size_rows(plan.rows, contributions.row_template, plan.items, plan.row_gap,
              contributions.block_known, contributions.content_block, style.align_content,
              maximize_block);
    plan.content_block = contributions.block_known ? contributions.content_block
                                                   : sum_tracks(plan.rows, plan.row_gap);
    plan.block_definite = contributions.block_known;

    const float column_space =
        contributions.inline_known ? contributions.column_space : plan.content_inline;
    const float columns_extent = sum_tracks(plan.columns, plan.column_gap);
    const float rows_extent = sum_tracks(plan.rows, plan.row_gap);
    const float column_free = std::max(0.f, column_space - columns_extent);
    const float row_free = std::max(0.f, plan.content_block - rows_extent);
    const Pack column_pack =
        pack_of(style.justify_content, column_free, static_cast<int>(plan.columns.size()));
    const Pack row_pack =
        pack_of(style.align_content, row_free, static_cast<int>(plan.rows.size()));
    plan.column_offsets = track_offsets(plan.columns, plan.column_gap, column_pack);
    plan.row_offsets = track_offsets(plan.rows, plan.row_gap, row_pack);
    return plan;
}


LayoutResult layout_grid(Box& box, const LayoutKey& key, LayoutContext& cx) {
    Grid::Contributions contributions = Grid::measure(box, key, cx);
    Grid::Plan plan = Grid::solve(contributions, key);
    LayoutResult out = Grid::place(box, plan, cx, key.space.fragmentation, false);

    // A block scrollbar shrinks the inline space the columns may use. Deciding that needs one pass,
    // so the whole thing runs again with the gutter reserved and the ask fuse cleared.
    const LayoutStyle& style = style_of(box);
    const OverflowMode block_overflow =
        is_horizontal_wm(style.writing_mode) ? style.overflow_y : style.overflow_x;
    const bool ox = style.overflow_x != OverflowMode::Visible;
    const bool oy = style.overflow_y != OverflowMode::Visible;
    // `scrollbar-gutter: stable` reserves the gutter on the first overflowing axis even
    // when the rows fit. `overflow: scroll` and overflowing `auto` still do the classic pass.
    const bool need_block_bar =
        overflow_eats_inline(style.writing_mode, ox, oy) &&
        block_overflow != OverflowMode::Visible &&
        (scrollbar_gutter_forces_reserve(style) || block_overflow == OverflowMode::Scroll ||
         (block_overflow == OverflowMode::Auto &&
          sum_tracks(plan.rows, plan.row_gap) > plan.content_block + 0.5f));
    if (!need_block_bar) {
        return out;
    }
    cx.note_grid_bar_rerun();
    for (Box* child : box.children) {
        if (child != nullptr) {
            cx.keep_latest_ask(*child);
            cx.reset_ask_fuse(*child);
        }
    }
    contributions = Grid::measure(box, key, cx, true);
    plan = Grid::solve(contributions, key);
    return Grid::place(box, plan, cx, key.space.fragmentation, true);
}

}  // namespace layout
}  // namespace zui
