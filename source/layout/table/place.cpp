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
namespace table_detail {

void layout_table_place(TablePlace& t) {
    Box& box = *t.box;
    const LayoutKey& key = *t.key;
    LayoutContext& cx = *t.cx;
    LayoutResult& out = *t.out;
    const LayoutStyle& style = *t.style;
    const WritingMode mode = t.mode;
    const Direction direction = t.direction;
    const Edges& padding = t.padding;
    const Edges& border = t.border;
    const float chrome_inline = t.chrome_inline;
    const float chrome_block = t.chrome_block;
    const float self_margin_block = t.self_margin_block;
    const bool height_auto = t.height_auto;
    const float height_used = t.height_used;
    const bool flex_block = t.flex_block;
    const bool grid_stretch = t.grid_stretch;
    Model& model = *t.model;
    const ConstraintSpace& base_cell_space = t.base_cell_space;
    const float spacing_inline = t.spacing_inline;
    const float spacing_block = t.spacing_block;
    const float edge_spacing = t.edge_spacing;
    float grid_target = t.grid_target;
    float content_inline = t.content_inline;
    bool converged = true;
    const int columns = model.columns;
    (void)mode;
    (void)direction;
    (void)edge_spacing;

    for (Row& row : model.rows) {
        for (Cell& cell : row.cells) {
            // A `visibility: collapse` cell - or one in a collapsed row - keeps its inline size;
            // only the block axis collapses (CSS 2.1 section 17.5.5). Zeroing it here made the
            // cell's content wrap one character per line (measured on `example/tests/table`'s
            // `tr.gone`: the browser keeps `w` 57/31 and one line, this engine reported 0 width and
            // three lines). A collapsed *column* still takes its width out of the span, which
            // `span_width_visible` already does.
            cell.used_inline = span_width_visible(model, cell.col, cell.colspan, spacing_inline);
        }
    }

    // Row heights: each cell is asked at the width its columns solved and its own content height is
    // what the row has to be at least.
    for (Row& row : model.rows) {
        for (Cell& cell : row.cells) {
            if (cell.box == nullptr) {
                continue;
            }
            // A collapsed cell is still measured. The placement ask below re-pins its
            // border box at 0; this height is what the row reports as scrollHeight.
            const LayoutStyle& cell_style = style_of(*cell.box);
            ConstraintSpace space =
                child_space(base_cell_space, cell_style, *cell.box, cell.used_inline, 0.f, false,
                            false, RequestKind::Final, Axis::Block);
            space.percentage.inline_size = {true, cell.used_inline};
            space.containing_block.inline_size = space.percentage.inline_size;
            const LayoutResult* result = cx.ask(*cell.box, space);
            cell.content_block = result != nullptr ? result->geometry.border_box_block : 0.f;
            cx.keep_latest_ask(*cell.box);
            cx.reset_ask_fuse(*cell.box);
        }
        if (row.collapsed) {
            float height = 0.f;
            for (const Cell& cell : row.cells) {
                if (cell.rowspan == 1) {
                    height = std::max(height, cell.content_block);
                }
            }
            row.content_block = height;
            row.block = 0;
            continue;
        }
        float height = row.cells.empty() ? 0.f : 0.f;
        for (const Cell& cell : row.cells) {
            if (cell.rowspan == 1) {
                height = std::max(height, cell.content_block);
            }
        }
        row.block = height;
    }
    // A spanning cell taller than the rows it covers pushes them apart (CSS 2.1 section 17.5.3).
    for (Row& row : model.rows) {
        for (const Cell& cell : row.cells) {
            if (cell.rowspan <= 1 || cell.collapsed) {
                continue;
            }
            float spanned = 0;
            int counted = 0;
            for (int r = cell.row;
                 r < cell.row + cell.rowspan && r < static_cast<int>(model.rows.size()); ++r) {
                if (!model.rows[static_cast<std::size_t>(r)].collapsed) {
                    spanned += model.rows[static_cast<std::size_t>(r)].block;
                    ++counted;
                }
            }
            spanned += spacing_block * static_cast<float>(std::max(0, counted - 1));
            if (cell.content_block - spanned <= 0.f || counted == 0) {
                continue;
            }
            const float share = (cell.content_block - spanned) / static_cast<float>(counted);
            for (int r = cell.row;
                 r < cell.row + cell.rowspan && r < static_cast<int>(model.rows.size()); ++r) {
                if (!model.rows[static_cast<std::size_t>(r)].collapsed) {
                    model.rows[static_cast<std::size_t>(r)].block += share;
                }
            }
        }
    }
    // A cell that spans rows is as tall as the rows it covers together (CSS 2.1 section 17.5.3).
    // The row heights are final now, which is why this runs after them.
    for (Row& row : model.rows) {
        for (Cell& cell : row.cells) {
            if (cell.rowspan <= 1) {
                continue;
            }
            float spanned = 0;
            int counted = 0;
            for (int r = cell.row;
                 r < cell.row + cell.rowspan && r < static_cast<int>(model.rows.size()); ++r) {
                if (model.rows[static_cast<std::size_t>(r)].collapsed) {
                    continue;
                }
                if (counted > 0) {
                    spanned += spacing_block;
                }
                spanned += model.rows[static_cast<std::size_t>(r)].block;
                ++counted;
            }
            cell.used_block = spanned;
        }
    }

    // Captions sit outside the grid but inside the table's box (CSS 2.1 section 17.4). They span
    // the table's content width.
    std::vector<std::pair<Box*, float>> captions_top;
    std::vector<std::pair<Box*, float>> captions_bottom;
    float caption_top_block = 0;
    float caption_bottom_block = 0;
    auto measure_caption = [&](Box* caption) {
        const LayoutStyle& caption_style = style_of(*caption);
        ConstraintSpace space =
            child_space(base_cell_space, caption_style, *caption, content_inline, 0.f, false, false,
                        RequestKind::Final, Axis::Block);
        const LayoutResult* result = cx.ask(*caption, space);
        cx.keep_latest_ask(*caption);
        cx.reset_ask_fuse(*caption);
        return result != nullptr ? result->geometry.border_box_block : 0.f;
    };
    for (Box* caption : model.captions_top) {
        const float height = measure_caption(caption);
        captions_top.emplace_back(caption, height);
        caption_top_block += height;
    }
    for (Box* caption : model.captions_bottom) {
        const float height = measure_caption(caption);
        captions_bottom.emplace_back(caption, height);
        caption_bottom_block += height;
    }

    // The grid's block extent: the rows, the spacing between them and the leading/trailing spacing.
    float grid_block = 0;
    int visible_rows = 0;
    for (const Row& row : model.rows) {
        if (row.collapsed) {
            continue;
        }
        if (visible_rows > 0) {
            grid_block += spacing_block;
        }
        grid_block += row.block;
        ++visible_rows;
    }
    if (visible_rows > 0) {
        grid_block += spacing_block * 2.f;
    }
    float content_block = caption_top_block + grid_block + caption_bottom_block;
    float block_specified = 0;
    bool block_known = false;
    if (flex_block || grid_stretch) {
        block_specified =
            std::max(0.f, key.space.available.block_size.px - self_margin_block - chrome_block);
        block_known = true;
    } else if (!height_auto) {
        block_specified = content_from_specified(height_used, style.border_box, chrome_block);
        block_known = true;
    }
    if (block_known) {
        content_block = block_specified;
    }

    // Place: captions first, then the row groups and rows, then the cells inside their row.
    const float grid_y = caption_top_block + (visible_rows > 0 ? spacing_block : 0.f);
    float caption_y = 0;
    for (const std::pair<Box*, float>& entry : captions_top) {
        entry.first->placement.cb_offset.inline_offset = padding.inline_start;
        entry.first->placement.cb_offset.block_offset = padding.block_start + caption_y;
        entry.first->placement.sticky_offset = {};
        caption_y += entry.second;
    }
    float bottom_y = caption_top_block + grid_block;
    for (const std::pair<Box*, float>& entry : captions_bottom) {
        entry.first->placement.cb_offset.inline_offset = padding.inline_start;
        entry.first->placement.cb_offset.block_offset = padding.block_start + bottom_y;
        entry.first->placement.sticky_offset = {};
        bottom_y += entry.second;
    }

    // Where every row goes, computed before anything is asked: a row group's extent follows from
    // it, and asking a group lays its rows out again at the group's own width.
    std::vector<float> row_offset(model.rows.size(), 0.f);
    float row_y = grid_y;
    for (std::size_t i = 0; i < model.rows.size(); ++i) {
        row_offset[i] = row_y;
        if (!model.rows[i].collapsed) {
            row_y += model.rows[i].block + spacing_block;
        }
    }
    // Fragmentation (LAYOUT.md section 12): a table breaks between its rows. Rows that end at or
    // above the incoming offset belong to earlier fragments, and the first row that does not fit
    // whole opens the next one - unless nothing was placed here yet, in which case it stays and
    // overflows, because an empty fragment makes no progress.
    const FragmentationContext& frag = key.space.fragmentation;
    const float own_start = frag.active ? std::max(0.f, frag.incoming.block_offset) : 0.f;
    const float frag_limit = frag.active ? own_start + std::max(0.f, frag.remaining_block) : 0.f;
    auto row_bottom = [&](std::size_t row) {
        return row < row_offset.size() ? row_offset[row] + model.rows[row].block : grid_y;
    };
    std::size_t resume_row = 0;
    while (frag.active && resume_row < model.rows.size() &&
           row_bottom(resume_row) <= own_start + 0.01f) {
        ++resume_row;
    }
    std::size_t break_row = resume_row;
    if (frag.active) {
        while (break_row < model.rows.size() && row_bottom(break_row) <= frag_limit + 0.01f) {
            ++break_row;
        }
        if (break_row == resume_row && resume_row < model.rows.size() && frag.at_fragment_start) {
            ++break_row;  // keep the first row, and overflow
        }
    } else {
        break_row = model.rows.size();
    }
    const bool rows_finished = break_row >= model.rows.size();
    std::vector<std::pair<Box*, std::pair<float, float>>> group_bounds;
    for (std::size_t i = 0; i < model.rows.size(); ++i) {
        Box* group = model.rows[i].group;
        if (group == nullptr) {
            continue;
        }
        bool seen = false;
        for (std::pair<Box*, std::pair<float, float>>& entry : group_bounds) {
            if (entry.first != group) {
                continue;
            }
            entry.second.second =
                std::max(entry.second.second, row_offset[i] + model.rows[i].block);
            seen = true;
            break;
        }
        if (!seen) {
            group_bounds.emplace_back(
                group, std::make_pair(row_offset[i], row_offset[i] + model.rows[i].block));
        }
    }
    // The cells come first among the table parts: the rows and the groups below only carry the size
    // the table solved, and their scrollable overflow has to see where the cells actually landed.
    //
    // The table's own first baseline comes out of this loop (its first row's first cell), and is
    // written into `out` at the end because the final block clears the set.
    float first_row_baseline = std::numeric_limits<float>::quiet_NaN();
    for (std::size_t i = 0; i < model.rows.size(); ++i) {
        if (i < resume_row || i >= break_row) {
            continue;  // another fragment holds this row
        }
        Row& row = model.rows[i];
        for (Cell& cell : row.cells) {
            if (cell.box == nullptr) {
                continue;
            }
            // The border spacing sits around the grid as well as between its tracks; the leading
            // one is already carried by the row's own placement, so only the interior gaps show up
            // here. Collapsed columns contribute neither width nor a spacing slot.
            const float x = track_inline_start(model, cell.col, spacing_inline);
            // In the collapsing model the cell's box and the row's box are the same rectangle: both
            // run from border line centre to border line centre. `cb_offset` is relative to the
            // row's padding box, so the row's own half borders come back off.
            float cell_inline = x;
            float cell_offset_block = 0.f;
            if (model.collapse && row.box != nullptr) {
                const Edges row_border = border_edges(mode, direction, style_of(*row.box));
                cell_inline -= row_border.inline_start;
                cell_offset_block -= row_border.block_start;
            }
            cell.box->placement.cb_offset.inline_offset = cell_inline;
            cell.box->placement.cb_offset.block_offset = cell_offset_block;
            cell.box->placement.sticky_offset = {};
            const LayoutStyle& cell_style = style_of(*cell.box);
            if (cell.collapsed) {
                // `visibility: collapse`: the cell keeps its place but has no extent on the block
                // axis, and it does not add a border spacing slot (CSS 2.1 section 17.6.2).
                ConstraintSpace space =
                    child_space(base_cell_space, cell_style, *cell.box, cell.used_inline, 0.f, true,
                                true, RequestKind::Final, Axis::Block);
                (void)cx.ask(*cell.box, space);
                cx.keep_latest_ask(*cell.box);
                cx.reset_ask_fuse(*cell.box);
                continue;
            }
            // A cell fills its row on the block axis whether or not its content needed the height.
            const float cell_block = cell.used_block > 0.f ? cell.used_block : row.block;
            // The height ask above already stored a Final at this column width. When that border
            // block is the row, stretching again rebuilds the same box (the row later reads the
            // pin). A shorter cell, or a rowspan, still takes the stretch ask.
            const bool block_matches =
                std::abs(cell.content_block - cell_block) <= 0.5f && cell.box->cache != nullptr &&
                cell.box->cache->pinned_final() != nullptr;
            const LayoutResult* result = nullptr;
            if (block_matches) {
                result = cell.box->cache->pinned_final();
            } else {
                ConstraintSpace space =
                    child_space(base_cell_space, cell_style, *cell.box, cell.used_inline,
                                cell_block, true, true, RequestKind::Final, Axis::Block);
                space.percentage.inline_size = {true, cell.used_inline};
                space.containing_block.inline_size = space.percentage.inline_size;
                result = cx.ask(*cell.box, space);
            }
            if (result != nullptr &&
                std::abs(result->geometry.border_box_inline - cell.used_inline) > 0.5f) {
                converged = false;
            }
            if (result != nullptr) {
                // LAYOUT.md section 4: the table's ink reaches wherever its cells' ink does. A
                // cell's `cb_offset` is relative to its row's padding box and the row's to the
                // group it sits in (the table itself when it has none), so the table's frame is
                // the sum of the two offsets.
                float origin_inline = padding.inline_start + spacing_inline;
                float origin_block = padding.block_start + row_offset[i];
                if (row.group != nullptr) {
                    origin_inline = 0.f;
                    for (const std::pair<Box*, std::pair<float, float>>& entry : group_bounds) {
                        if (entry.first == row.group) {
                            origin_block -= padding.block_start + entry.second.first;
                            break;
                        }
                    }
                }
                LogicalPoint cell_origin;
                cell_origin.inline_offset =
                    origin_inline + cell.box->placement.cb_offset.inline_offset;
                cell_origin.block_offset =
                    origin_block + cell.box->placement.cb_offset.block_offset;
                // The table's own first baseline is its first row's first cell (CSS 2.1 section
                // 17.5.3, which an inline-table and a baseline-aligned table item both read). The
                // cell hands over its own first baseline when it has one and the block-end edge of
                // its border box otherwise, exactly like a grid item (layout/baseline.h). Only the
                // table's own first row feeds it, and only while that row is in this fragment.
                if (i == 0 && std::isnan(first_row_baseline)) {
                    const float own = result->geometry.baselines.first;
                    first_row_baseline =
                        border.block_start + cell_origin.block_offset +
                        (std::isfinite(own) ? own : synthesized_item_baseline(*result));
                }
                rect_include(out.overflow.ink,
                             child_ink_in_box(result->overflow, result->geometry.border_box_inline,
                                              result->geometry.border_box_block, style.writing_mode,
                                              cell_style.writing_mode, border, cell_origin,
                                              clips_overflow(cell_style.overflow_x,
                                                             cell_style.overflow_y)));
            }
            if (!block_matches) {
                cx.keep_latest_ask(*cell.box);
                cx.reset_ask_fuse(*cell.box);
            }
        }
    }
    // Rows now: they only carry the size the table solved, and their own scrollable overflow is the
    // union of the cells that were just placed inside them (a `rowspan` cell reaches into the next
    // row, which is what a browser reports for the row it starts in).
    for (std::size_t i = 0; i < model.rows.size(); ++i) {
        if (i < resume_row || i >= break_row) {
            continue;
        }
        Row& row = model.rows[i];
        if (row.box == nullptr) {
            continue;
        }
        // A row's `cb_offset` is relative to its parent: the row group it sits in, or the table
        // itself when it has no group. The same rule puts a cell at 0 inside its row - the row's own
        // offset is already carried by the row's placement.
        float row_top = padding.block_start + row_offset[i];
        float row_inline = padding.inline_start + spacing_inline;
        if (row.group != nullptr) {
            // The group's placement is written after the rows (its extent needs them), so take the
            // group's top from the offset that was solved above rather than from its placement.
            for (const std::pair<Box*, std::pair<float, float>>& entry : group_bounds) {
                if (entry.first == row.group) {
                    row_top -= padding.block_start + entry.second.first;
                    break;
                }
            }
            row_inline = 0.f;
            // Every part that meets the same grid line shares one rectangle in the collapsing
            // model, so the row group's own half borders come back off as well.
            if (model.collapse) {
                const Edges group_border = border_edges(mode, direction, style_of(*row.group));
                row_top -= group_border.block_start;
                row_inline -= group_border.inline_start;
            }
        }
        row.box->placement.cb_offset.inline_offset = row_inline;
        row.box->placement.cb_offset.block_offset = row_top;
        row.box->placement.sticky_offset = {};
        const LayoutStyle& row_style = style_of(*row.box);
        ConstraintSpace space = child_space(base_cell_space, row_style, *row.box, grid_target,
                                            row.block, true, true, RequestKind::Final, Axis::Block);
        const LayoutResult* row_result = cx.ask(*row.box, space);
        // The row's layout zeros the border box. Chrome still reports the height the
        // cells would have had as scrollHeight, which is what makes `tr.gone` 24 tall
        // in the scroll size and 0 in the border box.
        if (row.collapsed && row_result != nullptr && row.content_block > 0.f) {
            Rect& scrollable = const_cast<LayoutResult*>(row_result)->overflow.scrollable;
            scrollable.h = std::max(scrollable.h, row.content_block);
        }
        cx.keep_latest_ask(*row.box);
        cx.reset_ask_fuse(*row.box);
    }
    for (const std::pair<Box*, std::pair<float, float>>& entry : group_bounds) {
        Box* group = entry.first;
        const float top = entry.second.first;
        const float height = std::max(0.f, entry.second.second - top);
        group->placement.cb_offset.inline_offset = padding.inline_start + spacing_inline;
        group->placement.cb_offset.block_offset = padding.block_start + top;
        group->placement.sticky_offset = {};
        const LayoutStyle& group_style = style_of(*group);
        ConstraintSpace space = child_space(base_cell_space, group_style, *group, grid_target,
                                            height, true, true, RequestKind::Final, Axis::Block);
        (void)cx.ask(*group, space);
        cx.keep_latest_ask(*group);
        cx.reset_ask_fuse(*group);
    }
    // A row group with no rows at all still has a box: a browser reports it as wide as the grid and
    // as tall as none, at the row it would have started with (measured: `example-zdb`'s
    // `#grid-body` is 852x0 at the table's content origin before the first row exists, and 0x0
    // here). Groups that do have rows were placed above.
    for (const Model::GroupBox& entry : model.groups) {
        Box* group = entry.box;
        if (group == nullptr) {
            continue;
        }
        bool placed = false;
        for (const std::pair<Box*, std::pair<float, float>>& bounds : group_bounds) {
            if (bounds.first == group) {
                placed = true;
                break;
            }
        }
        if (placed) {
            continue;
        }
        float top = 0.f;
        if (entry.first_row < static_cast<int>(row_offset.size())) {
            top = row_offset[static_cast<std::size_t>(entry.first_row)];
        } else {
            // No row follows it: its top is the end of the grid, without the trailing spacing.
            top = std::max(0.f, grid_block - spacing_block * 2.f);
        }
        group->placement.cb_offset.inline_offset = padding.inline_start + spacing_inline;
        group->placement.cb_offset.block_offset = padding.block_start + top;
        group->placement.sticky_offset = {};
        const LayoutStyle& group_style = style_of(*group);
        ConstraintSpace space = child_space(base_cell_space, group_style, *group, grid_target,
                                            0.f, true, true, RequestKind::Final, Axis::Block);
        (void)cx.ask(*group, space);
        cx.keep_latest_ask(*group);
        cx.reset_ask_fuse(*group);
    }
    // `<colgroup>` and `<col>` have no content, but a browser reports the grid tracks they cover
    // (not the whole table content box). Collapsed tracks contribute 0脳0 on the border box.
    const float grid_interior_block =
        visible_rows > 0 ? std::max(0.f, grid_block - spacing_block * 2.f) : 0.f;
    for (const Model::ColumnBox& entry : model.column_boxes) {
        if (entry.box == nullptr) {
            continue;
        }
        bool entry_collapsed = entry.count > 0;
        for (int c = entry.first; c < entry.first + entry.count && c < model.columns; ++c) {
            if (!column_collapsed(model, c)) {
                entry_collapsed = false;
                break;
            }
        }
        if (entry.count <= 0) {
            entry_collapsed = style_of(*entry.box).visibility_collapse;
        }
        const float x = track_inline_start(model, entry.first, spacing_inline);
        const float width = span_width_visible(model, entry.first, entry.count, spacing_inline);
        // Ask at the track block size even when collapsed so CSSOM can keep client/scroll height;
        // visibility:collapse then zeros the border box (see in_flow / paint_map).
        const float height = grid_interior_block;
        Box* target = entry.box;
        const LayoutStyle& part_style = style_of(*target);
        ConstraintSpace space = child_space(base_cell_space, part_style, *target, width, height,
                                            true, true, RequestKind::Final, Axis::Block);
        if (target->parent != nullptr && part_of(*target->parent) == TablePart::ColumnGroup) {
            // Offset is relative to the column group, which itself starts at its first track.
            int group_first = entry.first;
            for (const Model::ColumnBox& group : model.column_boxes) {
                if (group.box == target->parent) {
                    group_first = group.first;
                    break;
                }
            }
            target->placement.cb_offset.inline_offset =
                x - track_inline_start(model, group_first, spacing_inline);
            target->placement.cb_offset.block_offset = 0.f;
        } else {
            target->placement.cb_offset.inline_offset = padding.inline_start + spacing_inline + x;
            target->placement.cb_offset.block_offset = padding.block_start + grid_y;
        }
        target->placement.sticky_offset = {};
        (void)entry_collapsed;
        (void)cx.ask(*target, space);
        cx.keep_latest_ask(*target);
        cx.reset_ask_fuse(*target);
    }

    out.converged = converged;
    // The fragment's own extent, cut out of the table's block axis between two rows.
    FragmentCut cut;
    if (frag.active) {
        const float placed_bottom =
            break_row == resume_row
                ? own_start
                : std::max(0.f, row_bottom(break_row - 1) - (resume_row == 0 ? grid_y : own_start));
        const float first_unplaced =
            break_row < model.rows.size() ? row_bottom(break_row) - frag_limit : 0.f;
        int next_child = -1;
        if (break_row < model.rows.size() && model.rows[break_row].box != nullptr) {
            for (std::size_t index = 0; index < box.children.size(); ++index) {
                if (box.children[index] == model.rows[break_row].box) {
                    next_child = static_cast<int>(index);
                    break;
                }
            }
        }
        const float box_size = !style.height_auto ? content_block + chrome_block : -1.f;
        cut = cut_fragment(own_start, frag.remaining_block, grid_y, chrome_block, placed_bottom,
                           box_size, rows_finished, next_child, 0, first_unplaced);
        if (own_start > 0.01f) {
            out.geometry.margin.block_start = 0;
        }
        if (!rows_finished) {
            out.geometry.margin.block_end = 0;
        }
        out.break_token = cut.token;
        out.space_shortage = cut.space_shortage;
    } else {
        cut.border_block = content_block + chrome_block;
        cut.content_block = content_block;
    }
    out.geometry.content_inline = std::max(0.f, content_inline);
    out.geometry.content_block = std::max(0.f, cut.content_block);
    out.geometry.border_box_inline = out.geometry.content_inline + chrome_inline;
    out.geometry.border_box_block =
        frag.active ? cut.border_block : out.geometry.content_block + chrome_block;
    out.geometry.baselines = {};
    if (std::isfinite(first_row_baseline)) {
        out.geometry.baselines.first = first_row_baseline;
        out.geometry.baselines.last = first_row_baseline;
    }
    out.overflow.scrollable = scrollable_extent(
        padding.inline_start, padding.inline_end, padding.block_start, padding.block_end,
        std::max(0.f, content_inline), std::max(0.f, content_block),
        std::max(grid_target, content_inline), std::max(grid_block, content_block), scrolls(style));
    t.grid_target = grid_target;
    t.content_inline = content_inline;
}

}  // namespace table_detail
}  // namespace layout
}  // namespace zui
