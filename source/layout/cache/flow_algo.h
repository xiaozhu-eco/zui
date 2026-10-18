#ifndef ZUI_LAYOUT_CACHE_FLOW_ALGO_H
#define ZUI_LAYOUT_CACHE_FLOW_ALGO_H

#include "layout/flow/flow.h"

namespace zui {
namespace layout {

struct FlowAlgo {
    static LayoutResult compute(Box& box, const LayoutKey& key, LayoutContext& cx) {
        return layout_flow(box, key, cx);
    }
};

}  // namespace layout
}  // namespace zui

#endif
