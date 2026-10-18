#ifndef ZUI_LAYOUT_FLOW_FLOW_H
#define ZUI_LAYOUT_FLOW_FLOW_H

#include "layout/cache/layout_result.h"

namespace zui {
namespace layout {

struct Box;
struct LayoutKey;
class LayoutContext;

// Block and inline formatting. Flex and grid have their own algorithms (`layout/flex`,
// `layout/grid`); this is the normal-flow one.
LayoutResult layout_normal(Box& box, const LayoutKey& key, LayoutContext& cx);

// Dispatcher. Text, replaced, and `br` boxes never enter `layout_normal`: under /Od /RTC1 that
// function's frame is poisoned on entry, including storage the terminal path does not use.
LayoutResult layout_flow(Box& box, const LayoutKey& key, LayoutContext& cx);

bool is_terminal_box(const Box& box);
LayoutResult layout_terminal_result(Box& box, const LayoutKey& key, LayoutContext& cx);
bool plain_block_eligible(const Box& box, const LayoutKey& key);
LayoutResult layout_plain_block(Box& box, const LayoutKey& key, LayoutContext& cx);

}  // namespace layout
}  // namespace zui

#endif
