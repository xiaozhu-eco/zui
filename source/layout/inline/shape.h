#ifndef ZUI_LAYOUT_INLINE_SHAPE_H
#define ZUI_LAYOUT_INLINE_SHAPE_H

#include "layout/cache/direction.h"
#include "layout/cache/shaped_run.h"
#include "layout/layout_style.h"

#include <string_view>
#include <vector>

namespace zui {
namespace layout {

class FreeTypeTextMetrics;

namespace inline_text {

// Segment by script + bidi, shape each run (HarfBuzz advances when available, else FreeType +
// GPOS kern), then L2-reorder into visual left-to-right ShapedRuns. `base` is the paragraph /
// embedding base direction (ConstraintSpace.direction), not necessarily style.direction.
std::vector<ShapedRun> shape(std::string_view text, const LayoutStyle& style, Direction base,
                             FreeTypeTextMetrics* metrics);

}  // namespace inline_text
}  // namespace layout
}  // namespace zui

#endif
