// Shared fragment body for vector glyphs. The prologue (Vulkan push constants, or GL
// uniforms) defines `Curve`, `curves`, `vEm`, `vCol`, `vRange`, `o`, and the zui_* macros.
// Coverage is nonzero winding of the em-space outline. The CPU uploads control points only.

float sd_round(vec2 p, vec4 box, float rad) {
    vec2 half_size = box.zw * 0.5;
    rad = min(max(rad, 0.0), min(half_size.x, half_size.y));
    vec2 q = abs(p - (box.xy + half_size)) - half_size + rad;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - rad;
}

float edge_cover(float sd) {
    float w = max(fwidth(sd), 0.001);
    return smoothstep(w, -w, sd);
}

float round_at(int i) {
    if (i == 0) return zui_round_r.x;
    if (i == 1) return zui_round_r.y;
    if (i == 2) return zui_round_r.z;
    return zui_round_r.w;
}

// CSS `clip-path: polygon()`. `zui_poly_count` is negative for evenodd. Coverage matches the
// UI fill shader: distance to the nearest edge, then the same 1px linear ramp as `cover`.
float poly_cover(vec2 p) {
    int n = zui_poly_count < 0 ? -zui_poly_count : zui_poly_count;
    if (n < 3) {
        return 1.0;
    }
    bool even = zui_poly_count < 0;
    float dist = 1e9;
    int wind = 0;
    int hits = 0;
    for (int i = 0; i < 16; ++i) {
        if (i >= n) {
            break;
        }
        vec2 a = zui_poly[i];
        int j = i + 1;
        if (j >= n) {
            j = 0;
        }
        vec2 b = zui_poly[j];
        vec2 e = b - a;
        float el2 = dot(e, e);
        if (el2 > 1e-8) {
            float t = clamp(dot(p - a, e) / el2, 0.0, 1.0);
            dist = min(dist, length(p - (a + e * t)));
        }
        bool up = a.y <= p.y && b.y > p.y;
        bool down = b.y <= p.y && a.y > p.y;
        if (up || down) {
            float t = (p.y - a.y) / (b.y - a.y);
            if (mix(a.x, b.x, t) > p.x) {
                if (even) {
                    hits += 1;
                } else {
                    wind += up ? 1 : -1;
                }
            }
        }
    }
    bool inside = even ? (hits - 2 * (hits / 2)) != 0 : wind != 0;
    float sd = inside ? -dist : dist;
    return clamp(0.5 - sd, 0.0, 1.0);
}

float clip_cover(vec2 p) {
    float c = 1.0;
    int i = 0;
    while (i < zui_round_count && i < 4) {
        float rr = round_at(i);
        if (rr < -0.5) {
            if (i + 2 >= 4) {
                break;
            }
            vec4 local = zui_round_rect[i];
            vec4 inv0 = zui_round_rect[i + 1];
            vec4 inv1 = zui_round_rect[i + 2];
            float lx = inv0.x * p.x + inv0.y * p.y + inv0.z;
            float ly = inv0.w * p.x + inv1.x * p.y + inv1.y;
            float rad = inv1.z;
            if (rad > 0.5) {
                c *= edge_cover(sd_round(vec2(lx, ly), local, rad));
            } else if (lx < local.x || ly < local.y || lx >= local.x + local.z ||
                       ly >= local.y + local.w) {
                c = 0.0;
            }
            i += 3;
        } else {
            c *= edge_cover(sd_round(p, zui_round_rect[i], rr));
            i += 1;
        }
    }
    c *= poly_cover(p);
    return c;
}

float line_cross(vec2 a, vec2 b, vec2 p) {
    bool up = a.y <= p.y && b.y > p.y;
    bool down = b.y <= p.y && a.y > p.y;
    if (!up && !down) {
        return 0.0;
    }
    float t = (p.y - a.y) / (b.y - a.y);
    float x = mix(a.x, b.x, t);
    if (x <= p.x) {
        return 0.0;
    }
    return up ? 1.0 : -1.0;
}

float quad_at_x(vec2 p0, vec2 p1, vec2 p2, float t) {
    float u = 1.0 - t;
    return u * u * p0.x + 2.0 * u * t * p1.x + t * t * p2.x;
}

