#ifndef ZUI_BACKENDS_GLYPH_ATLAS_H
#define ZUI_BACKENDS_GLYPH_ATLAS_H

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace zui {
namespace backends {

struct Glyph {
    float w = 0;
    float h = 0;
    float bearing_x = 0;
    float bearing_y = 0;
    float advance = 0;
    float u0 = 0;
    float v0 = 0;
    float u1 = 0;
    float v1 = 0;
    /// Face/glyph ids for GPOS kerning between consecutive painted glyphs.
    std::uint32_t face_key = 0;
    std::uint32_t glyph_index = 0;
};

class GlyphAtlas {
public:
    GlyphAtlas();
    ~GlyphAtlas();

    bool ready() const { return ready_; }
    /// `weight` is CSS font-weight (400 normal / 700 bold). Italic selects italic/oblique faces.
    /// `glyph_id` 0 loads the cmap glyph. Any other id loads that glyph from the face `cp`
    /// resolves to (a ligature drawn at its first codepoint). `pen_x` / `pen_y` select the
    /// DirectWrite subpixel phase on Windows.
    // `coverage_green` selects Skia's coverage table for this text color. Masks for two colors
    // do not share a cache slot.
    const Glyph* get(std::uint32_t cp, float font_px, std::string_view family, int weight = 400,
                     bool italic = false, std::uint32_t glyph_id = 0, float pen_x = 0,
                     float pen_y = 0, std::uint8_t coverage_green = 0);
    /// Extra advance between two glyphs of one face at `font_px`. Zero across faces or when the
    /// face has no kerning. Matches `text::FontChain::kerning` used by layout.
    float kerning(std::string_view family, int weight, bool italic, float font_px,
                  std::uint32_t left_face, std::uint32_t left_glyph, std::uint32_t right_face,
                  std::uint32_t right_glyph);
    const std::uint8_t* pixels() const { return pixels_.data(); }
    int width() const { return width_; }
    int height() const { return height_; }
    bool take_dirty() {
        bool d = dirty_;
        dirty_ = false;
        return d;
    }
    /// Device limit for one edge. `reserve_edge` and `grow` will not pass it.
    void set_max_edge(int edge);
    /// Allocate an empty atlas of at least `edge` pixels on a side, before any glyph is packed.
    /// A chapter is about a thousand distinct CJK characters, each cached again per subpixel
    /// phase, and that does not fit in the 1024² default.
    void reserve_edge(int edge);
    /// `pack` failed because the current texture has no free row. `grow` doubles the texture.
    bool needs_grow() const { return needs_grow_; }
    /// Double the texture, keep every packed glyph's pixels, and rescale its UVs. False when
    /// `max_edge` is already reached; further misses stay misses instead of retrying forever.
    bool grow();

private:
    bool pack(int w, int h, int& x, int& y);

    bool ready_ = false;
    bool dirty_ = true;
    bool needs_grow_ = false;
    bool grow_exhausted_ = false;
    int width_ = 1024;
    int height_ = 1024;
    int max_edge_ = 8192;
    int row_x_ = 1;
    int row_y_ = 1;
    int row_h_ = 0;
    std::vector<std::uint8_t> pixels_;
    struct Chain;
    std::unordered_map<std::uint64_t, Glyph> glyphs_;
    std::unordered_map<std::string, std::unique_ptr<Chain>> chains_;
};

}  // namespace backends
}  // namespace zui

#endif
