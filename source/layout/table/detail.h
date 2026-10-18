#ifndef ZUI_SOURCE_LAYOUT_TABLE_DETAIL_H
#define ZUI_SOURCE_LAYOUT_TABLE_DETAIL_H

// Internals of table layout, shared by `model.cpp` (read the box tree into the table model),
// `geometry.cpp` (used edges and column geometry), and `table.cpp` (`layout_table`).

#include "layout/box.h"
#include "layout/cache/constraint_space.h"
#include "layout/cache/layout_context.h"
#include "layout/cache/layout_key.h"
#include "layout/cache/request.h"
#include "layout/layout_style.h"

#include <vector>

namespace zui {
namespace layout {
namespace table_detail {

const LayoutStyle& style_of(const Box& box);

float length_or_percent(float value, bool is_percent, float basis, bool basis_known);
float content_from_specified(float specified, bool border_box, float chrome);

Edges logical_margin(WritingMode mode, Direction direction, const LayoutStyle& style,
                     const AxisSize& basis);
Edges padding_edges(WritingMode mode, Direction direction, const LayoutStyle& style,
                    const AxisSize& basis);
Edges border_edges(WritingMode mode, Direction direction, const LayoutStyle& style);

bool out_of_flow(const LayoutStyle& style);
bool scrolls(const LayoutStyle& style);

TablePart part_of(const Box& box);
bool group_part(TablePart part);

struct Cell {
    Box* box = nullptr;
    int col = 0;
    int colspan = 1;
    int row = 0;
    int rowspan = 1;
    float min_inline = 0;
    float max_inline = 0;
    float used_inline = 0;
    /// The block size the rows it spans add up to, for a cell with `rowspan > 1`.
    float used_block = 0;
    // The height its own content needs, before the row stretches it.
    float content_block = 0;
    bool collapsed = false;
};

struct Row {
    Box* box = nullptr;
    Box* group = nullptr;
    std::vector<Cell> cells;
    float block = 0;
    /// The height the cells would have had. A collapsed row's border box is 0, and
    /// `scrollHeight` keeps this (Chrome on `example-table`'s `tr.gone` is 24).
    float content_block = 0;
    bool collapsed = false;
};

struct Model {
    /// A `<col>` or `<colgroup>` box and the columns it covers: the browser reports the grid's
    /// geometry for them even though they have no content of their own.
    struct ColumnBox {
        ColumnBox(Box* box_in, int first_in, int count_in)
            : box(box_in), first(first_in), count(count_in) {}
        Box* box = nullptr;
        int first = 0;
        int count = 0;
    };
    /// A row group box and the index of the first row of `rows` that follows it: an empty group
    /// still gets the grid's geometry in a browser (measured on `example-zdb`'s `#grid-body`
    /// before any row exists), so its position is the row it would have started at.
    struct GroupBox {
        Box* box = nullptr;
        int first_row = 0;
    };
    std::vector<Row> rows;
    std::vector<GroupBox> groups;
    std::vector<ColumnBox> column_boxes;
    std::vector<Box*> captions_top;
    std::vector<Box*> captions_bottom;
    int columns = 0;
    std::vector<float> col_min;
    std::vector<float> col_max;
    std::vector<float> col_used;
    /// The `width` a column's cells or `<col>` declare. A percentage outranks a length when a
    /// column carries both (measured: `build/table_dist.html` t26/t27, Chrome takes the
    /// percentage). `col_length` is -1 when no length is declared.
    std::vector<float> col_percent;
    std::vector<float> col_length;
    std::vector<bool> col_specified;
    /// Columns with `visibility: collapse` on `<col>` / `<colgroup>` (CSS 2.1 section 17.5.5).
    std::vector<bool> col_collapsed;
    float spacing_inline = 0;
    float spacing_block = 0;
    bool collapse = false;
};

/// Reads the table's structure from the boxes the box tree already fixed up: rows may sit in a row
/// group or directly in the table, and every cell knows its column and its span.
Model build_model(const Box& table);

float span_sum(const std::vector<float>& values, int col, int span);
bool column_collapsed(const Model& model, int col);
/// Inline start of column `col` inside the grid (after the leading border-spacing edge).
float track_inline_start(const Model& model, int col, float spacing_inline);
/// Used inline size of columns `[col, col+span)`, skipping collapsed tracks and their spacing.
float span_width_visible(const Model& model, int col, int span, float spacing_inline);
/// Spreads what a spanning cell needs beyond the columns it covers (CSS 2.1 section 17.5.2.2
/// leaves the share to the implementation: proportional to what the columns already are).
void raise_span(std::vector<float>& values, int col, int span, float target,
                const std::vector<bool>* collapsed);

ConstraintSpace child_space(const ConstraintSpace& parent, const LayoutStyle& style, const Box& box,
                            float inline_size, float block_size, bool block_definite,
                            bool stretch_block, RequestKind kind, Axis axis);


struct TablePlace {
    Box* box = nullptr;
    const LayoutKey* key = nullptr;
    LayoutContext* cx = nullptr;
    LayoutResult* out = nullptr;
    const LayoutStyle* style = nullptr;
    WritingMode mode = WritingMode::HorizontalTb;
    Direction direction = Direction::Ltr;
    Edges padding{};
    Edges border{};
    float chrome_inline = 0;
    float chrome_block = 0;
    float self_margin_block = 0;
    bool height_auto = false;
    float height_used = 0;
    bool flex_block = false;
    bool grid_stretch = false;
    Model* model = nullptr;
    ConstraintSpace base_cell_space{};
    float spacing_inline = 0;
    float spacing_block = 0;
    float edge_spacing = 0;
    float grid_target = 0;
    float content_inline = 0;
};

void layout_table_place(TablePlace& t);

}  // namespace table_detail
}  // namespace layout
}  // namespace zui

#endif
