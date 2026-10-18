#ifndef ZUI_LAYOUT_CACHE_TABLE_ALGO_H
#define ZUI_LAYOUT_CACHE_TABLE_ALGO_H

#include "layout/table/table.h"

namespace zui {
namespace layout {

struct TableAlgo {
    static LayoutResult compute(Box& box, const LayoutKey& key, LayoutContext& cx) {
        return layout_table(box, key, cx);
    }
};

}  // namespace layout
}  // namespace zui

#endif
