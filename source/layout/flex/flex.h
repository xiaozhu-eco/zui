#ifndef ZUI_LAYOUT_FLEX_FLEX_H
#define ZUI_LAYOUT_FLEX_FLEX_H

#include "layout/cache/layout_result.h"

namespace zui {
namespace layout {

struct Box;
struct LayoutKey;
class LayoutContext;

// Flex layout. Free space and line sizes stay in this function.
// Asks FlexBase, then either MinContent (automatic min / container MinContent) or
// FlexHypothetical (clamped main size; second ask slot), then Final (fuse reset when
// MinContent consumed the second slot). Container MinContent: nowrap sums item
// min-content contributions; wrap takes the max. Place does not ask.
LayoutResult layout_flex(Box& box, const LayoutKey& key, LayoutContext& cx);

}  // namespace layout
}  // namespace zui

#endif
