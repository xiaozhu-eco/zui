#ifndef ZUI_LAYOUT_TABLE_TABLE_H
#define ZUI_LAYOUT_TABLE_TABLE_H

#include "layout/cache/layout_result.h"

namespace zui {
namespace layout {

struct Box;
struct LayoutKey;
class LayoutContext;

// CSS 2.1 section 17. Column widths and row heights stay in this file: the table's children are
// the row groups and rows (the anonymous ones the box tree generated included), and the cells are
// asked twice - MinContent / MaxContent for their column contributions, then Final at the column
// width the sizing solved. Place does not ask.
LayoutResult layout_table(Box& box, const LayoutKey& key, LayoutContext& cx);

}  // namespace layout
}  // namespace zui

#endif
