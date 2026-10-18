#version 450
// Blur program. Separable gaussian of a mask texture. One axis per pass, blending off.
// shape_stroke is the radius, shape.xy the axis, shape_blur the sigma (CSS blur / 2).
// This shader does not evaluate box-shadow coverage or glyph winding.
layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vCol;
layout(location = 0) out vec4 o;
layout(binding = 0) uniform sampler2D uTex;
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

void main() {
    float s = (pc.scale > 0.0) ? pc.scale : 1.0;
    vec4 acc = vec4(0.0);
    float wsum = 0.0;
    float sig = max(pc.shape_blur, 0.0001);
    int rad = int(pc.shape_stroke + 0.5);
    if (rad > 48) rad = 48;
    if (rad < 0) rad = 0;
    ivec2 base = ivec2(gl_FragCoord.xy);
    ivec2 step = ivec2(int(pc.shape.x), int(pc.shape.y));
    ivec2 sz = ivec2(int(pc.screen.x * s + 0.5), int(pc.screen.y * s + 0.5));
    for (int i = -48; i <= 48; ++i) {
        if (i > rad || i < -rad) continue;
        float w = exp(-0.5 * float(i * i) / (sig * sig));
        ivec2 c = clamp(base + step * i, ivec2(0), sz - ivec2(1));
        acc += texelFetch(uTex, c, 0) * w;
        wsum += w;
    }
    o = acc / max(wsum, 0.0001);
    // Keep the fullscreen varyings live so the screen vertex stage stays linked.
    if (vCol.a < -1.0) {
        o = vec4(vUv, 0.0, 0.0);
    }
}
