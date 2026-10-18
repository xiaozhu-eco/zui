#ifndef ZUI_LAYOUT_CACHE_MULTICOL_ALGO_H
#define ZUI_LAYOUT_CACHE_MULTICOL_ALGO_H

#include "layout/multicol/multicol.h"

namespace zui {
namespace layout {

struct MulticolAlgo {
    static LayoutResult compute(Box& box, const LayoutKey& key, LayoutContext& cx) {
        return layout_multicol(box, key, cx);
    }
};

}  // namespace layout
}  // namespace zui

#endif
