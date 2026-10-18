#version 450
// Image program. Samples the bound texture and multiplies by the vertex color. It does not
// fill shapes, blur shadows, or evaluate glyph coverage.
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

float sd_round(vec2 p, vec4 box, float rad) {
    vec2 half_size = box.zw * 0.5;
    rad = min(max(rad, 0.0), min(half_size.x, half_size.y));
    vec2 q = abs(p - (box.xy + half_size)) - half_size + rad;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - rad;
}

float cover(float sd) {
    float w = max(fwidth(sd), 0.001);
    return smoothstep(w, -w, sd);
}

float clip_cover(vec2 p) {
    float c = 1.0;
    int i = 0;
    while (i < pc.round_count && i < 4) {
        float rr = (i == 0) ? pc.round_r.x : ((i == 1) ? pc.round_r.y : ((i == 2) ? pc.round_r.z : pc.round_r.w));
        if (rr < -0.5) {
            if (i + 2 >= 4) {
                break;
            }
            vec4 local = pc.round_rect[i];
            vec4 inv0 = pc.round_rect[i + 1];
            vec4 inv1 = pc.round_rect[i + 2];
            float lx = inv0.x * p.x + inv0.y * p.y + inv0.z;
            float ly = inv0.w * p.x + inv1.x * p.y + inv1.y;
            float rad = inv1.z;
            if (rad > 0.5) {
                c *= cover(sd_round(vec2(lx, ly), local, rad));
            } else if (lx < local.x || ly < local.y || lx >= local.x + local.z ||
                       ly >= local.y + local.w) {
                c = 0.0;
            }
            i += 3;
        } else {
            c *= cover(sd_round(p, pc.round_rect[i], rr));
            i += 1;
        }
    }
    return c;
}

void main() {
    float s = (pc.scale > 0.0) ? pc.scale : 1.0;
    vec2 p = gl_FragCoord.xy / s;
    float clip_c = clip_cover(p);
    if (clip_c <= 0.0) {
        discard;
    }
    vec4 col = texture(uTex, vUv) * vCol;
    col.a *= clip_c;
    o = col;
}
