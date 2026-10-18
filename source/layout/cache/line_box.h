#ifndef ZUI_LAYOUT_CACHE_LINE_BOX_H
#define ZUI_LAYOUT_CACHE_LINE_BOX_H

#include "layout/cache/shaped_run.h"

#include <vector>

namespace zui {
namespace layout {

struct LineBox {
    float inline_start = 0;
    float inline_size = 0;
    float block_offset = 0;
    float block_size = 0;
    float baseline = 0;
    // The strut's font content area. `baseline` includes the half-leading that centres it in the
    // `line-height` box, so the two together say where the font box sits; a browser paints the
    // caret over exactly that box (CSS 2.1 section 10.8).
    float font_ascent = 0;
    float font_descent = 0;
    // Source characters this line consumed, counted in the element's own text, collapsed
    // whitespace and forced breaks included. A soft wrap hangs a collapsible space at the end of
    // the line it broke after, so this is the number a browser reports as the offset of the next
    // line's first painted character; the line breaker is the only place that knows it, because
    // it is also the only place that drops characters.
    std::size_t source_chars = 0;
    // The line ended on a br. Chrome's Range list has a second rect at this line's top
    // for that break, with the same start offset as the line itself.
    bool break_rect = false;
    std::vector<ShapedRun> runs;
};

}  // namespace layout
}  // namespace zui

#endif
