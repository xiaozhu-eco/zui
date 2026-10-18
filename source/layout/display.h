#ifndef ZUI_LAYOUT_DISPLAY_H
#define ZUI_LAYOUT_DISPLAY_H

namespace zui {
namespace layout {

enum class DisplayOuter { Block, Inline, None };
enum class DisplayInner { Flow, Flex, Grid, Table, Multicol };

/// The table parts of CSS 2.1 section 17.1. The box tree turns them into the anonymous structure
/// the table layout needs; only the table wrapper itself carries `DisplayInner::Table`.
enum class TablePart {
    None,
    Table,
    InlineTable,
    RowGroup,
    HeaderGroup,
    FooterGroup,
    Row,
    Cell,
    Column,
    ColumnGroup,
    Caption,
};

}  // namespace layout
}  // namespace zui

#endif
