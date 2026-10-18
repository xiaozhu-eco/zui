#include "backends/draw_effect.h"

#include <cmath>

namespace zui {
namespace backends {

EffectChoice effect_for(const paint::DisplayItem& it) {
    EffectChoice c;
    switch (it.op) {
    case paint::DisplayOp::DrawImage:
        c.program = GpuProgram::Image;
        break;
    case paint::DisplayOp::ShadowRect:
        c.program = GpuProgram::Shadow;
        c.blur = it.blur > 0.f ? it.blur : 0.f;
        break;
    case paint::DisplayOp::DrawText:
        if (it.blur > 0.f) {
            c.program = GpuProgram::Shadow;
            c.blur = it.blur;
        } else {
            c.program = GpuProgram::Glyph;
        }
        break;
    default:
        c.program = GpuProgram::Fill;
        break;
    }
    return c;
}

GpuProgram program_for_batch(int mode, bool es) {
    switch (mode) {
    case kBatchImage:
        return GpuProgram::Image;
    case kBatchGlyph:
    case kBatchShadowGlyph:
        // The mask is the glyph program. ES has no glyph program and does not fall back to an atlas.
        return es ? GpuProgram::None : GpuProgram::Glyph;
    case kBatchShadow:
        return GpuProgram::Shadow;
    case kBatchShadowGauss:
        return GpuProgram::Blur;
    case kBatchShadowBlit:
    case kBatchLayer:
        return GpuProgram::Layer;
    case kBatchBackdrop:
        return GpuProgram::Image;
    default:
        return GpuProgram::Fill;
    }
}

bool batch_uses_glyph_verts(int mode) {
    return mode == kBatchGlyph || mode == kBatchShadowGlyph;
}

int batch_for(const paint::DisplayItem& it) {
    const EffectChoice effect = effect_for(it);
    switch (effect.program) {
    case GpuProgram::Image:
        return kBatchImage;
    case GpuProgram::Glyph:
        return kBatchGlyph;
    case GpuProgram::Shadow:
        return it.op == paint::DisplayOp::DrawText ? kBatchShadowGlyph : kBatchShadow;
    case GpuProgram::None:
    case GpuProgram::Blur:
    case GpuProgram::Layer:
    case GpuProgram::Fill:
        return kBatchFill;
    }
    return kBatchFill;
}

GlyphMatrix glyph_matrix(float font_px, paint::TextGlyphOrient orient, float pen_x, float pen_y,
                         bool has_xform, float a, float b, float c, float d, float e, float f) {
    GlyphMatrix m;
    if (orient == paint::TextGlyphOrient::SidewaysRl) {
        m.m00 = 0.f;
        m.m01 = font_px;
        m.m02 = pen_x;
        m.m10 = font_px;
        m.m11 = 0.f;
        m.m12 = pen_y;
    } else if (orient == paint::TextGlyphOrient::SidewaysLr) {
        m.m00 = 0.f;
        m.m01 = -font_px;
        m.m02 = pen_x;
        m.m10 = -font_px;
        m.m11 = 0.f;
        m.m12 = pen_y;
    } else {
        m.m00 = font_px;
        m.m01 = 0.f;
        m.m02 = pen_x;
        m.m10 = 0.f;
        m.m11 = -font_px;
        m.m12 = pen_y;
    }
    if (!has_xform) {
        return m;
    }
    GlyphMatrix r;
    r.m00 = a * m.m00 + b * m.m10;
    r.m01 = a * m.m01 + b * m.m11;
    r.m02 = a * m.m02 + b * m.m12 + c;
    r.m10 = d * m.m00 + e * m.m10;
    r.m11 = d * m.m01 + e * m.m11;
    r.m12 = d * m.m02 + e * m.m12 + f;
    return r;
}

float glyph_matrix_scale(const GlyphMatrix& m) { return std::hypot(m.m00, m.m10); }

void glyph_apply(const GlyphMatrix& m, float em_x, float em_y, float& sx, float& sy) {
    sx = m.m00 * em_x + m.m01 * em_y + m.m02;
    sy = m.m10 * em_x + m.m11 * em_y + m.m12;
}

}  // namespace backends
}  // namespace zui
