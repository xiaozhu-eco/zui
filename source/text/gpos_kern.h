#ifndef ZUI_TEXT_GPOS_KERN_H
#define ZUI_TEXT_GPOS_KERN_H

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H

#include <cstdint>

namespace zui {
namespace text {

/// The GPOS `kern` pair adjustment between two glyphs of one face, in design units. Zero when the
/// face has no GPOS table, no `kern` feature, or no adjustment for this pair.
///
/// FreeType's `FT_Get_Kerning` reads only the legacy `kern` table, and the faces this project
/// measures ship their kerning in GPOS instead, so the pair lookups are read here. Only PairPos
/// Format 1 (per-glyph pairs) and Format 2 (class-based pairs) are understood, which is what a
/// `kern` feature uses; ligatures and mark positioning are not part of it.
///
/// `weight` is the instance the face is loaded at: a variable font substitutes its feature tables
/// through FeatureVariations, and the weight axis is the one this engine instances.
std::int16_t gpos_kern_units(FT_Face face, int weight, std::uint32_t left, std::uint32_t right);

}  // namespace text
}  // namespace zui

#endif
