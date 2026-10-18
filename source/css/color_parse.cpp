// CSS Color 4 function parsing (oklch/lab/hwb/color()).

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
using namespace values_detail;

namespace modern_color {

bool starts_fn(std::string_view s, std::string_view name) {
    return s.size() > name.size() + 1 && s.compare(0, name.size(), name) == 0 &&
           s[name.size()] == '(' && s.back() == ')';
}

std::string_view fn_inner(std::string_view s, std::string_view name) {
    return trim_sv(s.substr(name.size() + 1, s.size() - name.size() - 2));
}

std::vector<std::string_view> split_top_commas(std::string_view s) {
    std::vector<std::string_view> out;
    int depth = 0;
    size_t start = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '(') {
            ++depth;
        } else if (s[i] == ')' && depth > 0) {
            --depth;
        } else if (s[i] == ',' && depth == 0) {
            out.push_back(trim_sv(s.substr(start, i - start)));
            start = i + 1;
        }
    }
    out.push_back(trim_sv(s.substr(start)));
    return out;
}

struct Tok {
    float num = 0.f;
    bool none = false;
    bool pct = false;
    bool ok = false;
};

bool read_tok(std::string_view s, size_t& i, Tok& tok) {
    while (i < s.size() && (std::isspace(static_cast<unsigned char>(s[i])) || s[i] == ',' ||
                            s[i] == '/')) {
        ++i;
    }
    if (i >= s.size()) {
        return false;
    }
    if (s.compare(i, 4, "none") == 0 &&
        (i + 4 == s.size() || !std::isalnum(static_cast<unsigned char>(s[i + 4])))) {
        tok.none = true;
        tok.ok = true;
        i += 4;
        return true;
    }
    char* end = nullptr;
    const std::string tmp(s.substr(i));
    const float n = std::strtof(tmp.c_str(), &end);
    if (end == tmp.c_str()) {
        return false;
    }
    i += static_cast<size_t>(end - tmp.c_str());
    tok.num = n;
    tok.ok = true;
    if (i < s.size() && s[i] == '%') {
        tok.pct = true;
        ++i;
    } else if (s.compare(i, 3, "deg") == 0) {
        i += 3;
    } else if (s.compare(i, 3, "rad") == 0) {
        tok.num = n * 180.f / kPi;
        i += 3;
    } else if (s.compare(i, 4, "grad") == 0) {
        tok.num = n * 0.9f;
        i += 4;
    } else if (s.compare(i, 4, "turn") == 0) {
        tok.num = n * 360.f;
        i += 4;
    }
    return true;
}

float chan_or_none(const Tok& tok, float pct_scale, float number_scale) {
    if (!tok.ok || tok.none) {
        return nan_v();
    }
    return tok.pct ? tok.num / 100.f * pct_scale : tok.num * number_scale;
}

bool read_components(std::string_view inner, Tok out[4], int& n, bool& alpha) {
    n = 0;
    alpha = false;
    size_t i = 0;
    bool saw_slash = false;
    while (i < inner.size() && n < 4) {
        size_t j = i;
        while (j < inner.size() && std::isspace(static_cast<unsigned char>(inner[j]))) {
            ++j;
        }
        if (j < inner.size() && inner[j] == '/') {
            if (saw_slash || n == 0) {
                return false;
            }
            saw_slash = true;
            i = j + 1;
        }
        Tok tok;
        const size_t before = i;
        if (!read_tok(inner, i, tok)) {
            return i == before && n >= 3;
        }
        if (saw_slash) {
            out[3] = tok;
            alpha = true;
            n = std::max(n, 3);
            break;
        }
        out[n++] = tok;
    }
    return n >= 3;
}

Abs from_srgb_bytes(const CssRgba& c) {
    Abs a;
    a.space = Space::Srgb;
    a.c[0] = static_cast<float>(c.r) / 255.f;
    a.c[1] = static_cast<float>(c.g) / 255.f;
    a.c[2] = static_cast<float>(c.b) / 255.f;
    a.a = static_cast<float>(c.a) / 255.f;
    return a;
}


