#ifndef ZUI_LAYOUT_CACHE_PERCENTAGE_BASIS_H
#define ZUI_LAYOUT_CACHE_PERCENTAGE_BASIS_H

#include "layout/cache/axis_size.h"

namespace zui {
namespace layout {

struct PercentageBasis {
    AxisSize inline_size;
    AxisSize block_size;
};

}  // namespace layout
}  // namespace zui

#endif