// A quadratic is split at its y extremum. Each monotonic piece then uses the same
// half-open rule as line_cross: include the lower endpoint, exclude the upper.
// t in [0, 1) does the opposite on a downward piece, so a shared vertex is counted
// twice and that scanline fills a horizontal through the counter.
float mono_quad(vec2 p0, vec2 p1, vec2 p2, vec2 p) {
    bool up = p0.y <= p.y && p2.y > p.y;
    bool down = p2.y <= p.y && p0.y > p.y;
    if (!up && !down) {
        return 0.0;
    }
    float a = p0.y - 2.0 * p1.y + p2.y;
    float b = 2.0 * (p1.y - p0.y);
    float c = p0.y - p.y;
    float t = -1.0;
    if (abs(a) < 1e-7) {
        if (abs(b) < 1e-7) {
            return 0.0;
        }
        t = -c / b;
    } else {
        float disc = max(b * b - 4.0 * a * c, 0.0);
        float s = sqrt(disc);
        float inv = 0.5 / a;
        float t0 = (-b - s) * inv;
        float t1 = (-b + s) * inv;
        bool in0 = t0 >= -1e-4 && t0 <= 1.0 + 1e-4;
        bool in1 = t1 >= -1e-4 && t1 <= 1.0 + 1e-4;
        float yp0 = 2.0 * a * t0 + b;
        float yp1 = 2.0 * a * t1 + b;
        bool sign0 = up ? yp0 >= 0.0 : yp0 <= 0.0;
        bool sign1 = up ? yp1 >= 0.0 : yp1 <= 0.0;
        if (in0 && (!in1 || sign0 || !sign1)) {
            t = clamp(t0, 0.0, 1.0);
        } else if (in1) {
            t = clamp(t1, 0.0, 1.0);
        } else {
            return 0.0;
        }
    }
    float x = quad_at_x(p0, p1, p2, t);
    if (x <= p.x) {
        return 0.0;
    }
    return up ? 1.0 : -1.0;
}

float quad_cross(vec2 p0, vec2 p1, vec2 p2, vec2 p) {
    float a = p0.y - 2.0 * p1.y + p2.y;
    float b = 2.0 * (p1.y - p0.y);
    if (abs(a) >= 1e-7) {
        float ts = -b / (2.0 * a);
        if (ts > 1e-4 && ts < 1.0 - 1e-4) {
            vec2 p01 = mix(p0, p1, ts);
            vec2 p12 = mix(p1, p2, ts);
            vec2 mid = mix(p01, p12, ts);
            return mono_quad(p0, p01, mid, p) + mono_quad(mid, p12, p2, p);
        }
    }
    return mono_quad(p0, p1, p2, p);
}

vec2 cubic_at(vec2 p0, vec2 p1, vec2 p2, vec2 p3, float t) {
    float u = 1.0 - t;
    return u * u * u * p0 + 3.0 * u * u * t * p1 + 3.0 * u * t * t * p2 + t * t * t * p3;
}

float cubic_cross(vec2 p0, vec2 p1, vec2 p2, vec2 p3, vec2 p) {
    vec2 prev = p0;
    float w = 0.0;
    for (int s = 1; s <= 16; ++s) {
        float t = float(s) / 16.0;
        vec2 cur = cubic_at(p0, p1, p2, p3, t);
        w += line_cross(prev, cur, p);
        prev = cur;
    }
    return w;
}

float inside(vec2 em) {
    float w = 0.0;
    uint last = vRange.x + min(vRange.y, 4096u);
    for (uint i = vRange.x; i < last; ++i) {
        Curve c = curves[i];
        vec2 p0 = c.a.xy;
        vec2 p1 = c.a.zw;
        if (c.kind == 1) {
            w += line_cross(p0, p1, em);
        } else if (c.kind == 2) {
            w += quad_cross(p0, p1, c.b.xy, em);
        } else if (c.kind == 3) {
            w += cubic_cross(p0, p1, c.b.xy, c.b.zw, em);
        }
    }
    return abs(w) > 0.5 ? 1.0 : 0.0;
}

float srgb_to_linear(float u) {
    return (u <= 0.04045) ? u / 12.92 : pow((u + 0.055) / 1.055, 2.4);
}

float linear_to_srgb(float u) {
    return (u <= 0.0031308) ? u * 12.92 : 1.055 * pow(u, 1.0 / 2.4) - 0.055;
}

// Skia's A8 preblend at contrast 1 (`text/mask_gamma.h`), evaluated for this text color's green.
float warp(float raw, float green) {
    float src = clamp(green, 0.0, 1.0);
    float dst = 1.0 - src;
    float lin_src = srgb_to_linear(src);
    float lin_dst = srgb_to_linear(dst);
    float srca = raw + (1.0 - raw) * lin_dst * raw;
    srca = clamp(srca, 0.0, 1.0);
    if (abs(src - dst) < (1.0 / 256.0)) {
        return srca;
    }
    float lin_out = clamp(lin_src * srca + (1.0 - srca) * lin_dst, 0.0, 1.0);
    return clamp((linear_to_srgb(lin_out) - dst) / (src - dst), 0.0, 1.0);
}

void main() {
    vec2 p = zui_frag_xy;
    float clip_c = clip_cover(p);
    // Derivatives must run before the discard. A clip that kills some pixels of the quad
    // would otherwise leave dFdx undefined for the pixels that stay.
    vec2 dx = dFdx(vEm);
    vec2 dy = dFdy(vEm);
    if (clip_c <= 0.0 || vRange.y == 0u) {
        discard;
    }
    float cov = inside(vEm + dx * -0.25 + dy * -0.25);
    cov += inside(vEm + dx * 0.25 + dy * -0.25);
    cov += inside(vEm + dx * -0.25 + dy * 0.25);
    cov += inside(vEm + dx * 0.25 + dy * 0.25);
    cov *= 0.25;
    cov = warp(cov, vCol.g);
    if (cov <= 0.0) {
        discard;
    }
    o = vec4(vCol.rgb, vCol.a * cov * clip_c);
}
