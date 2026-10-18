// <color> parsing and interpolation for the transition value model.

#include "css/transition/transition.h"
#include "css/transition/internal.h"

#include "css/values.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace zui {
namespace css {

using namespace transition_detail;

namespace transition_detail {

int hexv(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return 0;
}

float cl255(float v) { return std::max(0.0f, std::min(255.0f, v)); }

/// CSS Color 4: hsl/hsla → sRGB. H in degrees; S/L in percent or [0,1]; A like rgb().
Rgba rgba_from_hsl(float h_deg, float s, float l, float a) {
    s = std::max(0.0f, std::min(1.0f, s));
    l = std::max(0.0f, std::min(1.0f, l));
    a = cl255(a);
    if (s <= 1e-6f) {
        const float g = cl255(l * 255.0f);
        return Rgba{g, g, g, a};
    }
    auto hue2rgb = [](float p, float q, float t) {
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
    };
    float h = std::fmod(h_deg, 360.f);
    if (h < 0.f) {
        h += 360.f;
    }
    h /= 360.f;
    const float q = l < 0.5f ? l * (1.f + s) : l + s - l * s;
    const float p = 2.f * l - q;
    return Rgba{cl255(hue2rgb(p, q, h + 1.f / 3.f) * 255.f),
                cl255(hue2rgb(p, q, h) * 255.f),
                cl255(hue2rgb(p, q, h - 1.f / 3.f) * 255.f), a};
}

Rgba parse_rgba(std::string_view css) {
    css = trim(css);
    if (css.empty() || css == "transparent" || css == "none") {
        return Rgba{0, 0, 0, 0};
    }
    if (!css.empty() && css[0] == '#') {
        // #RGB
        if (css.size() == 4) {
            const int r = hexv(css[1]);
            const int g = hexv(css[2]);
            const int b = hexv(css[3]);
            return Rgba{static_cast<float>(r * 17), static_cast<float>(g * 17),
                        static_cast<float>(b * 17), 255};
        }
        // #RGBA
        if (css.size() == 5) {
            const int r = hexv(css[1]);
            const int g = hexv(css[2]);
            const int b = hexv(css[3]);
            const int a = hexv(css[4]);
            return Rgba{static_cast<float>(r * 17), static_cast<float>(g * 17),
                        static_cast<float>(b * 17), static_cast<float>(a * 17)};
        }
        // #RRGGBB
        if (css.size() == 7) {
            return Rgba{static_cast<float>((hexv(css[1]) << 4) | hexv(css[2])),
                        static_cast<float>((hexv(css[3]) << 4) | hexv(css[4])),
                        static_cast<float>((hexv(css[5]) << 4) | hexv(css[6])), 255};
        }
        // #RRGGBBAA
        if (css.size() >= 9) {
            return Rgba{static_cast<float>((hexv(css[1]) << 4) | hexv(css[2])),
                        static_cast<float>((hexv(css[3]) << 4) | hexv(css[4])),
                        static_cast<float>((hexv(css[5]) << 4) | hexv(css[6])),
                        static_cast<float>((hexv(css[7]) << 4) | hexv(css[8]))};
        }
    }
    std::string s = lower(css);
    if (s == "white") {
        return Rgba{255, 255, 255, 255};
    }
    if (s == "black") {
        return Rgba{0, 0, 0, 255};
    }
    if (s == "red") {
        return Rgba{255, 0, 0, 255};
    }
    if (s.rfind("rgb", 0) == 0) {
        auto lp = s.find('(');
        auto rp = s.rfind(')');
        if (lp != std::string::npos && rp > lp) {
            std::string inner = s.substr(lp + 1, rp - lp - 1);
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
                if (n == 3 && f <= 1.0f) {
                    vals[n++] = f * 255.0f;
                } else {
                    vals[n++] = f;
                }
                while (i < inner.size() && inner[i] != ',' && inner[i] != '/') {
                    ++i;
                }
            }
            return Rgba{cl255(vals[0]), cl255(vals[1]), cl255(vals[2]),
                        n >= 4 ? cl255(vals[3]) : 255.0f};
        }
    }
    if (s.rfind("hsl", 0) == 0) {
        auto lp = s.find('(');
        auto rp = s.rfind(')');
        if (lp != std::string::npos && rp > lp) {
            std::string inner = s.substr(lp + 1, rp - lp - 1);
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
                size_t consumed = static_cast<size_t>(end - (inner.c_str() + static_cast<int>(i)));
                i += consumed;
                // S/L: percent → [0,1]. Alpha: percent or [0,1] → 0..255.
                if (i < inner.size() && inner[i] == '%') {
                    ++i;
                    if (n == 0) {
                        // hue with % is non-standard; treat as raw degrees
                    } else if (n == 3) {
                        f = f / 100.f * 255.f;
                    } else {
                        f = f / 100.f;
                    }
                } else if (n == 3 && f <= 1.0f) {
                    f *= 255.0f;
                } else if ((n == 1 || n == 2) && f > 1.0f) {
                    // unitless S/L above 1: treat as percent leftovers
                    f = f / 100.f;
                }
                vals[n++] = f;
                while (i < inner.size() && inner[i] != ',' && inner[i] != '/') {
                    ++i;
                }
            }
            if (n >= 3) {
                return rgba_from_hsl(vals[0], vals[1], vals[2], n >= 4 ? vals[3] : 255.f);
            }
        }
    }
    return Rgba{0, 0, 0, 0};
}

Rgba lerp_rgba(Rgba a, Rgba b, float t) {
    t = std::max(0.0f, std::min(1.0f, t));
    return Rgba{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

bool rgba_eq(Rgba a, Rgba b) {
    return std::fabs(a.r - b.r) < 0.5f && std::fabs(a.g - b.g) < 0.5f && std::fabs(a.b - b.b) < 0.5f &&
           std::fabs(a.a - b.a) < 0.5f;
}

Rgba color_from(const StyleMap* m, const html::Node* node, const char* key) {
    if (const std::string* v = map_get(m, key)) {
        if (lower(trim(*v)) == "currentcolor") {
            if (const std::string* c = map_get(m, "color")) {
                return parse_rgba(*c);
            }
            if (const std::string* c = node_get(node, "color")) {
                return parse_rgba(*c);
            }
            return Rgba{0, 0, 0, 255};
        }
        return parse_rgba(*v);
    }
    if (key == std::string("background-color")) {
        return Rgba{0, 0, 0, 0};
    }
    if (key == std::string("border-color")) {
        if (const std::string* c = map_get(m, "color")) {
            return parse_rgba(*c);
        }
        if (const std::string* c = node_get(node, "color")) {
            return parse_rgba(*c);
        }
        return Rgba{0, 0, 0, 255};
    }
    if (const std::string* c = map_get(m, "color")) {
        return parse_rgba(*c);
    }
    if (const std::string* c = node_get(node, "color")) {
        return parse_rgba(*c);
    }
    return Rgba{0, 0, 0, 255};
}

}  // namespace transition_detail

}  // namespace css
}  // namespace zui
