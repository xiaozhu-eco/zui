#ifndef ZUI_LAYOUT_PHYSICAL_EDGES_H
#define ZUI_LAYOUT_PHYSICAL_EDGES_H

namespace zui {
namespace layout {

struct PhysicalEdges {
    float top = 0;
    float right = 0;
    float bottom = 0;
    float left = 0;
    float horizontal() const { return left + right; }
    float vertical() const { return top + bottom; }
};

}  // namespace layout
}  // namespace zui

#endif
