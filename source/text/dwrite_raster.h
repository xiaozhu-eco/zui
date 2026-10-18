#ifndef ZUI_TEXT_DWRITE_RASTER_H
#define ZUI_TEXT_DWRITE_RASTER_H

#include <cstdint>
#include <string>
#include <vector>

namespace zui {
namespace text {

/// Grayscale mask for one glyph. `left` / `top` match FreeType `bitmap_left` / `bitmap_top`
/// (top is positive above the baseline) so the existing blit can place it.
struct DWriteMask {
    bool ok = false;
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> alpha;
};

/// Rasterize `glyph` with DirectWrite natural-symmetric ClearType, averaged to one gray byte.
/// `origin_x` / `origin_y` are the fractional baseline in pixels, Y down, each in [0, 1).
/// `axis_tags` are FreeType's big-endian design tags. Returns false where DirectWrite is
/// unavailable or the face has no mask, and the caller keeps the FreeType bitmap. Advances are
/// not read from here. The coverage warp for the text color is applied by the caller.
/// `synthetic_bold` asks DirectWrite for `DWRITE_FONT_SIMULATIONS_BOLD`: it is how the browser
/// draws a `font-weight: 700` run whose family has no bold face (see `kSyntheticBoldEm` in
/// `font_chain.cpp`). The simulated face carries the same stroke the browser's does, because both
/// are DirectWrite's.
bool dwrite_raster_glyph(const std::string& path, std::uint32_t glyph, float px, float origin_x,
                         float origin_y, const std::uint32_t* axis_tags, const float* axis_values,
                         int axis_count, DWriteMask& out, bool synthetic_bold = false);

}  // namespace text
}  // namespace zui

#endif