std::optional<Abs> parse_fn_color(std::string_view s) {
    std::string_view inner;
    Space space = Space::Srgb;
    float pct_scale[3] = {1.f, 1.f, 1.f};
    float num_scale[3] = {1.f, 1.f, 1.f};
    bool hue2 = false;
    if (starts_fn(s, "oklch")) {
        inner = fn_inner(s, "oklch");
        space = Space::Oklch;
        pct_scale[0] = 1.f;
        pct_scale[1] = 0.4f;
        pct_scale[2] = 360.f;
        num_scale[2] = 1.f;
        hue2 = true;
    } else if (starts_fn(s, "oklab")) {
        inner = fn_inner(s, "oklab");
        space = Space::Oklab;
        pct_scale[1] = pct_scale[2] = 0.4f;
    } else if (starts_fn(s, "lch")) {
        inner = fn_inner(s, "lch");
        space = Space::Lch;
        pct_scale[0] = 100.f;
        num_scale[0] = 1.f;
        pct_scale[1] = 150.f;
        pct_scale[2] = 360.f;
        hue2 = true;
    } else if (starts_fn(s, "lab")) {
        inner = fn_inner(s, "lab");
        space = Space::Lab;
        pct_scale[0] = 100.f;
        pct_scale[1] = pct_scale[2] = 125.f;
    } else if (starts_fn(s, "hwb")) {
        inner = fn_inner(s, "hwb");
        space = Space::Hwb;
        pct_scale[0] = 360.f;
        pct_scale[1] = pct_scale[2] = 1.f;
        num_scale[1] = num_scale[2] = 1.f;
        hue2 = false;
    } else if (starts_fn(s, "color")) {
        inner = fn_inner(s, "color");
        size_t i = 0;
        while (i < inner.size() && !std::isspace(static_cast<unsigned char>(inner[i])) &&
               inner[i] != ',') {
            ++i;
        }
        const std::string_view name = inner.substr(0, i);
        if (name == "srgb") {
            space = Space::Srgb;
        } else if (name == "srgb-linear") {
            space = Space::SrgbLinear;
        } else if (name == "display-p3") {
            space = Space::DisplayP3;
        } else if (name == "xyz" || name == "xyz-d65") {
            space = Space::XyzD65;
        } else if (name == "xyz-d50") {
            space = Space::XyzD50;
        } else {
            return std::nullopt;
        }
        inner = trim_sv(inner.substr(i));
    } else if (starts_fn(s, "color-mix")) {
        return mix_colors(fn_inner(s, "color-mix"));
    } else {
        return std::nullopt;
    }
    Tok tok[4];
    int n = 0;
    bool alpha = false;
    if (!read_components(inner, tok, n, alpha)) {
        return std::nullopt;
    }
    Abs color;
    color.space = space;
    for (int i = 0; i < 3; ++i) {
        float scale_pct = pct_scale[i];
        float scale_num = num_scale[i];
        if (hue2 && i == 2 && tok[i].pct) {
            scale_pct = 360.f;
        }
        if (space == Space::Hwb && i > 0 && !tok[i].pct && !tok[i].none) {
            // Unitless whiteness/blackness is a fraction. A percentage is 0..100.
            scale_num = 1.f;
        }
        if (space == Space::Hwb && i == 0 && !tok[i].pct) {
            scale_num = 1.f;
            scale_pct = 360.f;
        }
        color.c[i] = chan_or_none(tok[i], scale_pct, scale_num);
        color.none[i] = tok[i].none;
    }
    if (alpha) {
        color.a = tok[3].none ? nan_v()
                              : (tok[3].pct ? tok[3].num / 100.f
                                            : (tok[3].num <= 1.f ? tok[3].num : tok[3].num / 255.f));
    }
    return color;
}

std::optional<Abs> parse_abs(std::string_view s) {
    if (starts_fn(s, "color-mix") || starts_fn(s, "oklch") || starts_fn(s, "oklab") ||
        starts_fn(s, "lch") || starts_fn(s, "lab") || starts_fn(s, "hwb") || starts_fn(s, "color")) {
        return parse_fn_color(s);
    }
    if (std::optional<CssRgba> legacy = parse_css_color(s)) {
        return from_srgb_bytes(*legacy);
    }
    return std::nullopt;
}

}  // namespace modern_color

}  // namespace css
}  // namespace zui
