// Grid layout: run the solved plan against the box children, writing used sizes, offsets, and
// fragmentation state back onto the boxes.

#include "layout/grid/grid.h"
#include "layout/baseline.h"
#include "layout/cache/used_length.h"
#include "layout/grid/detail.h"
#include "layout/scrollbar_v.h"

#include <algorithm>
#include <cstdio>

namespace zui {
namespace layout {
LayoutResult Grid::place(Box& box, const Plan& plan, LayoutContext& cx,
                         const FragmentationContext& frag, bool reserve_block_bar) {
    LayoutResult out;
    out.converged = true;
    out.margin_strut.blocks_margin_collapse = true;
    out.geometry.margin = plan.margin;
    out.geometry.border = plan.border;
    out.geometry.padding = plan.padding;
    out.margin_strut.start = plan.margin.block_start;
    out.margin_strut.end = plan.margin.block_end;

    const LayoutStyle& style = style_of(box);
    const float gutter_start = scrollbar_gutter_start(
        style, scrollbar_used_thickness(style, cx.scrollbar_thickness()), reserve_block_bar);
    const float pad_inline = plan.padding.inline_start + plan.padding.inline_end;
    const float pad_block = plan.padding.block_start + plan.padding.block_end;
    const bool scroll_container = scrolls(style);
    // Fragmentation (LAYOUT.md section 12): a grid container breaks between its row tracks. The
    // rows already placed by earlier fragments are those that end at or above the incoming offset,
    // and the first row that does not fit whole opens the next fragment - unless nothing has been
    // placed here yet, in which case that row stays and overflows (an empty fragment would make no
    // progress), which is exactly the amount the container's balancing stretches by.
    const float chrome_start = plan.border.block_start + plan.padding.block_start;
    const float chrome_end = plan.border.block_end + plan.padding.block_end;
    const float own_start = frag.active ? std::max(0.f, frag.incoming.block_offset) : 0.f;
    const float frag_limit = frag.active ? own_start + std::max(0.f, frag.remaining_block) : 0.f;
    auto row_end = [&](std::size_t row) {
        if (row >= plan.rows.size() || row >= plan.row_offsets.size()) {
            return chrome_start;
        }
        const float offset = plan.row_offsets[row];
        const float size = plan.rows[row].base;
        return chrome_start + offset + size;
    };
    // The row this fragment resumes at: every row that ends above the incoming offset was placed
    // by an earlier fragment.
    std::size_t resume_row = 0;
    while (frag.active && resume_row < plan.rows.size() &&
           row_end(resume_row) <= own_start + 0.01f) {
        ++resume_row;
    }
    std::size_t break_row = resume_row;
    if (frag.active) {
        while (break_row < plan.rows.size() && row_end(break_row) <= frag_limit + 0.01f) {
            ++break_row;
        }
        if (break_row == resume_row && resume_row < plan.rows.size() && !frag.at_fragment_start) {
            // The fragment already holds content: this row opens the next one.
        } else if (break_row == resume_row && resume_row < plan.rows.size()) {
            ++break_row;  // keep it, and overflow
        }
    } else {
        break_row = plan.rows.size();
    }
    const bool rows_finished = break_row >= plan.rows.size();

    // Baseline alignment (CSS Grid section 12, CSS Box Alignment section 9): the items of one row
    // that ask for `baseline` share one baseline, and each is offset so that its own first baseline
    // lands there. `last baseline` shares max(block − last) from the area end (same packing as
    // flex). A box that reports no baseline is measured by its block-end margin edge, which is what
    // the spec falls back to when a box has no baseline of its own.
    //
    // Inline-axis first baseline (`justify-items:baseline`) stays start under horizontal-tb: Chrome
    // falls back the same way — alphabetic baselines have no position along the inline axis. Last
    // baseline on the inline axis stays end.
    const float no_baseline = std::numeric_limits<float>::quiet_NaN();
    auto item_baseline = [](const GridItem& item) {
        if (item.final_result == nullptr) {
            return 0.f;
        }
        const float baseline = item.final_result->geometry.baselines.first;
        return std::isfinite(baseline) ? baseline : item.final_result->geometry.border_box_block;
    };
    auto item_last_baseline = [](const GridItem& item) {
        if (item.final_result == nullptr) {
            return 0.f;
        }
        const float baseline = item.final_result->geometry.baselines.last;
        return std::isfinite(baseline) ? baseline : item.final_result->geometry.border_box_block;
    };
    std::vector<float> row_baselines(plan.rows.size(), no_baseline);
    std::vector<float> row_last_from_end(plan.rows.size(), no_baseline);
    // The scrollable reach of the items this pass places, measured from the container's content box
    // origin (LAYOUT.md section 4). The item offsets below are relative to the padding box, so the
    // leading padding comes off before the item's border box size is added - the same shape flex
    // uses for `reach_main` (flex.cpp). A fragmented pass reports a slice whose origin does not line
    // up with the padding box; the union across fragmentainers is a separate design question, so the
    // reach is only tracked for the non-fragmented pass.
    float reach_inline = 0.f;
    float reach_block = 0.f;
    for (const GridItem& item : plan.items) {
        if (item.row < 0) {
            continue;
        }
        const std::size_t row = static_cast<std::size_t>(item.row);
        if (row >= row_baselines.size()) {
            continue;
        }
        if (item.align_block == FlexAlign::Baseline) {
            const float offset = item.margin.block_start + item_baseline(item);
            if (!std::isfinite(row_baselines[row]) || offset > row_baselines[row]) {
                row_baselines[row] = offset;
            }
        } else if (item.align_block == FlexAlign::LastBaseline && item.final_result != nullptr) {
            const float used = item.final_result->geometry.border_box_block;
            const float from_end = used - item_last_baseline(item);
            if (!std::isfinite(row_last_from_end[row]) || from_end > row_last_from_end[row]) {
                row_last_from_end[row] = from_end;
            }
        }
    }

    for (const GridItem& item : plan.items) {
        // Fragmentation: an item whose row belongs to an earlier fragment was placed by that
        // fragment's pass, and one that starts at or below the break opens the next fragment.
        const int item_row = std::max(0, item.row);
        if (static_cast<std::size_t>(item_row) < resume_row ||
            static_cast<std::size_t>(item_row) >= break_row) {
            continue;
        }
        const float area_inline =
            area_extent(plan.columns, item.col, item.col_span, plan.column_gap);
        const float area_block = area_extent(plan.rows, item.row, item.row_span, plan.row_gap);
        const float used_inline = item.final_result != nullptr
                                      ? item.final_result->geometry.border_box_inline
                                      : item.used_inline;
        const float used_block = item.final_result != nullptr
                                     ? item.final_result->geometry.border_box_block
                                     : item.used_block;
        const float inner_inline =
            std::max(0.f, area_inline - item.margin.inline_start - item.margin.inline_end);
        const float inner_block =
            std::max(0.f, area_block - item.margin.block_start - item.margin.block_end);
        const LayoutStyle& item_style = style_of(*item.box);
        float align_inline = 0;
        float align_block = 0;
        if (!item.stretch_inline) {
            const FlexAlign align = item.align_inline;
            if (align == FlexAlign::Center) {
                align_inline = (inner_inline - used_inline) * 0.5f;
            } else if (align == FlexAlign::End || align == FlexAlign::LastBaseline) {
                // Inline-axis last baseline → end (no alphabetic position along inline under
                // horizontal-tb; matches Chrome).
                align_inline = inner_inline - used_inline;
            }
            // First baseline on the inline axis keeps start (align_inline == 0): Chrome falls
            // back the same way for horizontal grids — alphabetic baselines have no inline-axis
            // position.
        }
        if (!item.stretch_block) {
            const FlexAlign align = item.align_block;
            if (align == FlexAlign::Center) {
                align_block = (inner_block - used_block) * 0.5f;
            } else if (align == FlexAlign::End) {
                align_block = inner_block - used_block;
            } else if (align == FlexAlign::Baseline && item.row >= 0 &&
                       static_cast<std::size_t>(item.row) < row_baselines.size() &&
                       std::isfinite(row_baselines[static_cast<std::size_t>(item.row)])) {
                align_block = row_baselines[static_cast<std::size_t>(item.row)] -
                              (item.margin.block_start + item_baseline(item));
            } else if (align == FlexAlign::LastBaseline && item.row >= 0 &&
                       static_cast<std::size_t>(item.row) < row_last_from_end.size() &&
                       std::isfinite(row_last_from_end[static_cast<std::size_t>(item.row)]) &&
                       item.final_result != nullptr &&
                       std::isfinite(item.final_result->geometry.baselines.last)) {
                // Mirror flex last-baseline: share max(block − last) from the area end.
                align_block = inner_block - item_last_baseline(item) -
                              row_last_from_end[static_cast<std::size_t>(item.row)];
            } else if (align == FlexAlign::LastBaseline) {
                // No shared last baseline → end fallback (CSS Box Alignment §9).
                align_block = inner_block - used_block;
            }
        }
        LogicalPoint placed;
        // Relative to the container's padding box (LAYOUT section 4): the border is added once
        // by the paint/hit walk, not here.
        placed.inline_offset = plan.padding.inline_start + gutter_start +
                               plan.column_offsets[static_cast<std::size_t>(item.col)] +
                               item.margin.inline_start + align_inline;
        placed.block_offset = plan.padding.block_start +
                              plan.row_offsets[static_cast<std::size_t>(item.row)] +
                              item.margin.block_start + align_block;
        item.box->placement.cb_offset = placed;
        item.box->placement.sticky_offset = {};
        // LAYOUT.md section 4: the container's ink reaches wherever its items' ink does.
        if (item.final_result != nullptr) {
            rect_include(out.overflow.ink,
                         child_ink_in_box(item.final_result->overflow,
                                          item.final_result->geometry.border_box_inline,
                                          item.final_result->geometry.border_box_block,
                                          style_of(box).writing_mode, item_style.writing_mode,
                                          plan.border, item.box->placement.cb_offset,
                                          clips_overflow(item_style.overflow_x,
                                                         item_style.overflow_y)));
        }
        ChildPlace recorded;
        recorded.box = item.box;
        recorded.inline_offset = placed.inline_offset;
        recorded.block_offset = placed.block_offset;
        out.child_places.push_back(recorded);
        if (!frag.active) {
            // Chrome unions the in-flow children's border boxes for the scrollable overflow, and a
            // box that is empty on an axis contributes nothing to that axis: a row-gap-created
            // zero-height item (`grid-auto-fill-outside`) must not push `scrollHeight` past the
            // container, while a zero-width item must not push `scrollWidth`. Measured against the
            // pinned browser: a 0-height item at the bottom of a grid leaves `scrollHeight` at the
            // client box, and a 0-height but nonzero-width one still reports its width.
            if (used_inline > 0.f) {
                // The both-edges start gutter is part of the scrollport, not of the content,
                // so it does not grow scrollWidth.
                reach_inline = std::max(
                    reach_inline, (placed.inline_offset - plan.padding.inline_start - gutter_start) +
                                      used_inline);
            }
            if (used_block > 0.f) {
                reach_block = std::max(
                    reach_block, (placed.block_offset - plan.padding.block_start) + used_block);
            }
            // Convergence self-check (the grid half of P3-5): a track whose size came from its
            // items' contributions has to still hold them after the Final asks. An item that comes
            // back past an intrinsic row or column means the track was sized from a contribution
            // the item no longer reports, which is the two-round ceiling the flex path already
            // reports. A fixed track letting its item overflow is normal and stays silent.
            const std::size_t row = static_cast<std::size_t>(item_row);
            const std::size_t col = static_cast<std::size_t>(std::max(0, item.col));
            if (row < plan.rows.size() && plan.rows[row].max_intrinsic &&
                used_block > area_block + 0.5f) {
                out.converged = false;
                char note[192];
                std::snprintf(note, sizeof(note),
                              "an item came back taller than the intrinsic row that sized it "
                              "(area %g, final %g; LAYOUT section 5 two-round ceiling)",
                              static_cast<double>(area_block), static_cast<double>(used_block));
                cx.note_non_convergence(&box, note);
            }
            if (col < plan.columns.size() && plan.columns[col].max_intrinsic &&
                used_inline > area_inline + 0.5f) {
                out.converged = false;
                char note[192];
                std::snprintf(note, sizeof(note),
                              "an item came back wider than the intrinsic column that sized it "
                              "(area %g, final %g; LAYOUT section 5 two-round ceiling)",
                              static_cast<double>(area_inline), static_cast<double>(used_inline));
                cx.note_non_convergence(&box, note);
            }
        }
        if (scroll_container && box.node != nullptr && item_style.position_sticky) {
            const float cb_inline =
                plan.content_inline + pad_inline - style.border_left - style.border_right;
            const float cb_block =
                plan.content_block + pad_block - style.border_top - style.border_bottom;
            const WritingMode wm = style.writing_mode;
            const float scroll_inline =
                is_horizontal_wm(wm) ? box.node->scroll_x : box.node->scroll_y;
            const float scroll_block =
                is_horizontal_wm(wm) ? box.node->scroll_y : box.node->scroll_x;
            item.box->placement.sticky_offset =
                sticky_shift(item_style, placed, used_inline, used_block, cb_inline, cb_block,
                             scroll_inline, scroll_block);
        }
    }

    // Out-of-flow children are asked by the containing block once the border box is known
    // (section 9.2 of the grid spec gives them the grid area, the out-of-flow protocol in
    // `cache/out_of_flow` uses the padding box, so this places the static position inside the area
    // and leaves the containing block itself to that protocol).
    for (Box* child : box.children) {
        if (child == nullptr || !out_of_flow(*child)) {
            continue;
        }
        const LayoutStyle& child_style = style_of(*child);
        const AxisArea col_area =
            area_of(child_style.grid_column_start, child_style.grid_column_end,
                    plan.explicit_columns, plan.column_line_names);
        const AxisArea row_area = area_of(child_style.grid_row_start, child_style.grid_row_end,
                                          plan.explicit_rows, plan.row_line_names);
        LogicalPoint local;
        if (cx.origin_at_padding_edge()) {
            local.inline_offset = plan.border.inline_start + plan.padding.inline_start;
            local.block_offset = plan.border.block_start + plan.padding.block_start;
        }
        const bool col_definite = !col_area.is_auto && !plan.column_offsets.empty();
        const bool row_definite = !row_area.is_auto && !plan.row_offsets.empty();
        int col_index = 0;
        int row_index = 0;
        if (col_definite) {
            col_index = std::min(col_area.index + plan.column_origin,
                                 static_cast<int>(plan.column_offsets.size()) - 1);
            col_index = std::max(0, col_index);
            local.inline_offset += plan.column_offsets[static_cast<std::size_t>(col_index)];
        }
        if (row_definite) {
            row_index = std::min(row_area.index + plan.row_origin,
                                 static_cast<int>(plan.row_offsets.size()) - 1);
            row_index = std::max(0, row_index);
            local.block_offset += plan.row_offsets[static_cast<std::size_t>(row_index)];
        }
        // The area origin stays on the track (or the padding edge). Alignment moves only the
        // static position inside that area: `place-items: center` puts a 1px `margin: -1px`
        // box at the centre of the padding box (file-explorer `.sr` is 17.5px into a 36px
        // button). A definite grid area is still the containing block for insets.
        LogicalPoint area_origin = local;
        float area_inline = plan.content_inline + plan.padding.inline_start + plan.padding.inline_end;
        float area_block = plan.content_block + plan.padding.block_start + plan.padding.block_end;
        if (col_definite && row_definite) {
            area_inline = area_extent(plan.columns, col_index, col_area.span, plan.column_gap);
            area_block = area_extent(plan.rows, row_index, row_area.span, plan.row_gap);
        } else {
            // Auto lines: the static-position area is the grid's padding box (CSS Grid §9.2),
            // and `local` was the content origin.
            local.inline_offset -= plan.padding.inline_start;
            local.block_offset -= plan.padding.block_start;
            area_origin = local;
        }
        const WritingMode mode = style.writing_mode;
        const Direction direction = style.direction;
        const AxisSize inline_basis{true, std::max(0.f, plan.content_inline)};
        const AxisSize block_basis{plan.block_definite,
                                   plan.block_definite ? std::max(0.f, plan.content_block) : 0.f};
        const Edges child_margin = logical_margin(mode, direction, child_style, inline_basis);
        const Edges child_pad = padding_edges(mode, direction, child_style, inline_basis);
        const Edges child_border = border_edges(mode, direction, child_style);
        const float chrome_inline = child_pad.inline_start + child_pad.inline_end +
                                    child_border.inline_start + child_border.inline_end;
        const float chrome_block = child_pad.block_start + child_pad.block_end +
                                   child_border.block_start + child_border.block_end;
        bool width_as_auto = false;
        bool height_as_auto = false;
        const float specified_w =
            used_main(child_style.width_auto, child_style.width_percent, child_style.width_px,
                      inline_basis, &width_as_auto);
        const float specified_h =
            used_main(child_style.height_auto, child_style.height_percent, child_style.height_px,
                      block_basis, &height_as_auto);
        const bool inline_definite = !child_style.width_auto && !width_as_auto &&
                                     !(child_style.width_percent && !inline_basis.definite);
        const bool block_size_definite = !child_style.height_auto && !height_as_auto &&
                                         !(child_style.height_percent && !block_basis.definite);
        const float border_inline =
            inline_definite
                ? border_from_specified(specified_w, child_style.border_box, chrome_inline)
                : 0.f;
        const float border_block =
            block_size_definite
                ? border_from_specified(specified_h, child_style.border_box, chrome_block)
                : 0.f;
        const FlexAlign align_inline = child_style.justify_self == FlexAlign::Auto
                                           ? style.justify_items
                                           : child_style.justify_self;
        const FlexAlign align_block = child_style.align_self == FlexAlign::Auto ? style.align_items
                                                                                : child_style.align_self;
        auto aligned = [](FlexAlign align, bool definite, float area, float border_size,
                          float margin_start, float margin_end) {
            // Stretch and start keep the margin edge on the area start. An auto size is not
            // aligned here: the out-of-flow ask still shrinks it to fit.
            if (!definite || align == FlexAlign::Stretch || align == FlexAlign::Start ||
                align == FlexAlign::Auto || align == FlexAlign::Baseline ||
                align == FlexAlign::LastBaseline) {
                return margin_start;
            }
            const float inner = std::max(0.f, area - margin_start - margin_end);
            float shift = 0.f;
            if (align == FlexAlign::Center) {
                shift = (inner - border_size) * 0.5f;
            } else if (align == FlexAlign::End) {
                shift = inner - border_size;
            }
            return margin_start + shift;
        };
        local.inline_offset += aligned(align_inline, inline_definite, area_inline, border_inline,
                                       child_margin.inline_start, child_margin.inline_end);
        local.block_offset += aligned(align_block, block_size_definite, area_block, border_block,
                                      child_margin.block_start, child_margin.block_end);
        child->placement = {};
        child->has_static_local = true;
        child->static_local = local;
        if (col_definite && row_definite) {
            // A definitely placed child takes its grid area as its containing block: percentages and
            // insets resolve against the area, and the area's start is the static position
            // (CSS Grid section 9.2). The area has no box, so it travels as an override.
            LayoutContext::AreaContainingBlock area;
            area.set = true;
            area.origin = area_origin;
            area.inline_size = area_inline;
            area.block_size = area_block;
            cx.note_out_of_flow(*child, local, &area);
        } else {
            cx.note_out_of_flow(*child, local);
        }
    }

    const float chrome_inline = plan.border.inline_start + plan.border.inline_end + pad_inline;
    const float chrome_block = plan.border.block_start + plan.border.block_end + pad_block;
    // The fragment's own extent: the rows it placed (or the hole the first row overflows into),
    // cut out of the container's own block axis by `cut_fragment`.
    FragmentCut cut;
    if (frag.active) {
        const float placed_bottom =
            break_row == resume_row
                ? own_start
                : std::max(0.f,
                           row_end(break_row - 1) - (resume_row == 0 ? chrome_start : own_start));
        const float first_unplaced =
            break_row < plan.rows.size() ? row_end(break_row) - frag_limit : 0.f;
        // The child index the next fragment resumes at: the first item of the row it broke before.
        int next_child = -1;
        if (break_row < plan.rows.size()) {
            for (std::size_t index = 0; index < box.children.size(); ++index) {
                const Box* child = box.children[index];
                if (child == nullptr) {
                    continue;
                }
                for (const GridItem& item : plan.items) {
                    if (item.box == child && std::max(0, item.row) >= static_cast<int>(break_row)) {
                        next_child = static_cast<int>(index);
                        break;
                    }
                }
                if (next_child >= 0) {
                    break;
                }
            }
        }
        cut = cut_fragment(own_start, frag.remaining_block, chrome_start, chrome_end, placed_bottom,
                           plan.block_definite ? chrome_block + plan.content_block : -1.f,
                           rows_finished, next_child, 0, first_unplaced);
    } else {
        cut.border_block = plan.content_block + chrome_block;
        cut.content_block = plan.content_block;
    }
    out.geometry.content_inline = std::max(0.f, plan.content_inline);
    out.geometry.content_block = std::max(0.f, cut.content_block);
    out.geometry.border_box_inline = out.geometry.content_inline + chrome_inline;
    out.geometry.border_box_block =
        frag.active ? cut.border_block : out.geometry.content_block + chrome_block;
    // LAYOUT.md section 9: a grid container reports `scrollWidth` / `scrollHeight` like any other
    // scroll container instead of falling back to its padding box (paint_map). The client box loses
    // the reserved block scrollbar first, the same way flex passes the narrowed inline size when
    // `reserve_block_bar` is set - the gutter comes out of `column_space`, not the used content box.
    const float client_content_inline = std::max(
        0.f, plan.content_inline - scrollbar_gutter_span(
                 style, scrollbar_used_thickness(style, cx.scrollbar_thickness()), reserve_block_bar));
    out.overflow.scrollable =
        scrollable_extent(plan.padding.inline_start, plan.padding.inline_end,
                          plan.padding.block_start, plan.padding.block_end, client_content_inline,
                          out.geometry.content_block, std::max(client_content_inline, reach_inline),
                          std::max(out.geometry.content_block, reach_block), scroll_container);
    if (frag.active) {
        out.break_token = cut.token;
        out.space_shortage = cut.space_shortage;
        if (own_start > 0.01f) {
            out.geometry.margin.block_start = 0;
        }
        if (!rows_finished) {
            out.geometry.margin.block_end = 0;
        }
    }
    out.geometry.baselines = {};
    // A grid container's own baseline set (CSS Grid section 8.5): the first item of the first row.
    // An item that has a baseline of its own hands that one over; an item without one contributes
    // the block-end edge of its border box (CSS Box Alignment section 9.2). Measured against
    // Chrome: with two first-row items of different baselines the container reports the first item
    // in document order rather than the largest, and an item's block-end margin is not part of the
    // value it contributes. The item's own position carries its block-start margin.
    //
    // `row_baselines` above is a different thing: it holds only the items that asked for baseline
    // alignment, and it is what those items are packed against.
    int first_row = -1;
    int last_row = -1;
    for (const GridItem& item : plan.items) {
        if (item.final_result == nullptr) {
            continue;
        }
        const int row = std::max(0, item.row);
        if (static_cast<std::size_t>(row) >= plan.row_offsets.size()) {
            continue;
        }
        if (first_row < 0 || row < first_row) {
            first_row = row;
        }
        last_row = std::max(last_row, row);
    }
    if (first_row >= 0) {
        const auto item_baseline = [](const GridItem& item, bool last) {
            const BaselineSet& set = item.final_result->geometry.baselines;
            const float own = last ? set.last : set.first;
            return std::isfinite(own) ? own : synthesized_item_baseline(*item.final_result);
        };
        // The item's own placement is what the container's baseline is measured through, so the row
        // offset, the item's block-start margin and whichever alignment moved it are all inside
        // `cb_offset` already (it is relative to the container's padding box).
        const auto container_baseline = [&](const GridItem& item, bool last) {
            return plan.border.block_start + item.box->placement.cb_offset.block_offset +
                   item_baseline(item, last);
        };
        // Document order decides the first row and the last row alike.
        for (const GridItem& item : plan.items) {
            if (item.final_result != nullptr && std::max(0, item.row) == first_row) {
                out.geometry.baselines.first = container_baseline(item, false);
                break;
            }
        }
        for (auto it = plan.items.rbegin(); it != plan.items.rend(); ++it) {
            if (it->final_result != nullptr && std::max(0, it->row) == last_row) {
                out.geometry.baselines.last = container_baseline(*it, true);
                break;
            }
        }
    }

    return out;
}
}  // namespace layout
}  // namespace zui
