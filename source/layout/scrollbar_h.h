#ifndef ZUI_LAYOUT_SCROLLBAR_H_H
#define ZUI_LAYOUT_SCROLLBAR_H_H

#include "layout/box.h"

namespace zui {
namespace layout {

struct ScrollbarH {
    bool visible = false;
    float track_x = 0;
    float track_y = 0;
    float track_w = 0;
    float track_h = 0;
    float thumb_x = 0;
    float thumb_w = 0;
    float max_scroll = 0;
};

/// The viewport's horizontal bar, in viewport coordinates. See `viewport_vertical_scrollbar`.
ScrollbarH viewport_horizontal_scrollbar(const Box& root, float viewport_w, float viewport_h,
                                         float thickness);

/// The bar an element scroll container draws on its inline axis. See `vertical_scrollbar`.
ScrollbarH horizontal_scrollbar(const Box& box, float thickness);

}  // namespace layout
}  // namespace zui

#endif
