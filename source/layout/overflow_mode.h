#ifndef ZUI_LAYOUT_OVERFLOW_MODE_H
#define ZUI_LAYOUT_OVERFLOW_MODE_H

namespace zui {
namespace layout {

// The CSS overflow keyword for one axis. The used overflow rectangles live in
// `layout/cache/overflow.h` (`Overflow`); this header used to share that name.
enum class OverflowMode { Visible, Hidden, Clip, Auto, Scroll };

}  // namespace layout
}  // namespace zui

#endif
