#ifndef ZUI_SOURCE_BACKENDS_GLYPH_OUTLINES_H
#define ZUI_SOURCE_BACKENDS_GLYPH_OUTLINES_H

// Em-space outlines for GPU coverage. One entry per (face, glyph), not per pixel size or
// subpixel phase. The GPU buffer is a flat array of these curves; a painted glyph is one quad
// that names a range.

#include "paint/display_list.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace zui {
namespace backends {

struct GpuGlyphCurve {
    float a[4];  // p0.xy, p1.xy  (em, y up)
    float b[4];  // p2.xy, p3.xy
    std::int32_t kind = 0;  // 1 line, 2 quadratic, 3 cubic
    std::int32_t pad[3] = {0, 0, 0};
};

// Em corner plus the matrix that takes it to the screen. The curve shelf stays in em, so a
// second font size does not rewrite it. Six vertices of one quad share the matrix.
struct GlyphVert {
    float em_x = 0;
    float em_y = 0;
    float r = 0;
    float g = 0;
    float b = 0;
    float a = 0;
    std::uint32_t curve0 = 0;
    std::uint32_t count = 0;
    float m00 = 1;
    float m01 = 0;
    float m02 = 0;
    float m10 = 0;
    float m11 = 1;
    float m12 = 0;
};

static_assert(sizeof(GpuGlyphCurve) == 48, "std430 Curve");
static_assert(sizeof(GlyphVert) == 56, "glyph vertex");

class GlyphOutlines {
public:
    GlyphOutlines();
    ~GlyphOutlines();
    GlyphOutlines(const GlyphOutlines&) = delete;
    GlyphOutlines& operator=(const GlyphOutlines&) = delete;

    struct Draw {
        bool ok = false;
        std::uint32_t curve0 = 0;
        std::uint32_t count = 0;
        float min_x = 0;
        float min_y = 0;
        float max_x = 0;
        float max_y = 0;
        float advance = 0;
        std::uint32_t face_key = 0;
        std::uint32_t glyph_index = 0;
    };

    Draw prepare(std::uint32_t cp, float font_px, std::string_view family, int weight, bool italic,
                 std::uint32_t glyph_id);
    float kerning(std::string_view family, int weight, bool italic, float font_px,
                  std::uint32_t left_face, std::uint32_t left_glyph, std::uint32_t right_face,
                  std::uint32_t right_glyph);

    const std::vector<GpuGlyphCurve>& curves() const { return curves_; }

private:
    struct Chain;
    struct Span {
        std::uint32_t curve0 = 0;
        std::uint32_t count = 0;
        float min_x = 0;
        float min_y = 0;
        float max_x = 0;
        float max_y = 0;
        float advance_em = 0;
    };
    Chain* chain_for(std::string_view family, int weight, bool italic);
    std::unordered_map<std::string, std::unique_ptr<Chain>> chains_;
    std::unordered_map<std::uint64_t, Span> spans_;
    std::vector<GpuGlyphCurve> curves_;
    // The last chain_for key. A run asks for the same face once per glyph; building the map key
    // each time allocated two strings under /Od.
    Chain* last_chain_ = nullptr;
    std::string last_family_;
    int last_weight_ = 0;
    bool last_italic_ = false;
    bool last_valid_ = false;
};

/// Append one DrawText item. Layout advances, when present, move the pen. A glyph with no
/// outline (a space) still consumes its advance and emits nothing.
void append_glyph_quads(GlyphOutlines& outlines, const paint::DisplayItem& item,
                        std::vector<GlyphVert>& verts);

// Temporary submit split. Remove with the probe clocks.
struct GlyphPaintDiag {
    double prepare_ms = 0;
    double load_ms = 0;
    double xfer_ms = 0;
    double append_ms = 0;
    double capture_ms = 0;
    std::uint64_t glyphs = 0;
    std::uint64_t span_miss = 0;
};
GlyphPaintDiag take_glyph_paint_diag();

}  // namespace backends
}  // namespace zui

#endif
