#ifndef ZUI_TEXT_HB_SHAPE_H
#define ZUI_TEXT_HB_SHAPE_H

#include <ft2build.h>
#include FT_FREETYPE_H

#include <cstddef>
#include <cstdint>

namespace zui {
namespace text {

/// One output slot from `hb_shape_advances`, parallel to an input codepoint.
struct HbGlyphAdvance {
    float advance = 0;
    /// Glyph HarfBuzz picked. Every component of a ligature carries the ligature id so pair
    /// kerning sees the glyph the font actually drew, not the unligated letters.
    std::uint32_t glyph_index = 0;
    /// False for a ligature continuation: the advance is that cluster's share of the ligature,
    /// and paint must not draw a second copy of the glyph.
    bool draw = true;
    /// HarfBuzz kept the caller's cmap glyph, so `advance` was not recomputed: the caller's
    /// advance for that glyph stands (it already carries the synthetic bold delta).
    bool unchanged = false;
};

/// Shape `cps[0..count)` with HarfBuzz on `face` (already sized). Writes one slot per
/// codepoint. `liga` is on when `ligatures` is set; `clig` / `calt` stay off, and `kern` stays
/// off — pair kerning comes from `FontChain::kerning`, because hb-ft answers in 1/64 px and its
/// rounded delta would move the run width. A ligature is split back into one slot per source
/// codepoint using the GDEF ligature caret (design units, scaled with the unhinted advance).
/// The base advance is FreeType `linearHoriAdvance`, not HarfBuzz's 26.6 value.
/// Returns false when HarfBuzz is not compiled in, shaping fails, or a substitution is not a
/// ligature this splitter understands. `out` is left untouched on failure.
/// `cmap_glyphs`, when given, is the caller's codepoint-to-glyph map (`FontChain::load`'s answer)
/// and `out[i].advance` must already hold that glyph's advance. HarfBuzz keeps the glyph for most
/// slots; there the advance is the caller's value (same FT load flags, same `linearHoriAdvance`),
/// so the per-glyph FreeType load is skipped. A substituted glyph (a ligature, or a required
/// feature such as `ccmp`) still re-reads its advance.
bool hb_shape_advances(FT_Face face, const std::uint32_t* cps, std::size_t count,
                       HbGlyphAdvance* out, bool ligatures,
                       const std::uint32_t* cmap_glyphs = nullptr);

/// Drop the cached HarfBuzz font of `face` (`hb_shape_advances` keeps one per FT face so a shaped
/// span does not pay `hb_ft_font_create_referenced`). Call it wherever the face is closed: a
/// destroyed FT_Face's address can be reused by the next `FT_New_Face`, and the cache is keyed by
/// that address. No-op without HarfBuzz.
void hb_shape_forget(FT_Face face);

/// `apply_weight` changed this face's variation instance. The next `hb_shape_advances` re-reads
/// design coordinates. No-op without HarfBuzz.
void hb_shape_note_var(FT_Face face);

}  // namespace text
}  // namespace zui

#endif
