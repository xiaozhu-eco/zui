#version 450
#extension GL_GOOGLE_include_directive : require
// Vector glyph coverage. Lines and quadratics are solved analytically; a cubic is flattened
// in this shader (16 segments). The storage buffer is the whole outline shelf, appended as
// new glyphs appear. Boxes, gradients, and images stay on `ui.frag`.
layout(location = 0) in vec2 vEm;
layout(location = 1) in vec4 vCol;
layout(location = 2) flat in uvec2 vRange;
layout(location = 0) out vec4 o;

struct Curve {
    vec4 a;
    vec4 b;
    int kind;
    int pad0;
    int pad1;
    int pad2;
};

layout(std430, binding = 0) readonly buffer CurveBlock { Curve curves[]; };

layout(push_constant) uniform PC {
    vec2 screen;
    int mode;
    int round_count;
    vec4 round_rect[4];
    vec4 round_r;
    vec4 shape;
    vec4 shape_radii;
    vec4 shape_radii_y;
    float shape_stroke;
    int grad_count;
    float scale;
    float shape_blur;
    vec4 grad_line;
    vec4 grad_off;
    vec4 grad_col[4];
} pc;

#define zui_scale ((pc.scale > 0.0) ? pc.scale : 1.0)
#define zui_round_count pc.round_count
#define zui_round_rect pc.round_rect
#define zui_round_r pc.round_r
#define zui_frag_xy (gl_FragCoord.xy / zui_scale)

#include "glyph_cover.glsl"
