#ifndef ZUI_LAYOUT_CACHE_GLYPH_INLINE_H
#define ZUI_LAYOUT_CACHE_GLYPH_INLINE_H

#include <cstdint>

namespace zui {
namespace layout {

struct GlyphInline {
    float offset = 0;
    float advance = 0;
    // Em-box top, measured from the line box block-start. Baseline alignment leaves this at the half-leading.
    float block_offset = 0;
    // Set by shape from the codepoint. The line breaker wraps here; spaces can also be dropped.
    bool wrap_after = false;
    bool collapsible = false;
    bool forced_break = false;
    /// 0 paints the codepoint's cmap glyph. `text::kSkipPaintGlyph` advances the pen and draws
    /// nothing (a ligature's later clusters). Any other value is the glyph id to paint, loaded
    /// from the face this codepoint maps to.
    std::uint32_t paint_glyph = 0;
};

}  // namespace layout
}  // namespace zui

#endif
