#include "layout/grid/detail.h"

namespace zui {
namespace layout {
namespace grid_detail {

LineIndex line_to_index(const GridLine& line, int explicit_count,
                        const std::vector<std::vector<std::string>>& names,
                        bool end_edge)
{
    if (line.is_auto) {
        return {};
    }
    if (!line.name.empty()) {
        for (std::size_t i = 0; i < names.size(); ++i) {
            for (const std::string& name : names[i]) {
                if (name == line.name) {
                    return {true, static_cast<int>(i)};
                }
            }
        }
        // A name that does not match a line but does name a grid area stands for that area's
        // edge line, which `grid-template-areas` expanded into `<name>-start` / `<name>-end`
        // (section 8.3). `grid-area: header` relies on this: as a shorthand it has to be
        // equivalent to the four longhands, which only see the bare name.
        const std::string edge = line.name + (end_edge ? "-end" : "-start");
        for (std::size_t i = 0; i < names.size(); ++i) {
            for (const std::string& name : names[i]) {
                if (name == edge) {
                    return {true, static_cast<int>(i)};
                }
            }
        }
        return {};
    }
    if (line.line == 0) {
        return {};
    }
    if (line.line > 0) {
        return {true, line.line - 1};
    }
    return {true, explicit_count + 1 + line.line};
}

AxisArea area_of(const GridLine& start, const GridLine& end, int explicit_count,
                 const std::vector<std::vector<std::string>>& names)
{
    AxisArea out;
    const LineIndex start_line = line_to_index(start, explicit_count, names, false);
    LineIndex end_line = line_to_index(end, explicit_count, names, true);
    const int start_index = start_line.found ? start_line.index : -1;
    int end_index = end_line.found ? end_line.index : -1;
    // `span <name>`: the end is the nth line with that name after the start. A span whose start is
    // auto-placed cannot be resolved here and stays a span of one (recorded in ROADMAP2).
    if (end.is_span && !end.name.empty() && start_line.found && start_index >= 0) {
        int seen = 0;
        end_index = -1;
        for (std::size_t i = static_cast<std::size_t>(start_index + 1); i < names.size(); ++i) {
            for (const std::string& name : names[i]) {
                if (name == end.name && ++seen == std::max(1, end.span)) {
                    end_index = static_cast<int>(i);
                    break;
                }
            }
            if (end_index >= 0) {
                break;
            }
        }
    }
    if (start_line.found && end_line.found && end_index > start_index) {
        out.index = start_index;
        out.span = end_index - start_index;
        out.is_auto = false;
    } else if (start_line.found) {
        out.index = start_index;
        out.span = std::max(1, end.span);
        out.is_auto = false;
    } else if (end_line.found) {
        out.span = std::max(1, start.span);
        out.index = end_index - out.span;
        out.is_auto = false;
    } else {
        out.index = 0;
        out.span = start.span > 0 ? start.span : 1;
        out.is_auto = true;
    }
    return out;
}

// Section 8.5, sparse unless dense is asked for, running along rows unless grid-auto-flow says
// columns. Items with both lines given occupy their areas first: an auto item that comes earlier
// in the document must not take a cell away from an explicitly placed one. Auto items are then
// placed in order-modified document order (section 6.1).
PlacementResult place_items(const std::vector<GridItem>& source, const LayoutStyle& style,
                            int template_columns, int template_rows)
{
    PlacementResult out;
    out.items = source;
    std::stable_sort(out.items.begin(), out.items.end(),
                     [](const GridItem& left, const GridItem& right) {
                         return style_of(*left.box).flex_order < style_of(*right.box).flex_order;
                     });
    const int explicit_columns = std::max<int>(1, template_columns);
    const int explicit_rows = std::max<int>(1, template_rows);
    const bool column_flow = style.grid_auto_flow_column;
    const bool dense = style.grid_auto_flow_dense;

    Occupancy occupied;
    for (const GridItem& item : out.items) {
        const bool major_auto = column_flow ? item.col_auto : item.row_auto;
        const bool cross_auto = column_flow ? item.row_auto : item.col_auto;
        if (major_auto || cross_auto) {
            continue;
        }
        occupied.mark(column_flow ? item.col : item.row, column_flow ? item.row : item.col,
                      column_flow ? item.col_span : item.row_span,
                      column_flow ? item.row_span : item.col_span);
    }

    // Items locked to a given row (a definite row position with an automatic column) fill that
    // row left to right: each row remembers the line after the last item this step placed in it,
    // so an item never lands in a gap an earlier one left behind (CSS Grid section 8.5 step 2).
    std::vector<int> row_next_column(static_cast<std::size_t>(explicit_rows), 0);
    for (GridItem& item : out.items) {
        if (item.row_auto || !item.col_auto) {
            continue;
        }
        if (item.row < 0 || item.col_span <= 0) {
            continue;
        }
        if (static_cast<std::size_t>(item.row) >= row_next_column.size()) {
            row_next_column.resize(static_cast<std::size_t>(item.row) + 1, 0);
        }
        int col = row_next_column[static_cast<std::size_t>(item.row)];
        while (!occupied.free(item.row, col, item.row_span, item.col_span)) {
            ++col;
        }
        item.col = col;
        occupied.mark(item.row, col, item.row_span, item.col_span);
        row_next_column[static_cast<std::size_t>(item.row)] = col + item.col_span;
    }

    // The remaining items fill the grid along the flow axis (section 8.5 step 4). The auto
    // placement cursor is a (row, column) pair; sparse packing keeps it between items, dense
    // packing restarts it from the start of the implicit grid for every item.
    int cursor_row = 0;
    int cursor_col = 0;
    for (GridItem& item : out.items) {
        if (!item.row_auto && !item.col_auto) {
            continue;
        }
        const int row_span = std::max(1, item.row_span);
        const int col_span = std::max(1, item.col_span);
        if (item.row_auto && !item.col_auto) {
            // A definite column with an automatic row: the cursor moves to that column; if the
            // column is before where the cursor already is, the cursor first steps down a row.
            int row = dense ? 0 : cursor_row;
            if (!dense && item.col < cursor_col) {
                ++row;
            }
            while (!occupied.free(row, item.col, row_span, col_span)) {
                ++row;
            }
            item.row = row;
            cursor_row = row;
            cursor_col = item.col;
            occupied.mark(item.row, item.col, row_span, col_span);
            continue;
        }
        if (!item.row_auto && item.col_auto) {
            // Already placed in step 2 (definite row, automatic column). Placing it again
            // treats its own cell as occupied and pushes it into the next implicit column.
            continue;
        }
        // Automatic in both axes: scan along a line, wrapping to the next line when the span no
        // longer fits (or a free position is not found).
        const int row_limit = std::max(explicit_rows, row_span);
        const int col_limit = std::max(explicit_columns, col_span);
        int row = dense ? 0 : cursor_row;
        int col = dense ? 0 : cursor_col;
        bool placed = false;
        if (column_flow) {
            for (int c = col; !placed; ++c) {
                const int first_row = c == col ? row : 0;
                for (int r = first_row; r + row_span <= row_limit; ++r) {
                    if (occupied.free(r, c, row_span, col_span)) {
                        row = r;
                        col = c;
                        placed = true;
                        break;
                    }
                }
            }
        } else {
            for (int r = row; !placed; ++r) {
                const int first_col = r == row ? col : 0;
                for (int c = first_col; c + col_span <= col_limit; ++c) {
                    if (occupied.free(r, c, row_span, col_span)) {
                        row = r;
                        col = c;
                        placed = true;
                        break;
                    }
                }
            }
        }
        item.row = row;
        item.col = col;
        if (column_flow) {
            cursor_row = row + row_span;
            cursor_col = col;
        } else {
            cursor_row = row;
            cursor_col = col + col_span;
        }
        occupied.mark(item.row, item.col, row_span, col_span);
    }

    out.column_count = explicit_columns;
    out.row_count = explicit_rows;
    for (const GridItem& item : out.items) {
        out.column_count = std::max(out.column_count, item.col + item.col_span);
        out.row_count = std::max(out.row_count, item.row + item.row_span);
    }
    return out;
}

}  // namespace grid_detail
}  // namespace layout
}  // namespace zui
