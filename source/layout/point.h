#ifndef ZUI_LAYOUT_POINT_H
#define ZUI_LAYOUT_POINT_H

namespace zui {
namespace layout {

inline bool point_in_rect(float x, float y, float rx, float ry, float rw, float rh) {
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

}  // namespace layout
}  // namespace zui

#endif
