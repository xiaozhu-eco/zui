#ifndef ZUI_LAYOUT_CACHE_FLOAT_RECORD_H
#define ZUI_LAYOUT_CACHE_FLOAT_RECORD_H

#include "layout/cache/float_side.h"
#include "layout/cache/rect.h"
#include "layout/clip_path.h"

namespace zui {
namespace layout {

struct FloatRecord {
    FloatSide side = FloatSide::Left;
    Rect margin_box;
    bool pending = false;
    /// CSS Shapes `shape-outside` resolved against `margin_box` (reference box = margin-box).
    /// `Kind::None` keeps the CSS 2.1 rectangular exclusion.
    ClipPathShape shape;
    float shape_margin = 0;
};

}  // namespace layout
}  // namespace zui

#endif
