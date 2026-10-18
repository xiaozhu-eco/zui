#ifndef ZUI_LAYOUT_CACHE_BOX_GEOMETRY_H
#define ZUI_LAYOUT_CACHE_BOX_GEOMETRY_H

#include "layout/cache/baseline_set.h"
#include "layout/cache/edges.h"

namespace zui {
namespace layout {

struct BoxGeometry {
    Edges margin;
    Edges border;
    Edges padding;
    float border_box_inline = 0;
    float border_box_block = 0;
    float content_inline = 0;
    float content_block = 0;
    BaselineSet baselines;
    /// LAYOUT.md section 9 step B: somewhere in this subtree is a box that paints where its layout
    /// geometry does not say - a `fixed` or `sticky` box (it follows the viewport or the scroll
    /// offset) or one with a `transform` / `filter`. Painting may not cull a subtree that carries
    /// the flag, and the flag has to reach every ancestor, which is why `LayoutContext::store`
    /// merges it out of the children's own contribution snapshots.
    bool subtree_exempt = false;
};

}  // namespace layout
}  // namespace zui

#endif
