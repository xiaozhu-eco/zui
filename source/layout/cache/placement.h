#ifndef ZUI_LAYOUT_CACHE_PLACEMENT_H
#define ZUI_LAYOUT_CACHE_PLACEMENT_H

#include "layout/cache/logical_point.h"

#include <vector>

namespace zui {
namespace layout {

/// One fragmentainer's worth of a box that broke across fragmentainers (LAYOUT.md section 12).
/// The parent writes these: the child's own cached result keeps describing the box, while the
/// placement says where each fragment of it landed. `fragments` is empty for a box that fits in
/// one piece, and otherwise holds *every* fragment in fragment order, including the first.
struct FragmentPlacement {
    LogicalPoint cb_offset;  // border-box start, in the containing block's padding-box frame
    float block_offset = 0;  // where this fragment starts inside the box's own block axis
    float block_size = 0;    // the fragment's border-box block size (the slice)
};

struct Placement {
    LogicalPoint cb_offset;
    LogicalPoint sticky_offset;
    std::vector<FragmentPlacement> fragments;
};

}  // namespace layout
}  // namespace zui

#endif
