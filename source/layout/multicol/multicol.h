#ifndef ZUI_LAYOUT_MULTICOL_MULTICOL_H
#define ZUI_LAYOUT_MULTICOL_MULTICOL_H

#include "layout/cache/layout_result.h"

namespace zui {
namespace layout {

struct Box;
struct LayoutKey;
class LayoutContext;

/// CSS Multi-column Layout. The container solves its used column count and width (section 3.4),
/// asks each in-flow child once for its Final geometry in a column of that width, then places the
/// children column by column from the sizes it got back. Nothing about the columns is written onto
/// a child: the column geometry stays in this file's locals, and the container's own
/// `Overflow.scrollable` carries the overflow columns.
///
/// Subset for this block:
///   * `column-count` / `column-width` / `column-gap` / `column-fill`;
///   * a child fragments across columns through the break token. A trailing margin that crosses the
///     column edge is truncated there, so it does not push a border box that fits into the next
///     column. A child that reports no fragment for the leftover still moves whole;
///   * `column-rule` (solid) is painted from computed style;
///   * `column-span: all` on a direct in-flow child breaks the column run (spanner).
LayoutResult layout_multicol(Box& box, const LayoutKey& key, LayoutContext& cx);

}  // namespace layout
}  // namespace zui

#endif
