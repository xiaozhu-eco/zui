#ifndef ZUI_LAYOUT_CACHE_CONTAINING_BLOCK_H
#define ZUI_LAYOUT_CACHE_CONTAINING_BLOCK_H

#include "layout/cache/axis_size.h"
#include "layout/cache/logical_point.h"

namespace zui {
namespace layout {

struct ContainingBlock {
    AxisSize inline_size;
    AxisSize block_size;
    LogicalPoint static_position;
    bool fixed_is_viewport = false;
};

}  // namespace layout
}  // namespace zui

#endif
