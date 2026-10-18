#include "layout/paint_map_internal.h"

#include <unordered_map>
#include <unordered_set>

// The containment table lives in its own translation unit because the box tree, not the paint walk,
// is what frees boxes: `BoxTree::release_detached` / `BoxTree::reset` have to drop the entries of
// the boxes they just freed, and the standalone box-tree targets do not compile `paint_map.cpp`.

namespace zui {
namespace layout {
namespace paint_map_detail {

std::unordered_map<const Box*, Frame>& frames() {
    static std::unordered_map<const Box*, Frame> table;
    return table;
}

void forget_frames(const std::unordered_set<const Box*>& gone) {
    if (gone.empty()) {
        return;
    }
    std::unordered_map<const Box*, Frame>& table = frames();
    for (const Box* box : gone) {
        table.erase(box);
    }
}

void clear_frames() { frames().clear(); }

}  // namespace paint_map_detail
}  // namespace layout
}  // namespace zui
