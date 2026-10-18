#ifndef ZUI_LAYOUT_CACHE_SHAPED_RUN_H
#define ZUI_LAYOUT_CACHE_SHAPED_RUN_H

#include "layout/cache/glyph_inline.h"

#include <string>
#include <vector>

namespace zui {
namespace layout {

struct Box;

struct ShapedRun {
    float advance_inline = 0;
    std::vector<GlyphInline> glyphs;
    // Characters this run paints, in glyph order. Line breaking fills it.
    std::string text;
    // Content-box inline position of this run. Set when the run does not start at the
    // line's own inline start (an atomic inline sits before it on the same line).
    bool has_origin = false;
    float origin = 0;
    // Element whose font and color paint the run. Null uses the box that owns the line.
    Box* style_box = nullptr;
};

}  // namespace layout
}  // namespace zui

#endif
