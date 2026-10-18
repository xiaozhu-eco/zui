#ifndef ZUI_LAYOUT_CACHE_FRAGMENTATION_H
#define ZUI_LAYOUT_CACHE_FRAGMENTATION_H

#include "layout/cache/break_token.h"

namespace zui {
namespace layout {

struct FragmentationContext {
    bool active = false;
    float remaining_block = 0;
    int fragment_index = 0;
    BreakToken incoming;
    /// True when this box starts at the fragmentainer's own start (nothing was placed above it in
    /// this fragment). It decides whether content that does not fit may overflow the fragmentainer
    /// or has to open the next fragment: a fragment where something already sits can always push
    /// content on, while an empty fragment has to keep the first line or child to make progress
    /// (Chromium's `IsAtFragmentainerStart` / `MovePastBreakpoint`).
    bool at_fragment_start = false;
};

}  // namespace layout
}  // namespace zui

#endif
