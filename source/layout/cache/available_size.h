#ifndef ZUI_LAYOUT_CACHE_AVAILABLE_SIZE_H
#define ZUI_LAYOUT_CACHE_AVAILABLE_SIZE_H

#include "layout/cache/axis_size.h"

namespace zui {
namespace layout {

struct AvailableSize {
    AxisSize inline_size;
    AxisSize block_size;
};

}  // namespace layout
}  // namespace zui

#endif
