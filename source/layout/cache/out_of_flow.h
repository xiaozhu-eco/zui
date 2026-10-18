#ifndef ZUI_LAYOUT_CACHE_OUT_OF_FLOW_H
#define ZUI_LAYOUT_CACHE_OUT_OF_FLOW_H

#include "layout/cache/box_geometry.h"
#include "layout/cache/layout_key.h"

namespace zui {
namespace layout {

struct Box;
struct LayoutResult;
class LayoutContext;

// Asks out-of-flow descendants while the caller's used_requests frame is still on the stack.
class OutOfFlowAsk {
public:
    // `out` is the containing block's result: the walk also unions the placed children into its
    // scrollable overflow, which only the caller can reach (LAYOUT.md section 9, the out-of-flow
    // half of `scrollWidth` / `scrollHeight`).
    static void collect(Box& containing_block, const LayoutKey& key, const BoxGeometry& geometry,
                        LayoutContext& cx, LayoutResult& out);
};

}  // namespace layout
}  // namespace zui

#endif
