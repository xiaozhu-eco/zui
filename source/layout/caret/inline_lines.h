#ifndef ZUI_LAYOUT_CARET_INLINE_LINES_H
#define ZUI_LAYOUT_CARET_INLINE_LINES_H

namespace zui {
namespace layout {

struct Box;
struct InlineContent;

/// Pinned LayoutResult::lines for the IFC that paints @p box's text, or nullptr.
/// Resolves text boxes painted by a parent IFC the same way paint_map does, without
/// including paint_map_internal.h.
const InlineContent* inline_lines(const Box& box);

}  // namespace layout
}  // namespace zui

#endif