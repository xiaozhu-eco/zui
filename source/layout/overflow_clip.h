#ifndef ZUI_LAYOUT_OVERFLOW_CLIP_H
#define ZUI_LAYOUT_OVERFLOW_CLIP_H

#include "layout/box.h"

namespace zui {
namespace layout {

bool clips_overflow(const Box& box);
bool overflow_disjoint(const Box& box, float x, float y, float w, float h);

/// CSS Overflow §3.5: the viewport's used overflow is the root's; `visible` on the root
/// propagates the body's value, and remaining `visible` behaves as `auto`.
OverflowMode used_viewport_overflow(const Box& root, bool block_axis);

}  // namespace layout
}  // namespace zui

#endif
