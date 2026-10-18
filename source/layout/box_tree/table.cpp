#include "layout/box_tree/tree.h"

#include "layout/box_tree/detail.h"
#include "layout/intern_style.h"
#include "trace/trace.h"

#include <algorithm>
#include <vector>

namespace zui {
namespace layout {

using namespace box_tree_detail;

void BoxTree::fixup_table(Box& box) {
    zui::trace::Zone zone("fixup_table", zui::trace::Category::LayoutInner);
    if (box.style_ptr == nullptr) {
        return;
    }
    const LayoutStyle& style = box.style();
    if (style.inner == DisplayInner::Flex || style.inner == DisplayInner::Grid) {
        // A table part inside a flex or grid container is a blockified item, not a table part.
        return;
    }
    const TablePart part = style.table_part;
    const bool is_table = table_outer_part(part);
    const bool is_group = table_group_part(part);
    const bool is_row = part == TablePart::Row;
    const bool is_flow_host =
        !is_table && !is_group && !is_row && style.inner == DisplayInner::Flow;
    if (!is_table && !is_group && !is_row && !is_flow_host) {
        return;
    }
    auto needs_wrap = [&](const Box& child) {
        const TablePart child_part = table_part_of(child);
        if (is_row) {
            return child_part != TablePart::Cell;
        }
        if (part == TablePart::ColumnGroup) {
            // A column group holds columns; everything else is wrapped so a stray cell or row can
            // still find its table.
            return child_part != TablePart::Column && child_part != TablePart::ColumnGroup;
        }
        if (is_flow_host) {
            // Only the parts that need a table around them: everything else is ordinary flow. A
            // cell, row, row group, caption or column whose parent is not a table part gets one.
            return child_part == TablePart::Cell || child_part == TablePart::Row ||
                   table_group_part(child_part) || child_part == TablePart::Caption ||
                   child_part == TablePart::Column;
        }
        return !proper_table_child(is_table ? TablePart::Table : TablePart::RowGroup, child_part);
    };
    bool any = false;
    for (Box* child : box.children) {
        if (child != nullptr && needs_wrap(*child)) {
            any = true;
            break;
        }
    }
    if (!any) {
        return;
    }

    std::vector<Box*> rebuilt;
    std::vector<Box*> run;
    auto wrap_run = [&]() {
        if (run.empty()) {
            return;
        }
        // A flow host gets a table around the misplaced parts, a table or row group gets a row, a
        // row gets a cell - and the anonymous box is fixed up in turn, so a table between a row
        // and its cells is generated too.
        const TablePart kind =
            is_flow_host ? TablePart::Table : (is_row ? TablePart::Cell : TablePart::Row);
        Box* anon = make();
        anon->parent = &box;
        anon->role = BoxRole::Anonymous;
        anon->style_ptr = intern_style_as(
            box.node, BoxRole::Anonymous, DisplayOuter::Block,
            kind == TablePart::Table ? DisplayInner::Table : DisplayInner::Flow, kind, viewport_);
        for (Box* child : run) {
            child->parent = anon;
            anon->children.push_back(child);
            if (child->subtree_has_fixed) {
                anon->subtree_has_fixed = true;
            }
        }
        run.clear();
        fixup_table(*anon);
        rebuilt.push_back(anon);
    };
    for (Box* child : box.children) {
        if (child == nullptr) {
            continue;
        }
        if (!needs_wrap(*child)) {
            wrap_run();
            rebuilt.push_back(child);
            continue;
        }
        if (child->role == BoxRole::Text && blank_text(child->text)) {
            // Whitespace between table parts is not content (CSS 2.1 section 17.2.1).
            wrap_run();
            // The box is leaving the tree, so it has to leave `by_node_` too, the same way
            // `wrap_inlines` drops a collapsible run: a dropped box that keeps its mapping answers
            // `principal()` with a box no walk can reach, and the next sync of that node reparents
            // a box whose parent the tree has already released.
            forget_subtree_boxes(*child);
            continue;
        }
        run.push_back(child);
    }
    wrap_run();
    box.children.swap(rebuilt);
}

void BoxTree::resolve_collapsed_borders(Box& table) {
    if (table.style_ptr == nullptr || !table.style().border_collapse) {
        return;
    }
    struct RowInfo {
        Box* row = nullptr;
        Box* group = nullptr;
        std::vector<std::pair<Box*, int>> cells;
    };
    std::vector<RowInfo> rows;
    std::vector<std::vector<bool>> occupied;
    int columns = 0;
    auto add_row = [&](Box& row_box, Box* group) {
        RowInfo info;
        info.row = &row_box;
        info.group = group;
        const int index = static_cast<int>(rows.size());
        occupied.resize(static_cast<std::size_t>(index) + 1);
        int col = 0;
        for (Box* cell : row_box.children) {
            if (cell == nullptr || cell->style_ptr == nullptr ||
                cell->style().table_part != TablePart::Cell) {
                continue;
            }
            const LayoutStyle& cell_style = cell->style();
            while (col < static_cast<int>(occupied[static_cast<std::size_t>(index)].size()) &&
                   occupied[static_cast<std::size_t>(index)][static_cast<std::size_t>(col)]) {
                ++col;
            }
            columns = std::max(columns, col + cell_style.table_colspan);
            for (int r = index; r < index + cell_style.table_rowspan; ++r) {
                if (static_cast<int>(occupied.size()) <= r) {
                    occupied.resize(static_cast<std::size_t>(r) + 1);
                }
                std::vector<bool>& line = occupied[static_cast<std::size_t>(r)];
                line.resize(static_cast<std::size_t>(columns), false);
                for (int c = col; c < col + cell_style.table_colspan; ++c) {
                    line[static_cast<std::size_t>(c)] = true;
                }
            }
            info.cells.emplace_back(cell, col);
            col += cell_style.table_colspan;
        }
        rows.push_back(std::move(info));
    };
    for (Box* child : table.children) {
        if (child == nullptr || child->style_ptr == nullptr) {
            continue;
        }
        const TablePart part = child->style().table_part;
        if (table_group_part(part)) {
            for (Box* row_box : child->children) {
                if (row_box != nullptr && row_box->style_ptr != nullptr &&
                    row_box->style().table_part == TablePart::Row) {
                    add_row(*row_box, child);
                }
            }
            continue;
        }
        if (part == TablePart::Row) {
            add_row(*child, nullptr);
        }
    }
    if (rows.empty()) {
        return;
    }
    const std::size_t row_count = rows.size();
    // CSS 2.1 §17.6.2.1: each grid line keeps the winning (width, style). Later raises come from
    // closer sources (table → column → group → row → cell), so an equal score lets the later
    // source win — approximating the cell > row > … color/source rule without storing colors.
    struct BorderEdge {
        float width = 0.f;
        BorderLineStyle style = BorderLineStyle::None;
        bool set = false;
    };
    auto style_rank = [](BorderLineStyle style) -> int {
        // Higher wins among equal widths (double … inset).
        switch (style) {
            case BorderLineStyle::Double:
                return 8;
            case BorderLineStyle::Solid:
                return 7;
            case BorderLineStyle::Dashed:
                return 6;
            case BorderLineStyle::Dotted:
                return 5;
            case BorderLineStyle::Ridge:
                return 4;
            case BorderLineStyle::Outset:
                return 3;
            case BorderLineStyle::Groove:
                return 2;
            case BorderLineStyle::Inset:
                return 1;
            case BorderLineStyle::None:
            case BorderLineStyle::Hidden:
                return 0;
        }
        return 0;
    };
    auto raise = [&](BorderEdge& line, float width, BorderLineStyle style) {
        BorderEdge cand{width, style, true};
        if (style == BorderLineStyle::Hidden) {
            // Rule 1: hidden suppresses the edge.
            line = {0.f, BorderLineStyle::Hidden, true};
            return;
        }
        if (line.set && line.style == BorderLineStyle::Hidden) {
            return;
        }
        if (style == BorderLineStyle::None) {
            // Rule 2: none loses to any other style; only seeds an empty line.
            if (!line.set) {
                line = cand;
            }
            return;
        }
        if (!line.set || line.style == BorderLineStyle::None) {
            line = cand;
            return;
        }
        // Rule 3: wider wins; equal width → style rank; equal score → later source wins.
        if (cand.width > line.width ||
            (cand.width == line.width && style_rank(cand.style) >= style_rank(line.style))) {
            line = cand;
        }
    };
    // CSS 2.1 §17.6.2.1 resolves the conflict per border line *segment*, the piece of a line
    // between two grid points, so a thick border on one row's edge does not widen the same line in
    // another row. Chrome's own numbers (probe `build/collapseprobe.py`, table `border: 2px`,
    // cells `1px`, one cell `3px`): the first row's cells keep 1px halves of the table's 2px line
    // while the `.bold` cell's row gets 1.5px halves. `vline[row][col]` is the segment of the
    // vertical line at column `col` inside row `row`; `hline[line][col]` the horizontal line at
    // row line `line` over column `col`.
    std::vector<std::vector<BorderEdge>> vline(
        row_count, std::vector<BorderEdge>(static_cast<std::size_t>(columns) + 1));
    std::vector<std::vector<BorderEdge>> hline(
        row_count + 1, std::vector<BorderEdge>(static_cast<std::size_t>(columns)));
    auto raise_v = [&](int row, int col, float width, BorderLineStyle style) {
        if (row >= 0 && row < static_cast<int>(row_count) && col >= 0 && col <= columns) {
            raise(vline[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)], width,
                  style);
        }
    };
    auto raise_h = [&](int line, int col, float width, BorderLineStyle style) {
        if (line >= 0 && line <= static_cast<int>(row_count) && col >= 0 && col < columns) {
            raise(hline[static_cast<std::size_t>(line)][static_cast<std::size_t>(col)], width,
                  style);
        }
    };
    // The winning edge over a run of segments: the same rule, folded. Equal widths keep the first
    // (the per-segment pass already ordered candidates by source precedence).
    auto fold_v = [](const std::vector<BorderEdge>& column, int from, int to) {
        BorderEdge out;
        for (int r = from; r < to; ++r) {
            const BorderEdge& edge = column[static_cast<std::size_t>(r)];
            if (edge.set && (!out.set || edge.width > out.width)) {
                out = edge;
            }
        }
        return out;
    };
    auto fold_h = [](const std::vector<std::vector<BorderEdge>>& lines, int line, int from,
                     int to) {
        const std::vector<BorderEdge>& row = lines[static_cast<std::size_t>(line)];
        BorderEdge out;
        for (int c = from; c < to; ++c) {
            const BorderEdge& edge = row[static_cast<std::size_t>(c)];
            if (edge.set && (!out.set || edge.width > out.width)) {
                out = edge;
            }
        }
        return out;
    };
    const LayoutStyle& table_style = table.style();
    for (std::size_t r = 0; r < row_count; ++r) {
        raise_v(static_cast<int>(r), 0, table_style.border_left, table_style.border_left_style);
        raise_v(static_cast<int>(r), columns, table_style.border_right,
                table_style.border_right_style);
    }
    for (std::size_t c = 0; c < static_cast<std::size_t>(columns); ++c) {
        raise_h(0, static_cast<int>(c), table_style.border_top, table_style.border_top_style);
        raise_h(static_cast<int>(row_count), static_cast<int>(c), table_style.border_bottom,
                table_style.border_bottom_style);
    }
    for (Box* child : table.children) {
        if (child == nullptr || child->style_ptr == nullptr) {
            continue;
        }
        const TablePart part = child->style().table_part;
        if (part != TablePart::Column && part != TablePart::ColumnGroup) {
            continue;
        }
        const LayoutStyle& column_style = child->style();
        if (part == TablePart::Column) {
            for (std::size_t r = 0; r < row_count; ++r) {
                raise_v(static_cast<int>(r), 0, column_style.border_left,
                        column_style.border_left_style);
                raise_v(static_cast<int>(r), columns, column_style.border_right,
                        column_style.border_right_style);
            }
        }
    }
    for (std::size_t i = 0; i < row_count; ++i) {
        RowInfo& info = rows[i];
        if (info.group != nullptr && info.group->style_ptr != nullptr) {
            for (int c = 0; c < columns; ++c) {
                raise_h(static_cast<int>(i), c, info.group->style().border_top,
                        info.group->style().border_top_style);
                raise_h(static_cast<int>(i) + 1, c, info.group->style().border_bottom,
                        info.group->style().border_bottom_style);
            }
        }
        if (info.row->style_ptr != nullptr) {
            for (int c = 0; c < columns; ++c) {
                raise_h(static_cast<int>(i), c, info.row->style().border_top,
                        info.row->style().border_top_style);
                raise_h(static_cast<int>(i) + 1, c, info.row->style().border_bottom,
                        info.row->style().border_bottom_style);
            }
        }
        for (const std::pair<Box*, int>& entry : info.cells) {
            Box* cell = entry.first;
            if (cell == nullptr || cell->style_ptr == nullptr) {
                continue;
            }
            const LayoutStyle& cell_style = cell->style();
            const int col = entry.second;
            const int right = std::min(col + cell_style.table_colspan, columns);
            const int bottom = std::min(static_cast<int>(i) + cell_style.table_rowspan,
                                        static_cast<int>(row_count));
            for (int r = static_cast<int>(i); r < bottom; ++r) {
                raise_v(r, col, cell_style.border_left, cell_style.border_left_style);
                raise_v(r, right, cell_style.border_right, cell_style.border_right_style);
            }
            for (int c = col; c < right; ++c) {
                raise_h(static_cast<int>(i), c, cell_style.border_top, cell_style.border_top_style);
                raise_h(bottom, c, cell_style.border_bottom, cell_style.border_bottom_style);
            }
        }
    }
    // Halve every line onto the boxes that meet there: a cell (and a row) has half of each of its
    // two lines, the table has the outer half, and the spacer between two cells disappears.
    auto set_borders = [](Box& box, const BorderEdge& left, const BorderEdge& right,
                          const BorderEdge& top, const BorderEdge& bottom) {
        if (box.style_ptr == nullptr) {
            return;
        }
        LayoutStyle variant = box.style();
        variant.border_left = left.width * 0.5f;
        variant.border_right = right.width * 0.5f;
        variant.border_top = top.width * 0.5f;
        variant.border_bottom = bottom.width * 0.5f;
        variant.border_left_style = left.style;
        variant.border_right_style = right.style;
        variant.border_top_style = top.style;
        variant.border_bottom_style = bottom.style;
        box.style_ptr = StyleTable::process().intern(std::move(variant));
    };
    // A box with one border per side takes the widest segment it touches; the table's outer edges
    // are the widest over all of theirs.
    auto vedge = [&](int col, std::size_t from, std::size_t to) {
        BorderEdge out;
        for (std::size_t r = from; r < to; ++r) {
            const BorderEdge& edge = vline[r][static_cast<std::size_t>(col)];
            if (edge.set && (!out.set || edge.width > out.width)) {
                out = edge;
            }
        }
        return out;
    };
    set_borders(table, vedge(0, 0, row_count), vedge(columns, 0, row_count),
                fold_h(hline, 0, 0, columns),
                fold_h(hline, static_cast<int>(row_count), 0, columns));
    for (std::size_t i = 0; i < row_count; ++i) {
        RowInfo& info = rows[i];
        const int row_index = static_cast<int>(i);
        if (info.row != nullptr) {
            set_borders(*info.row, vline[i][0], vline[i][static_cast<std::size_t>(columns)],
                        fold_h(hline, row_index, 0, columns),
                        fold_h(hline, row_index + 1, 0, columns));
        }
        for (const std::pair<Box*, int>& entry : info.cells) {
            Box* cell = entry.first;
            if (cell == nullptr || cell->style_ptr == nullptr) {
                continue;
            }
            const int col = entry.second;
            const int right = std::min(col + cell->style().table_colspan, columns);
            const int bottom = std::min(static_cast<int>(i) + cell->style().table_rowspan,
                                        static_cast<int>(row_count));
            set_borders(*cell, vedge(col, i, static_cast<std::size_t>(bottom)),
                        vedge(right, i, static_cast<std::size_t>(bottom)),
                        fold_h(hline, row_index, col, right), fold_h(hline, bottom, col, right));
        }
    }
    // A row group gets its own widest segments over the rows it spans (its borders are raised for
    // every one of those rows).
    for (std::size_t i = 0; i < row_count; ++i) {
        RowInfo& info = rows[i];
        if (info.group == nullptr || info.group->style_ptr == nullptr) {
            continue;
        }
        std::size_t last = i;
        while (last + 1 < row_count && rows[last + 1].group == info.group) {
            ++last;
        }
        set_borders(*info.group, vedge(0, i, last + 1), vedge(columns, i, last + 1),
                    fold_h(hline, static_cast<int>(i), 0, columns),
                    fold_h(hline, static_cast<int>(last) + 1, 0, columns));
        i = last;
    }
}

}  // namespace layout
}  // namespace zui
