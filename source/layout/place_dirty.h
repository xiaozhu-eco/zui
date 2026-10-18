#ifndef ZUI_LAYOUT_PLACE_DIRTY_H
#define ZUI_LAYOUT_PLACE_DIRTY_H

#include "layout/box.h"

namespace zui {
namespace layout {

/// docs/PERF_REFACTOR.md stage P: "the placement of this box and of everything below it has to be
/// recomputed before the next paint". The ancestor bits are what let the world walk enter a dirty
/// subtree without testing every box: a box whose subtree is skipped keeps the world geometry the
/// last walk published for it, which is only sound because its inputs - and those of every
/// ancestor - did not move.
///
/// Call this from the writers of a placement input: a stored layout result, a replayed child
/// offset, an out-of-flow static position, a scroll offset that moved, a length transition's new
/// used value, and a box-tree edit. It walks the box's ancestor chain, so the cost is the box's
/// depth, not the tree.
inline void mark_place_dirty(Box& box) {
    box.place_dirty = true;
    for (Box* ancestor = box.parent; ancestor != nullptr; ancestor = ancestor->parent) {
        if (ancestor->place_below) {
            // Everything above an already-marked ancestor is marked too, so the walk can stop.
            break;
        }
        ancestor->place_below = true;
    }
}

}  // namespace layout
}  // namespace zui

#endif
