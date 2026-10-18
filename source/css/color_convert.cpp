// CSS Color 4 space conversions and sRGB encoding.

#include "ascii.h"
#include "css/values.h"
#include "css/color_internal.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#include "css/values_internal.h"

namespace zui {
namespace css {
using namespace values_detail;

namespace modern_color {

float nan_v() { return std::numeric_limits<float>::quiet_NaN(); }
bool missing(float v) { return std::isnan(v); }

void mul3(const float m[3][3], const float v[3], float out[3]) {
    for (int r = 0; r < 3; ++r) {
        out[r] = m[r][0] * v[0] + m[r][1] * v[1] + m[r][2] * v[2];
    }
}

float lin_channel(float v) {
    const float sign = v < 0.f ? -1.f : 1.f;
    const float abs = std::fabs(v);
    if (abs <= 0.04045f) {
        return v / 12.92f;
    }
    return sign * std::pow((abs + 0.055f) / 1.055f, 2.4f);
}

float gam_channel(float v) {
    const float sign = v < 0.f ? -1.f : 1.f;
    const float abs = std::fabs(v);
    if (abs > 0.0031308f) {
        return sign * (1.055f * std::pow(abs, 1.f / 2.4f) - 0.055f);
    }
    return 12.92f * v;
}

void lin_srgb(const float rgb[3], float out[3]) {
    for (int i = 0; i < 3; ++i) {
        out[i] = lin_channel(rgb[i]);
    }
}

void gam_srgb(const float rgb[3], float out[3]) {
    for (int i = 0; i < 3; ++i) {
        out[i] = gam_channel(rgb[i]);
    }
}

void lin_srgb_to_xyz(const float rgb[3], float out[3]) {
    const float m[3][3] = {
        {506752.f / 1228815.f, 87881.f / 245763.f, 12673.f / 70218.f},
        {87098.f / 409605.f, 175762.f / 245763.f, 12673.f / 175545.f},
        {7918.f / 409605.f, 87881.f / 737289.f, 1001167.f / 1053270.f},
    };
    mul3(m, rgb, out);
}

void xyz_to_lin_srgb(const float xyz[3], float out[3]) {
    const float m[3][3] = {
        {12831.f / 3959.f, -329.f / 214.f, -1974.f / 3959.f},
        {-851781.f / 878810.f, 1648619.f / 878810.f, 36519.f / 878810.f},
        {705.f / 12673.f, -2585.f / 12673.f, 705.f / 667.f},
    };
    mul3(m, xyz, out);
}

void lin_p3_to_xyz(const float rgb[3], float out[3]) {
    const float m[3][3] = {
        {608311.f / 1250200.f, 189793.f / 714400.f, 198249.f / 1000160.f},
        {35783.f / 156275.f, 247089.f / 357200.f, 198249.f / 2500400.f},
        {0.f, 32229.f / 714400.f, 5220557.f / 5000800.f},
    };
    mul3(m, rgb, out);
}

void xyz_to_lin_p3(const float xyz[3], float out[3]) {
    const float m[3][3] = {
        {446124.f / 178915.f, -333277.f / 357830.f, -72051.f / 178915.f},
        {-14852.f / 17905.f, 63121.f / 35810.f, 423.f / 17905.f},
        {11844.f / 330415.f, -50337.f / 660830.f, 316169.f / 330415.f},
    };
    mul3(m, xyz, out);
}

void d65_to_d50(const float xyz[3], float out[3]) {
    const float m[3][3] = {
        {1.0479297925449969f, 0.022946870601609652f, -0.05019226628920524f},
        {0.02962780877005599f, 0.9904344267538799f, -0.017073799063418826f},
        {-0.009243040646204504f, 0.015055191490298152f, 0.7518742814281371f},
    };
    mul3(m, xyz, out);
}

void d50_to_d65(const float xyz[3], float out[3]) {
    const float m[3][3] = {
        {0.955473421488075f, -0.02309845494876471f, 0.06325924320057072f},
        {-0.0283697093338637f, 1.0099953980813041f, 0.021041441191917323f},
        {0.012314014864481998f, -0.020507649298898964f, 1.330365926242124f},
    };
    mul3(m, xyz, out);
}

void d50_white(float out[3]) {
    out[0] = 0.3457f / 0.3585f;
    out[1] = 1.f;
    out[2] = (1.f - 0.3457f - 0.3585f) / 0.3585f;
}

void xyz_d50_to_lab(const float xyz[3], float out[3]) {
    const float eps = 216.f / 24389.f;
    const float kappa = 24389.f / 27.f;
    float white[3];
    d50_white(white);
    float f[3];
    for (int i = 0; i < 3; ++i) {
        const float t = xyz[i] / white[i];
        f[i] = t > eps ? std::cbrt(t) : (kappa * t + 16.f) / 116.f;
    }
    out[0] = 116.f * f[1] - 16.f;
    out[1] = 500.f * (f[0] - f[1]);
    out[2] = 200.f * (f[1] - f[2]);
}

void lab_to_xyz_d50(const float lab[3], float out[3]) {
    const float kappa = 24389.f / 27.f;
    const float eps = 216.f / 24389.f;
    const float fy = (lab[0] + 16.f) / 116.f;
    const float fx = lab[1] / 500.f + fy;
    const float fz = fy - lab[2] / 200.f;
    const float xyz_f[3] = {
        std::pow(fx, 3.f) > eps ? std::pow(fx, 3.f) : (116.f * fx - 16.f) / kappa,
        lab[0] > kappa * eps ? std::pow((lab[0] + 16.f) / 116.f, 3.f) : lab[0] / kappa,
        std::pow(fz, 3.f) > eps ? std::pow(fz, 3.f) : (116.f * fz - 16.f) / kappa,
    };
    float white[3];
    d50_white(white);
    for (int i = 0; i < 3; ++i) {
        out[i] = xyz_f[i] * white[i];
    }
}

void xyz_d65_to_oklab(const float xyz[3], float out[3]) {
    const float to_lms[3][3] = {
        {0.8190224379967030f, 0.3619062600528904f, -0.1288737815209879f},
        {0.0329836539323885f, 0.9292868615863434f, 0.0361446663506424f},
        {0.0481771893596242f, 0.2642395317527308f, 0.6335478284694309f},
    };
    const float to_lab[3][3] = {
        {0.2104542683093140f, 0.7936177747023054f, -0.0040720430116193f},
        {1.9779985324311684f, -2.4285922420485799f, 0.4505937096174110f},
        {0.0259040424655478f, 0.7827717124575296f, -0.8086757549230774f},
    };
    float lms[3];
    mul3(to_lms, xyz, lms);
    for (float& c : lms) {
        c = std::cbrt(c);
    }
    mul3(to_lab, lms, out);
}

void oklab_to_xyz_d65(const float lab[3], float out[3]) {
    const float to_lms[3][3] = {
        {1.f, 0.3963377773761749f, 0.2158037573099136f},
        {1.f, -0.1055613458156586f, -0.0638541728258133f},
        {1.f, -0.0894841775298119f, -1.2914855480194092f},
    };
    const float to_xyz[3][3] = {
        {1.2268798758459243f, -0.5578149944602171f, 0.2813910456659647f},
        {-0.0405757452148008f, 1.1122868032803170f, -0.0717110580655164f},
        {-0.0763729366746601f, -0.4214933324022432f, 1.5869240198367816f},
    };
    float lms[3];
    mul3(to_lms, lab, lms);
    for (float& c : lms) {
        c = c * c * c;
    }
    mul3(to_xyz, lms, out);
}

void rect_to_polar(const float lab[3], float out[3], float hue_eps) {
    out[0] = lab[0];
    out[1] = std::sqrt(lab[1] * lab[1] + lab[2] * lab[2]);
    float hue = std::atan2(lab[2], lab[1]) * 180.f / kPi;
    if (hue < 0.f) {
        hue += 360.f;
    }
    out[2] = out[1] <= hue_eps ? nan_v() : hue;
}

void polar_to_rect(const float lch[3], float out[3]) {
    const float hue = missing(lch[2]) ? 0.f : lch[2];
    const float rad = hue * kPi / 180.f;
    out[0] = lch[0];
    out[1] = lch[1] * std::cos(rad);
    out[2] = lch[1] * std::sin(rad);
}

void hsl_to_srgb(float h_deg, float s, float l, float out[3]) {
    s = std::max(0.f, std::min(1.f, s));
    l = std::max(0.f, std::min(1.f, l));
    float h = std::fmod(missing(h_deg) ? 0.f : h_deg, 360.f);
    if (h < 0.f) {
        h += 360.f;
    }
    h /= 360.f;
    const float q = l < 0.5f ? l * (1.f + s) : l + s - l * s;
    const float p = 2.f * l - q;
    out[0] = hue2rgb(p, q, h + 1.f / 3.f);
    out[1] = hue2rgb(p, q, h);
    out[2] = hue2rgb(p, q, h - 1.f / 3.f);
}

void srgb_to_hsl(const float rgb[3], float out[3]) {
    const float r = rgb[0];
    const float g = rgb[1];
    const float b = rgb[2];
    const float max = std::max(r, std::max(g, b));
    const float min = std::min(r, std::min(g, b));
    const float l = (max + min) * 0.5f;
    float h = nan_v();
    float s = 0.f;
    if (max != min) {
        const float d = max - min;
        s = l > 0.5f ? d / (2.f - max - min) : d / (max + min);
        if (max == r) {
            h = (g - b) / d + (g < b ? 6.f : 0.f);
        } else if (max == g) {
            h = (b - r) / d + 2.f;
        } else {
            h = (r - g) / d + 4.f;
        }
        h *= 60.f;
    }
    out[0] = h;
    out[1] = s;
    out[2] = l;
}

void hwb_to_srgb(float h, float w, float b, float out[3]) {
    if (w + b >= 1.f) {
        const float g = w / std::max(w + b, 0.0001f);
        out[0] = out[1] = out[2] = g;
        return;
    }
    float pure[3];
    hsl_to_srgb(h, 1.f, 0.5f, pure);
    const float scale = 1.f - w - b;
    for (int i = 0; i < 3; ++i) {
        out[i] = pure[i] * scale + w;
    }
}


bool in_srgb_gamut(const float lin[3]) {
    constexpr float eps = 1e-4f;
    for (int i = 0; i < 3; ++i) {
        if (lin[i] < -eps || lin[i] > 1.f + eps) {
            return false;
        }
    }
    return true;
}

void abs_to_lin_srgb(const Abs& color, float out[3]) {
    float tmp[3];
    float xyz[3];
    switch (color.space) {
    case Space::Srgb:
    case Space::Hsl:
    case Space::Hwb: {
        float rgb[3];
        if (color.space == Space::Hsl) {
            hsl_to_srgb(color.c[0], color.c[1], color.c[2], rgb);
        } else if (color.space == Space::Hwb) {
            hwb_to_srgb(color.c[0], color.c[1], color.c[2], rgb);
        } else {
            for (int i = 0; i < 3; ++i) {
                rgb[i] = missing(color.c[i]) ? 0.f : color.c[i];
            }
        }
        lin_srgb(rgb, out);
        return;
    }
    case Space::SrgbLinear:
        for (int i = 0; i < 3; ++i) {
            out[i] = missing(color.c[i]) ? 0.f : color.c[i];
        }
        return;
    case Space::DisplayP3:
        lin_srgb(color.c, tmp);
        lin_p3_to_xyz(tmp, xyz);
        xyz_to_lin_srgb(xyz, out);
        return;
    case Space::XyzD65:
        xyz_to_lin_srgb(color.c, out);
        return;
    case Space::XyzD50:
        d50_to_d65(color.c, xyz);
        xyz_to_lin_srgb(xyz, out);
        return;
    case Space::Lab:
        lab_to_xyz_d50(color.c, tmp);
        d50_to_d65(tmp, xyz);
        xyz_to_lin_srgb(xyz, out);
        return;
    case Space::Lch: {
        float lab[3];
        polar_to_rect(color.c, lab);
        lab_to_xyz_d50(lab, tmp);
        d50_to_d65(tmp, xyz);
        xyz_to_lin_srgb(xyz, out);
        return;
    }
    case Space::Oklab:
        oklab_to_xyz_d65(color.c, xyz);
        xyz_to_lin_srgb(xyz, out);
        return;
    case Space::Oklch: {
        float lab[3];
        polar_to_rect(color.c, lab);
        oklab_to_xyz_d65(lab, xyz);
        xyz_to_lin_srgb(xyz, out);
        return;
    }
    }
}

// CSS Color 4 gamut mapping is constant-lightness chroma reduction in Oklch. The binary
// search is the algorithm the spec lists first; in-gamut colors skip it.
void gamut_map_oklch(float oklch[3]) {
    if (oklch[0] >= 1.f) {
        oklch[0] = 1.f;
        oklch[1] = 0.f;
        oklch[2] = nan_v();
        return;
    }
    if (oklch[0] <= 0.f) {
        oklch[0] = 0.f;
        oklch[1] = 0.f;
        oklch[2] = nan_v();
        return;
    }
    Abs probe;
    probe.space = Space::Oklch;
    probe.c[0] = oklch[0];
    probe.c[1] = oklch[1];
    probe.c[2] = oklch[2];
    float lin[3];
    abs_to_lin_srgb(probe, lin);
    if (in_srgb_gamut(lin)) {
        return;
    }
    float lo = 0.f;
    float hi = std::max(0.f, oklch[1]);
    for (int i = 0; i < 16; ++i) {
        const float mid = (lo + hi) * 0.5f;
        probe.c[1] = mid;
        abs_to_lin_srgb(probe, lin);
        if (in_srgb_gamut(lin)) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    oklch[1] = lo;
}

void clip01(float rgb[3]) {
    for (int i = 0; i < 3; ++i) {
        rgb[i] = std::max(0.f, std::min(1.f, rgb[i]));
    }
}

CssRgba bytes_from_srgb(const float rgb[3], float alpha) {
    const float a = std::max(0.f, std::min(1.f, missing(alpha) ? 1.f : alpha));
    return CssRgba{clamp_u8f(rgb[0] * 255.f), clamp_u8f(rgb[1] * 255.f), clamp_u8f(rgb[2] * 255.f),
                   clamp_u8f(a * 255.f)};
}

// In-gamut sRGB is encoded directly. Out-of-gamut colors lose chroma in Oklch first, so a
// display-p3 or oklch color that sRGB cannot hold does not clip one channel to white.
CssRgba encode_srgb(const Abs& color) {
    float rgb[3];
    if (color.space == Space::Srgb || color.space == Space::Hsl || color.space == Space::Hwb) {
        if (color.space == Space::Hsl) {
            hsl_to_srgb(color.c[0], color.c[1], color.c[2], rgb);
        } else if (color.space == Space::Hwb) {
            hwb_to_srgb(color.c[0], color.c[1], color.c[2], rgb);
        } else {
            for (int i = 0; i < 3; ++i) {
                rgb[i] = missing(color.c[i]) ? 0.f : color.c[i];
            }
        }
        clip01(rgb);
        return bytes_from_srgb(rgb, color.a);
    }
    float lin[3];
    abs_to_lin_srgb(color, lin);
    if (!in_srgb_gamut(lin)) {
        float xyz[3];
        lin_srgb_to_xyz(lin, xyz);
        float lab[3];
        xyz_d65_to_oklab(xyz, lab);
        float oklch[3];
        rect_to_polar(lab, oklch, 0.000004f);
        gamut_map_oklch(oklch);
        float mapped[3];
        polar_to_rect(oklch, mapped);
        oklab_to_xyz_d65(mapped, xyz);
        xyz_to_lin_srgb(xyz, lin);
    }
    clip01(lin);
    gam_srgb(lin, rgb);
    clip01(rgb);
    return bytes_from_srgb(rgb, color.a);
}

Abs to_space(const Abs& src, Space dest) {
    if (src.space == dest) {
        return src;
    }
    float lin[3];
    abs_to_lin_srgb(src, lin);
    Abs out;
    out.space = dest;
    out.a = src.a;
    float xyz[3];
    float rgb[3];
    switch (dest) {
    case Space::Srgb:
        gam_srgb(lin, out.c);
        break;
    case Space::SrgbLinear:
        for (int i = 0; i < 3; ++i) {
            out.c[i] = lin[i];
        }
        break;
    case Space::DisplayP3:
        lin_srgb_to_xyz(lin, xyz);
        xyz_to_lin_p3(xyz, rgb);
        gam_srgb(rgb, out.c);
        break;
    case Space::XyzD65:
        lin_srgb_to_xyz(lin, out.c);
        break;
    case Space::XyzD50:
        lin_srgb_to_xyz(lin, xyz);
        d65_to_d50(xyz, out.c);
        break;
    case Space::Lab:
        lin_srgb_to_xyz(lin, xyz);
        d65_to_d50(xyz, rgb);
        xyz_d50_to_lab(rgb, out.c);
        break;
    case Space::Lch: {
        lin_srgb_to_xyz(lin, xyz);
        d65_to_d50(xyz, rgb);
        float lab[3];
        xyz_d50_to_lab(rgb, lab);
        rect_to_polar(lab, out.c, 0.0015f);
        break;
    }
    case Space::Oklab:
        lin_srgb_to_xyz(lin, xyz);
        xyz_d65_to_oklab(xyz, out.c);
        break;
    case Space::Oklch: {
        lin_srgb_to_xyz(lin, xyz);
        float lab[3];
        xyz_d65_to_oklab(xyz, lab);
        rect_to_polar(lab, out.c, 0.000004f);
        break;
    }
    case Space::Hsl:
        gam_srgb(lin, rgb);
        clip01(rgb);
        srgb_to_hsl(rgb, out.c);
        break;
    case Space::Hwb: {
        gam_srgb(lin, rgb);
        clip01(rgb);
        float hsl[3];
        srgb_to_hsl(rgb, hsl);
        out.c[0] = hsl[0];
        out.c[1] = std::min(rgb[0], std::min(rgb[1], rgb[2]));
        out.c[2] = 1.f - std::max(rgb[0], std::max(rgb[1], rgb[2]));
        break;
    }
    }
    return out;
}

}  // namespace modern_color

}  // namespace css
}  // namespace zui
