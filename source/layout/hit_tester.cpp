#include "layout/hit_tester.h"

#include "layout/paint_map.h"
#include "trace/trace.h"

namespace zui {
namespace layout {

Box* HitTester::hit(Box* root, float x, float y) const {
    // `pointer_move` walks the whole box tree from the root on every frame, and until now the walk
    // had no zone - it was one of the two segments of the frame `布局成本结构性修复方案.md`
    // section 1.5 could not name.
    zui::trace::Zone zone("hit_test", zui::trace::Category::Engine);
    return hit_mapped(root, x, y);
}

}  // namespace layout
}  // namespace zui
