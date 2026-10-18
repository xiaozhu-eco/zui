#ifndef ZUI_LAYOUT_CACHE_FLEX_ALGO_H
#define ZUI_LAYOUT_CACHE_FLEX_ALGO_H

#include "layout/flex/flex.h"

namespace zui {
namespace layout {

struct FlexAlgo {
    static LayoutResult compute(Box& box, const LayoutKey& key, LayoutContext& cx) {
        return layout_flex(box, key, cx);
    }
};

}  // namespace layout
}  // namespace zui

#endif
