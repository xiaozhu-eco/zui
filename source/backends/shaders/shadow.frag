#version 450
// Shadow program. Analytic box-shadow coverage only. A separable gaussian lives in blur.frag.
// The premul composite of a mask lives in layer.frag. Glyph winding is not evaluated here.
layout(location = 1) in vec4 vCol;
layout(location = 0) out vec4 o;
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

float sd_round4(vec2 p, vec4 box, vec4 radx, vec4 rady) {
    vec2 half_size = box.zw * 0.5;
    vec2 q = p - (box.xy + half_size);
    vec4 rx4 = vec4(radx.z, radx.y, radx.w, radx.x);
    vec4 ry4 = vec4(rady.z, rady.y, rady.w, rady.x);
    rx4.xy = (q.x > 0.0) ? rx4.xy : rx4.zw;
    ry4.xy = (q.x > 0.0) ? ry4.xy : ry4.zw;
    float rx = min(max((q.y > 0.0) ? rx4.x : rx4.y, 0.0), half_size.x);
    float ry = min(max((q.y > 0.0) ? ry4.x : ry4.y, 0.0), half_size.y);
    if (abs(rx - ry) < 0.001) {
        float rad = rx;
        vec2 m = abs(q) - half_size + rad;
        return min(max(m.x, m.y), 0.0) + length(max(m, 0.0)) - rad;
    }
    vec2 r = max(vec2(rx, ry), vec2(1e-6));
    vec2 d = abs(q) - half_size + r;
    float inner = min(max(d.x, d.y), 0.0);
    vec2 m = max(d, 0.0);
    float outside;
    if (m.x > 0.0 && m.y > 0.0) {
        vec2 n = m / r;
        float len = length(n);
        outside = (len - 1.0) * length(r * (n / max(len, 1e-6)));
    } else {
        float rad = (m.x > 0.0) ? r.x : ((m.y > 0.0) ? r.y : min(r.x, r.y));
        outside = length(m) - rad;
    }
    return inner + outside;
}

float cover(float sd) {
    float w = max(fwidth(sd), 0.001);
    return smoothstep(w, -w, sd);
}

// Chernov CDF. sigma is the CSS blur radius / 2. sigma == 0 keeps a hard antialiased edge.
float shadow_cover(float sd, float sigma) {
    float x = -sd / max(sigma, 0.0001);
    float t = 1.0 / (1.0 + 0.2316419 * abs(x));
    float d = 0.3989423 * exp(-0.5 * x * x);
    float p = d * t * (0.3193815 + t * (-0.3565638 + t * (1.781478 + t * (-1.821256 + t * 1.330274))));
    return (x > 0.0) ? 1.0 - p : p;
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
    vec2 sp = p;
    bool aff2_shape = (pc.grad_count == -8);
    if (aff2_shape) {
        sp = vec2(pc.grad_line.x * p.x + pc.grad_line.y * p.y + pc.grad_line.z,
                  pc.grad_line.w * p.x + pc.grad_off.x * p.y + pc.grad_off.y);
    }
    float sd = sd_round4(sp, pc.shape, pc.shape_radii, pc.shape_radii_y);
    float cov = (pc.shape_blur > 0.0) ? shadow_cover(sd, pc.shape_blur) : cover(sd);
    if (pc.grad_count >= 0 && pc.shape_stroke > 0.0) {
        cov = max(cov - cover(sd + pc.shape_stroke), 0.0);
    }
    o = vec4(vCol.rgb, vCol.a * cov * clip_c);
}
