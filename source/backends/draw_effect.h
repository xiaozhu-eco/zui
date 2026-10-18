#ifndef ZUI_SOURCE_BACKENDS_DRAW_EFFECT_H
#define ZUI_SOURCE_BACKENDS_DRAW_EFFECT_H

// Which GPU program draws a display item, and the 2x3 matrix that places a glyph.
// Vulkan, desktop GL, and OpenGL ES call these. A fragment program does not switch
// among fill, image, shadow, blur, layer composite, and glyph with a mode flag.

#include "paint/display_list.h"

#include <cstdint>

namespace zui {
namespace backends {

enum class GpuProgram : std::uint8_t {
    None = 0,
    Fill = 1,
    Image = 2,
    Shadow = 3,
    Glyph = 4,
    Blur = 5,
    Layer = 6,
};

struct EffectChoice {
    GpuProgram program = GpuProgram::Fill;
    float blur = 0.f;
};

/// Solid fill, image, shadow, or glyph. A blurred DrawText is a shadow, not a second glyph draw.
EffectChoice effect_for(const paint::DisplayItem& it);

// Batch tags stored by both recorders. Solid and rounded fills share the fill program.
// program_for_batch is what submit binds. `es` is the OpenGL ES path: it draws fill, image,
// shadow, blur, and layer composite, and it does not return a glyph program.
constexpr int kBatchFill = 0;
constexpr int kBatchFillShape = 2;
constexpr int kBatchImage = 3;
constexpr int kBatchGlyph = 4;
constexpr int kBatchShadowGauss = 5;
constexpr int kBatchShadow = 6;
constexpr int kBatchShadowBlit = 7;
constexpr int kBatchShadowGlyph = 8;
constexpr int kBatchLayer = 9;
constexpr int kBatchBackdrop = 10;

GpuProgram program_for_batch(int mode, bool es = false);
bool batch_uses_glyph_verts(int mode);

/// Recorder tag for an item. Fills stay kBatchFill; the caller upgrades a rounded fill to
/// kBatchFillShape. Both tags bind the fill program.
int batch_for(const paint::DisplayItem& it);

/// Em (y up) to screen (y down). Columns are the image of the em axes; translation is the pen.
/// An Aff2, when present, is applied on the left.
struct GlyphMatrix {
    float m00 = 1.f;
    float m01 = 0.f;
    float m02 = 0.f;
    float m10 = 0.f;
    float m11 = 1.f;
    float m12 = 0.f;
};

GlyphMatrix glyph_matrix(float font_px, paint::TextGlyphOrient orient, float pen_x, float pen_y,
                         bool has_xform, float a, float b, float c, float d, float e, float f);

/// Length of the em-x column. Upright and sideways with no extra affine scale equal font_px.
float glyph_matrix_scale(const GlyphMatrix& m);

void glyph_apply(const GlyphMatrix& m, float em_x, float em_y, float& sx, float& sy);

}  // namespace backends
}  // namespace zui

#endif
