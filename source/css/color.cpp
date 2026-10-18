#include "ascii.h"
#include "css/values.h"
#include "css/color_internal.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "css/values_internal.h"

namespace zui {
namespace css {

namespace values_detail {


int hex_digit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

std::string_view trim_sv(std::string_view s) { return ::zui::trim_sv(s); }

bool equals_ci_sv(std::string_view a, std::string_view b) { return ::zui::equals_ci(a, b); }

std::uint8_t clamp_u8(int v) {
    if (v < 0) {
        return 0;
    }
    if (v > 255) {
        return 255;
    }
    return static_cast<std::uint8_t>(v);
}

std::uint8_t clamp_u8f(float v) {
    return clamp_u8(static_cast<int>(v + (v >= 0.f ? 0.5f : -0.5f)));
}

float hue2rgb(float p, float q, float t) {
    if (t < 0.f) {
        t += 1.f;
    }
    if (t > 1.f) {
        t -= 1.f;
    }
    if (t < 1.f / 6.f) {
        return p + (q - p) * 6.f * t;
    }
    if (t < 1.f / 2.f) {
        return q;
    }
    if (t < 2.f / 3.f) {
        return p + (q - p) * (2.f / 3.f - t) * 6.f;
    }
    return p;
}

CssRgba hsl_to_rgba(float h_deg, float s, float l, float a) {
    s = std::max(0.f, std::min(1.f, s));
    l = std::max(0.f, std::min(1.f, l));
    float h = std::fmod(h_deg, 360.f);
    if (h < 0.f) {
        h += 360.f;
    }
    h /= 360.f;
    const float q = l < 0.5f ? l * (1.f + s) : l + s - l * s;
    const float p = 2.f * l - q;
    return CssRgba{clamp_u8f(hue2rgb(p, q, h + 1.f / 3.f) * 255.f),
                   clamp_u8f(hue2rgb(p, q, h) * 255.f),
                   clamp_u8f(hue2rgb(p, q, h - 1.f / 3.f) * 255.f), clamp_u8f(a)};
}


}  // namespace values_detail

using namespace values_detail;

std::optional<CssRgba> parse_modern_color(std::string_view s);

std::optional<CssRgba> parse_css_color(std::string_view css) {
    css = trim_sv(css);
    if (css.empty()) {
        return std::nullopt;
    }
    if (equals_ci_sv(css, "transparent") || equals_ci_sv(css, "none")) {
        return CssRgba{0, 0, 0, 0};
    }
    // currentcolor is resolved by the caller against the originating element.
    if (equals_ci_sv(css, "currentcolor")) {
        return std::nullopt;
    }
    // System colours (CSS Color 4). Chromium 150 on Windows: Highlight=#0078D7.
    if (equals_ci_sv(css, "highlight")) {
        return CssRgba{0x00, 0x78, 0xD7, 255};
    }
    if (equals_ci_sv(css, "highlighttext")) {
        return CssRgba{255, 255, 255, 255};
    }
    if (!css.empty() && css[0] == '#') {
        if (css.size() == 4) {
            const int r = hex_digit(css[1]);
            const int g = hex_digit(css[2]);
            const int b = hex_digit(css[3]);
            if (r < 0 || g < 0 || b < 0) {
                return std::nullopt;
            }
            return CssRgba{static_cast<std::uint8_t>(r * 17), static_cast<std::uint8_t>(g * 17),
                           static_cast<std::uint8_t>(b * 17), 255};
        }
        if (css.size() == 5) {
            const int r = hex_digit(css[1]);
            const int g = hex_digit(css[2]);
            const int b = hex_digit(css[3]);
            const int a = hex_digit(css[4]);
            if (r < 0 || g < 0 || b < 0 || a < 0) {
                return std::nullopt;
            }
            return CssRgba{static_cast<std::uint8_t>(r * 17), static_cast<std::uint8_t>(g * 17),
                           static_cast<std::uint8_t>(b * 17), static_cast<std::uint8_t>(a * 17)};
        }
        if (css.size() == 7 || css.size() == 9) {
            const int r1 = hex_digit(css[1]);
            const int r0 = hex_digit(css[2]);
            const int g1 = hex_digit(css[3]);
            const int g0 = hex_digit(css[4]);
            const int b1 = hex_digit(css[5]);
            const int b0 = hex_digit(css[6]);
            if (r1 < 0 || r0 < 0 || g1 < 0 || g0 < 0 || b1 < 0 || b0 < 0) {
                return std::nullopt;
            }
            std::uint8_t a = 255;
            if (css.size() == 9) {
                const int a1 = hex_digit(css[7]);
                const int a0 = hex_digit(css[8]);
                if (a1 < 0 || a0 < 0) {
                    return std::nullopt;
                }
                a = static_cast<std::uint8_t>((a1 << 4) | a0);
            }
            return CssRgba{static_cast<std::uint8_t>((r1 << 4) | r0),
                           static_cast<std::uint8_t>((g1 << 4) | g0),
                           static_cast<std::uint8_t>((b1 << 4) | b0), a};
        }
        return std::nullopt;
    }

    // CSS Color 4 named colours (extended + SVG). Sorted for binary search.
    static constexpr struct Named {
        const char* name;
        std::uint8_t r, g, b;
    } kNamed[] = {
        {"aliceblue", 240, 248, 255},
        {"antiquewhite", 250, 235, 215},
        {"aqua", 0, 255, 255},
        {"aquamarine", 127, 255, 212},
        {"azure", 240, 255, 255},
        {"beige", 245, 245, 220},
        {"bisque", 255, 228, 196},
        {"black", 0, 0, 0},
        {"blanchedalmond", 255, 235, 205},
        {"blue", 0, 0, 255},
        {"blueviolet", 138, 43, 226},
        {"brown", 165, 42, 42},
        {"burlywood", 222, 184, 135},
        {"cadetblue", 95, 158, 160},
        {"chartreuse", 127, 255, 0},
        {"chocolate", 210, 105, 30},
        {"coral", 255, 127, 80},
        {"cornflowerblue", 100, 149, 237},
        {"cornsilk", 255, 248, 220},
        {"crimson", 220, 20, 60},
        {"cyan", 0, 255, 255},
        {"darkblue", 0, 0, 139},
        {"darkcyan", 0, 139, 139},
        {"darkgoldenrod", 184, 134, 11},
        {"darkgray", 169, 169, 169},
        {"darkgreen", 0, 100, 0},
        {"darkgrey", 169, 169, 169},
        {"darkkhaki", 189, 183, 107},
        {"darkmagenta", 139, 0, 139},
        {"darkolivegreen", 85, 107, 47},
        {"darkorange", 255, 140, 0},
        {"darkorchid", 153, 50, 204},
        {"darkred", 139, 0, 0},
        {"darksalmon", 233, 150, 122},
        {"darkseagreen", 143, 188, 143},
        {"darkslateblue", 72, 61, 139},
        {"darkslategray", 47, 79, 79},
        {"darkslategrey", 47, 79, 79},
        {"darkturquoise", 0, 206, 209},
        {"darkviolet", 148, 0, 211},
        {"deeppink", 255, 20, 147},
        {"deepskyblue", 0, 191, 255},
        {"dimgray", 105, 105, 105},
        {"dimgrey", 105, 105, 105},
        {"dodgerblue", 30, 144, 255},
        {"firebrick", 178, 34, 34},
        {"floralwhite", 255, 250, 240},
        {"forestgreen", 34, 139, 34},
        {"fuchsia", 255, 0, 255},
        {"gainsboro", 220, 220, 220},
        {"ghostwhite", 248, 248, 255},
        {"gold", 255, 215, 0},
        {"goldenrod", 218, 165, 32},
        {"gray", 128, 128, 128},
        {"green", 0, 128, 0},
        {"greenyellow", 173, 255, 47},
        {"grey", 128, 128, 128},
        {"honeydew", 240, 255, 240},
        {"hotpink", 255, 105, 180},
        {"indianred", 205, 92, 92},
        {"indigo", 75, 0, 130},
        {"ivory", 255, 255, 240},
        {"khaki", 240, 230, 140},
        {"lavender", 230, 230, 250},
        {"lavenderblush", 255, 240, 245},
        {"lawngreen", 124, 252, 0},
        {"lemonchiffon", 255, 250, 205},
        {"lightblue", 173, 216, 230},
        {"lightcoral", 240, 128, 128},
        {"lightcyan", 224, 255, 255},
        {"lightgoldenrodyellow", 250, 250, 210},
        {"lightgray", 211, 211, 211},
        {"lightgreen", 144, 238, 144},
        {"lightgrey", 211, 211, 211},
        {"lightpink", 255, 182, 193},
        {"lightsalmon", 255, 160, 122},
        {"lightseagreen", 32, 178, 170},
        {"lightskyblue", 135, 206, 250},
        {"lightslategray", 119, 136, 153},
        {"lightslategrey", 119, 136, 153},
        {"lightsteelblue", 176, 196, 222},
        {"lightyellow", 255, 255, 224},
        {"lime", 0, 255, 0},
        {"limegreen", 50, 205, 50},
        {"linen", 250, 240, 230},
        {"magenta", 255, 0, 255},
        {"maroon", 128, 0, 0},
        {"mediumaquamarine", 102, 205, 170},
        {"mediumblue", 0, 0, 205},
        {"mediumorchid", 186, 85, 211},
        {"mediumpurple", 147, 112, 219},
        {"mediumseagreen", 60, 179, 113},
        {"mediumslateblue", 123, 104, 238},
        {"mediumspringgreen", 0, 250, 154},
        {"mediumturquoise", 72, 209, 204},
        {"mediumvioletred", 199, 21, 133},
        {"midnightblue", 25, 25, 112},
        {"mintcream", 245, 255, 250},
        {"mistyrose", 255, 228, 225},
        {"moccasin", 255, 228, 181},
        {"navajowhite", 255, 222, 173},
        {"navy", 0, 0, 128},
        {"oldlace", 253, 245, 230},
        {"olive", 128, 128, 0},
        {"olivedrab", 107, 142, 35},
        {"orange", 255, 165, 0},
        {"orangered", 255, 69, 0},
        {"orchid", 218, 112, 214},
        {"palegoldenrod", 238, 232, 170},
        {"palegreen", 152, 251, 152},
        {"paleturquoise", 175, 238, 238},
        {"palevioletred", 219, 112, 147},
        {"papayawhip", 255, 239, 213},
        {"peachpuff", 255, 218, 185},
        {"peru", 205, 133, 63},
        {"pink", 255, 192, 203},
        {"plum", 221, 160, 221},
        {"powderblue", 176, 224, 230},
        {"purple", 128, 0, 128},
        {"rebeccapurple", 102, 51, 153},
        {"red", 255, 0, 0},
        {"rosybrown", 188, 143, 143},
        {"royalblue", 65, 105, 225},
        {"saddlebrown", 139, 69, 19},
        {"salmon", 250, 128, 114},
        {"sandybrown", 244, 164, 96},
        {"seagreen", 46, 139, 87},
        {"seashell", 255, 245, 238},
        {"sienna", 160, 82, 45},
        {"silver", 192, 192, 192},
        {"skyblue", 135, 206, 235},
        {"slateblue", 106, 90, 205},
        {"slategray", 112, 128, 144},
        {"slategrey", 112, 128, 144},
        {"snow", 255, 250, 250},
        {"springgreen", 0, 255, 127},
        {"steelblue", 70, 130, 180},
        {"tan", 210, 180, 140},
        {"teal", 0, 128, 128},
        {"thistle", 216, 191, 216},
        {"tomato", 255, 99, 71},
        {"turquoise", 64, 224, 208},
        {"violet", 238, 130, 238},
        {"wheat", 245, 222, 179},
        {"white", 255, 255, 255},
        {"whitesmoke", 245, 245, 245},
        {"yellow", 255, 255, 0},
        {"yellowgreen", 154, 205, 50},
    };
    {
        const std::string key = lower_copy(css);
        const auto* begin = std::begin(kNamed);
        const auto* end = std::end(kNamed);
        const auto* it = std::lower_bound(begin, end, key, [](const Named& n, const std::string& k) {
            return std::string_view(n.name) < std::string_view(k);
        });
        if (it != end && it->name == key) {
            return CssRgba{it->r, it->g, it->b, 255};
        }
    }

    const std::string s = lower_copy(css);
    if (s.rfind("rgb", 0) == 0) {
        const auto lp = s.find('(');
        const auto rp = s.rfind(')');
        if (lp == std::string::npos || rp <= lp) {
            return std::nullopt;
        }
        const std::string inner = s.substr(lp + 1, rp - lp - 1);
        float vals[4] = {0, 0, 0, 255};
        int n = 0;
        size_t i = 0;
        while (i < inner.size() && n < 4) {
            while (i < inner.size() &&
                   (inner[i] == ',' || inner[i] == '/' ||
                    std::isspace(static_cast<unsigned char>(inner[i])))) {
                ++i;
            }
            if (i >= inner.size()) {
                break;
            }
            char* end = nullptr;
            const float f = std::strtof(inner.c_str() + static_cast<int>(i), &end);
            if (end == inner.c_str() + static_cast<int>(i)) {
                break;
            }
            size_t consumed = static_cast<size_t>(end - (inner.c_str() + static_cast<int>(i)));
            i += consumed;
            bool pct = false;
            if (i < inner.size() && inner[i] == '%') {
                pct = true;
                ++i;
            }
            if (n == 3) {
                vals[n++] = pct ? f / 100.f * 255.f : (f <= 1.f ? f * 255.f : f);
            } else if (pct) {
                vals[n++] = f / 100.f * 255.f;
            } else {
                vals[n++] = f;
            }
            while (i < inner.size() && inner[i] != ',' && inner[i] != '/') {
                if (!std::isspace(static_cast<unsigned char>(inner[i]))) {
                    break;
                }
                ++i;
            }
        }
        if (n < 3) {
            return std::nullopt;
        }
        // 0.14*255 is 35.7. Chrome stores 36 and blends with Skia's SrcOver, which is
        // what makes both the album topbar and the sidebar border match. Truncating to
        // 35 only agreed with the topbar under a different blend.
        return CssRgba{clamp_u8f(vals[0]), clamp_u8f(vals[1]), clamp_u8f(vals[2]),
                       n >= 4 ? clamp_u8f(vals[3]) : static_cast<std::uint8_t>(255)};
    }
    if (s.rfind("hsl", 0) == 0) {
        const auto lp = s.find('(');
        const auto rp = s.rfind(')');
        if (lp == std::string::npos || rp <= lp) {
            return std::nullopt;
        }
        const std::string inner = s.substr(lp + 1, rp - lp - 1);
        float vals[4] = {0, 0, 0, 255};
        int n = 0;
        size_t i = 0;
        while (i < inner.size() && n < 4) {
            while (i < inner.size() &&
                   (inner[i] == ',' || inner[i] == '/' ||
                    std::isspace(static_cast<unsigned char>(inner[i])))) {
                ++i;
            }
            if (i >= inner.size()) {
                break;
            }
            char* end = nullptr;
            float f = std::strtof(inner.c_str() + static_cast<int>(i), &end);
            if (end == inner.c_str() + static_cast<int>(i)) {
                break;
            }
            size_t consumed = static_cast<size_t>(end - (inner.c_str() + static_cast<int>(i)));
            i += consumed;
            if (i < inner.size() && inner[i] == '%') {
                ++i;
                if (n == 3) {
                    f = f / 100.f * 255.f;
                } else if (n > 0) {
                    f = f / 100.f;
                }
            } else if (n == 3 && f <= 1.f) {
                f *= 255.f;
            }
            vals[n++] = f;
            while (i < inner.size() && inner[i] != ',' && inner[i] != '/') {
                if (!std::isspace(static_cast<unsigned char>(inner[i]))) {
                    break;
                }
                ++i;
            }
        }
        if (n < 3) {
            return std::nullopt;
        }
        return hsl_to_rgba(vals[0], vals[1], vals[2], n >= 4 ? vals[3] : 255.f);
    }
    if (std::optional<CssRgba> modern = parse_modern_color(s)) {
        return modern;
    }
    return std::nullopt;
}

std::optional<CssRgba> parse_modern_color(std::string_view s) {
    if (!(modern_color::starts_fn(s, "color-mix") || modern_color::starts_fn(s, "oklch") ||
          modern_color::starts_fn(s, "oklab") || modern_color::starts_fn(s, "lch") ||
          modern_color::starts_fn(s, "lab") || modern_color::starts_fn(s, "hwb") ||
          modern_color::starts_fn(s, "color"))) {
        return std::nullopt;
    }
    const std::optional<modern_color::Abs> color = modern_color::parse_abs(s);
    if (!color) {
        return std::nullopt;
    }
    return modern_color::encode_srgb(*color);
}

}  // namespace css
}  // namespace zui
