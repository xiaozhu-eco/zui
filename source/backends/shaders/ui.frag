#version 450
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
    // A gradient rides mode 2: the display list resolves the line (xy start, zw end) and the stop
    // offsets, so the shader only walks them. Four stops is what this block carries; with them the
    // block is 256 bytes, which desktop implementations allow (the guaranteed minimum is 128).
    // `scale` sits in the std430 padding before grad_line (offset 152); content_scale maps
    // physical FragCoord into logical DisplayList space (1.0 keeps the scale==1 path identical).
    int grad_count;
    float scale;
    // Kept so this block stays 256 bytes and lines up with the other effect programs.
    // Box-shadow softness lives in shadow.frag, not here.
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

// radx/rady = (tl, tr, br, bl). y grows downward in display-list space.
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
    float inside = min(max(d.x, d.y), 0.0);
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
    return inside + outside;
}

float cover(float sd) {
    float w = max(fwidth(sd), 0.001);
    return smoothstep(w, -w, sd);
}

// Stops interpolate in premultiplied space, which is what CSS Images 3 section 3.4.4 asks for and
// what gradient_color_at() does on the CPU path. Negative grad_count = abs stops with t-range
// clamp so backends can loop >4-stop gradients without growing the 256-byte push-constant block.
vec4 grad(vec2 p) {
    vec2 line = pc.grad_line.zw - pc.grad_line.xy;
    float len = max(length(line), 0.0001);
    float t = dot(p - pc.grad_line.xy, line / len) / len;
    bool clamp_range = pc.grad_count < 0;
    int n = clamp_range ? -pc.grad_count : pc.grad_count;
    float t_lo = pc.grad_off.x;
    float t_hi = (n <= 1) ? pc.grad_off.x : ((n == 2) ? pc.grad_off.y : ((n == 3) ? pc.grad_off.z : pc.grad_off.w));
    if (clamp_range) {
        bool is_last = pc.shape_stroke > -1.5 && pc.shape_stroke < -0.5;
        bool is_first = pc.shape_stroke < -1.5;
        if (!is_first && t < t_lo - 1e-5) {
            return vec4(0.0);
        }
        if (!is_last && t >= t_hi - 1e-5) {
            return vec4(0.0);
        }
    }
    vec4 prev = pc.grad_col[0];
    float po = pc.grad_off.x;
    if (t <= po) {
        return prev;
    }
    for (int i = 1; i < 4; ++i) {
        if (i >= n) {
            break;
        }
        vec4 cur = pc.grad_col[i];
        float co = (i == 1) ? pc.grad_off.y : ((i == 2) ? pc.grad_off.z : pc.grad_off.w);
        if (t <= co) {
            float f = (co > po) ? (t - po) / (co - po) : 0.0;
            vec4 a = vec4(prev.rgb * prev.a, prev.a);
            vec4 b = vec4(cur.rgb * cur.a, cur.a);
            vec4 m = mix(a, b, f);
            return vec4(m.a > 0.0 ? m.rgb / m.a : vec3(0.0), m.a);
        }
        prev = cur;
        po = co;
    }
    return prev;
}

// round_r[i] < -0.5 packs an Aff2 clip across three slots: local rect, (ia,ib,ic,id),
// (ie,iff,rad,0). Positive radii keep the axis-aligned rounded-clip path.
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
    vec4 col = vCol;
    if (pc.mode == 2) {
        // grad_count == -8: Aff2 rounded fill. Inverse Aff2 lives in grad_line / grad_off
        // (ia,ib,ic,id) / (ie,iff,*,*) so the 256-byte push-constant block stays put.
        // mode 2 is solid-versus-shape inside the fill program. Image, shadow, and glyph
        // coverage are other programs.
        vec2 sp = p;
        bool aff2_shape = (pc.grad_count == -8);
        if (aff2_shape) {
            sp = vec2(pc.grad_line.x * p.x + pc.grad_line.y * p.y + pc.grad_line.z,
                      pc.grad_line.w * p.x + pc.grad_off.x * p.y + pc.grad_off.y);
        }
        float sd = sd_round4(sp, pc.shape, pc.shape_radii, pc.shape_radii_y);
        float cov = cover(sd);
        if (pc.grad_count >= 0 && pc.shape_stroke > 0.0) {
            cov = max(cov - cover(sd + pc.shape_stroke), 0.0);
        }
        vec4 base = (pc.grad_count != 0 && !aff2_shape) ? grad(p) : vCol;
        col = vec4(base.rgb, base.a * cov);
    }
    col.a *= clip_c;
    o = col;
}
