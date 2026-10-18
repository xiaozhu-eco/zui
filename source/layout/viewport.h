#ifndef ZUI_LAYOUT_VIEWPORT_H
#define ZUI_LAYOUT_VIEWPORT_H

#include "layout/cache/constraint_space.h"

#include <algorithm>

namespace zui {
namespace layout {

struct Viewport {
    float width = 0;
    float height = 0;
};

// Classic viewport scrollbars. vw/vh stay on the first viewport; icb and available shrink once.
// A bar that appears on one axis can force the other axis over in the same pass.
// `scrollbar_thickness` is the injected platform parameter (LAYOUT.md section 9), not a constant.
inline ConstraintSpace fit_viewport_scrollbars(ConstraintSpace space, float border_inline,
                                               float border_block, float scrollbar_thickness) {
    bool vertical =
        space.available.block_size.definite && border_block > space.available.block_size.px + 0.5f;
    bool horizontal = space.available.inline_size.definite &&
                      border_inline > space.available.inline_size.px + 0.5f;
    const float kScrollbar = scrollbar_thickness;
    if (vertical && !horizontal && space.available.inline_size.definite &&
        border_inline > space.available.inline_size.px - kScrollbar + 0.5f) {
        horizontal = true;
    }
    if (horizontal && !vertical && space.available.block_size.definite &&
        border_block > space.available.block_size.px - kScrollbar + 0.5f) {
        vertical = true;
    }
    if (!vertical && !horizontal) {
        return space;
    }
    if (vertical) {
        space.available.inline_size.px = std::max(0.f, space.available.inline_size.px - kScrollbar);
        space.icb.inline_size = space.available.inline_size.px;
    }
    if (horizontal) {
        space.available.block_size.px = std::max(0.f, space.available.block_size.px - kScrollbar);
        space.icb.block_size = space.available.block_size.px;
    }
    space.percentage.inline_size = space.available.inline_size;
    space.percentage.block_size = space.available.block_size;
    space.containing_block.inline_size = space.available.inline_size;
    space.containing_block.block_size = space.available.block_size;
    return space;
}

}  // namespace layout
}  // namespace zui

#endif
