#ifndef ZUI_LAYOUT_GRID_GRID_H
#define ZUI_LAYOUT_GRID_GRID_H

#include "layout/cache/layout_result.h"

namespace zui {
namespace layout {

struct Box;
struct LayoutKey;
class LayoutContext;

// CSS Grid. Track sizes stay in solve locals. Measure asks MinContent/MaxContent for
// track contributions; Final asks happen in compute before place writes offsets.
// Place does not ask. Out-of-flow children are noted for the containing-block ask.
LayoutResult layout_grid(Box& box, const LayoutKey& key, LayoutContext& cx);

}  // namespace layout
}  // namespace zui

#endif
