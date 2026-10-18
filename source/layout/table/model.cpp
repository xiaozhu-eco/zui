#include "layout/table/detail.h"

// Table model: read the fixed-up box tree into rows, cells, and column boxes, including
// rowspan/colspan occupancy and `visibility: collapse` on columns.

namespace zui {
namespace layout {
namespace table_detail {

Model build_model(const Box& table) {
    Model model;
    const LayoutStyle& style = style_of(table);
    model.collapse = style.border_collapse;
    if (!model.collapse) {
        model.spacing_inline = style.border_spacing_inline;
        model.spacing_block = style.border_spacing_block;
    }
    std::vector<std::vector<bool>> occupied;
    auto line_at = [&](int row) -> std::vector<bool>& {
        if (static_cast<int>(occupied.size()) <= row) {
            occupied.resize(static_cast<std::size_t>(row) + 1);
        }
        return occupied[static_cast<std::size_t>(row)];
    };
    auto grow = [&](int columns) {
        if (columns <= model.columns) {
            return;
        }
        for (std::vector<bool>& line : occupied) {
            line.resize(static_cast<std::size_t>(columns), false);
        }
        model.columns = columns;
    };
    auto add_column = [&](Box& col_box, bool group_collapsed) {
        const LayoutStyle& col_style = style_of(col_box);
        // `span` makes one `<col>` cover several columns; a width covers every column it spans.
        const int span = std::max(1, col_style.table_span);
        const bool collapsed = group_collapsed || col_style.visibility_collapse;
        for (int i = 0; i < span; ++i) {
            const std::size_t index = static_cast<std::size_t>(model.columns);
            grow(model.columns + 1);
            model.col_min.resize(static_cast<std::size_t>(model.columns), 0.f);
            model.col_max.resize(static_cast<std::size_t>(model.columns), 0.f);
            model.col_used.resize(static_cast<std::size_t>(model.columns), 0.f);
            model.col_percent.resize(static_cast<std::size_t>(model.columns), 0.f);
            model.col_length.resize(static_cast<std::size_t>(model.columns), -1.f);
            model.col_specified.resize(static_cast<std::size_t>(model.columns), false);
            model.col_collapsed.resize(static_cast<std::size_t>(model.columns), false);
            model.col_collapsed[index] = collapsed;
            if (!collapsed) {
                if (col_style.width_percent) {
                    model.col_percent[index] = col_style.width_px;
                } else if (!col_style.width_auto) {
                    model.col_length[index] = col_style.width_px;
                    model.col_min[index] = col_style.width_px;
                    model.col_max[index] = col_style.width_px;
                    model.col_specified[index] = true;
                }
            }
        }
        model.column_boxes.emplace_back(&col_box, model.columns - span, span);
    };
    auto add_row = [&](Box& row_box, Box* group) {
        Row row;
        row.box = &row_box;
        row.group = group;
        row.collapsed = style_of(row_box).visibility_collapse;
        const int row_index = static_cast<int>(model.rows.size());
        line_at(row_index);
        int col = 0;
        for (Box* child : row_box.children) {
            if (child == nullptr || part_of(*child) != TablePart::Cell) {
                continue;
            }
            const LayoutStyle& cell_style = style_of(*child);
            while (col < static_cast<int>(line_at(row_index).size()) &&
                   line_at(row_index)[static_cast<std::size_t>(col)]) {
                ++col;
            }
            Cell cell;
            cell.box = child;
            cell.col = col;
            cell.colspan = std::max(1, cell_style.table_colspan);
            cell.row = row_index;
            cell.rowspan = std::max(1, cell_style.table_rowspan);
            cell.collapsed = row.collapsed || cell_style.visibility_collapse;
            grow(col + cell.colspan);
            for (int r = row_index; r < row_index + cell.rowspan; ++r) {
                std::vector<bool>& line = line_at(r);
                line.resize(static_cast<std::size_t>(model.columns), false);
                for (int c = col; c < col + cell.colspan; ++c) {
                    line[static_cast<std::size_t>(c)] = true;
                }
            }
            row.cells.push_back(cell);
            col += cell.colspan;
        }
        model.rows.push_back(std::move(row));
    };

    for (Box* child : table.children) {
        if (child == nullptr) {
            continue;
        }
        const TablePart part = part_of(*child);
        if (part == TablePart::Caption) {
            // `caption-side` is the caption's own property, not the table's.
            (style_of(*child).caption_below ? model.captions_bottom : model.captions_top)
                .push_back(child);
            continue;
        }
        if (part == TablePart::Column) {
            add_column(*child, false);
            continue;
        }
        if (part == TablePart::ColumnGroup) {
            const int first = model.columns;
            const bool group_collapsed = style_of(*child).visibility_collapse;
            int span_total = 0;
            for (Box* col : child->children) {
                if (col != nullptr && col->node != nullptr && !col->node->tag.empty() &&
                    col->node->tag != "#text") {
                    span_total += std::max(1, style_of(*col).table_span);
                }
            }
            // The group comes before the columns it holds: asking it (which would lay its children
            // out at the group's width) must not follow the answers the columns got.
            model.column_boxes.emplace_back(child, first, span_total);
            for (Box* col : child->children) {
                if (col != nullptr && col->node != nullptr && !col->node->tag.empty() &&
                    col->node->tag != "#text") {
                    add_column(*col, group_collapsed);
                }
            }
            continue;
        }
        if (group_part(part)) {
            // An empty group still gets a box, so remember where in the row list it starts.
            model.groups.push_back(Model::GroupBox{child, static_cast<int>(model.rows.size())});
            for (Box* row_box : child->children) {
                if (row_box != nullptr && part_of(*row_box) == TablePart::Row) {
                    add_row(*row_box, child);
                }
            }
            continue;
        }
        if (part == TablePart::Row) {
            add_row(*child, nullptr);
        }
    }
    model.col_min.resize(static_cast<std::size_t>(model.columns), 0.f);
    model.col_max.resize(static_cast<std::size_t>(model.columns), 0.f);
    model.col_used.resize(static_cast<std::size_t>(model.columns), 0.f);
    model.col_percent.resize(static_cast<std::size_t>(model.columns), 0.f);
    model.col_length.resize(static_cast<std::size_t>(model.columns), -1.f);
    model.col_specified.resize(static_cast<std::size_t>(model.columns), false);
    model.col_collapsed.resize(static_cast<std::size_t>(model.columns), false);
    return model;
}

}  // namespace table_detail
}  // namespace layout
}  // namespace zui
