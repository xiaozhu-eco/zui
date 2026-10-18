#ifndef ZUI_LAYOUT_CACHE_BASELINE_SET_H
#define ZUI_LAYOUT_CACHE_BASELINE_SET_H

#include <limits>

namespace zui {
namespace layout {

struct BaselineSet {
    float first = std::numeric_limits<float>::quiet_NaN();
    float last = std::numeric_limits<float>::quiet_NaN();
};

}  // namespace layout
}  // namespace zui

#endif
