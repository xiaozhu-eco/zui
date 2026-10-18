// CSS Color 5 color-mix().

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

float hue_towards(float h1, float h2, HueMethod method) {
    if (missing(h1) || missing(h2)) {
        return missing(h2) ? h1 : h2;
    }
    const float d = h2 - h1;
    switch (method) {
    case HueMethod::Shorter:
        if (d > 180.f) {
            return h2 - 360.f;
        }
        if (d < -180.f) {
            return h2 + 360.f;
        }
        return h2;
    case HueMethod::Longer:
        if (d > 0.f && d < 180.f) {
            return h2 - 360.f;
        }
        if (d > -180.f && d <= 0.f) {
            return h2 + 360.f;
        }
        return h2;
    case HueMethod::Increasing:
        return h2 < h1 ? h2 + 360.f : h2;
    case HueMethod::Decreasing:
        return h2 > h1 ? h2 - 360.f : h2;
    }
    return h2;
}

bool parse_method(std::string_view text, Space& space, HueMethod& hue) {
    text = trim_sv(text);
    if (text.size() < 4 || text.compare(0, 3, "in ") != 0) {
        return false;
    }
    size_t i = 2;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
        ++i;
    }
    const size_t begin = i;
    while (i < text.size() && (std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '-')) {
        ++i;
    }
    const std::string_view name = text.substr(begin, i - begin);
    if (name == "srgb") {
        space = Space::Srgb;
    } else if (name == "srgb-linear") {
        space = Space::SrgbLinear;
    } else if (name == "display-p3") {
        space = Space::DisplayP3;
    } else if (name == "lab") {
        space = Space::Lab;
    } else if (name == "lch") {
        space = Space::Lch;
    } else if (name == "oklab") {
        space = Space::Oklab;
    } else if (name == "oklch") {
        space = Space::Oklch;
    } else if (name == "hsl") {
        space = Space::Hsl;
    } else if (name == "hwb") {
        space = Space::Hwb;
    } else if (name == "xyz" || name == "xyz-d65") {
        space = Space::XyzD65;
    } else if (name == "xyz-d50") {
        space = Space::XyzD50;
    } else {
        return false;
    }
    hue = HueMethod::Shorter;
    text = trim_sv(text.substr(i));
    if (text.empty()) {
        return true;
    }
    if (!polar_space(space)) {
        return false;
    }
    if (text == "shorter hue") {
        hue = HueMethod::Shorter;
    } else if (text == "longer hue") {
        hue = HueMethod::Longer;
    } else if (text == "increasing hue") {
        hue = HueMethod::Increasing;
    } else if (text == "decreasing hue") {
        hue = HueMethod::Decreasing;
    } else {
        return false;
    }
    return true;
}

bool split_pct(std::string_view arg, std::string_view& color, bool& has_pct, float& pct) {
    int depth = 0;
    size_t pct_at = std::string_view::npos;
    for (size_t i = 0; i < arg.size(); ++i) {
        if (arg[i] == '(') {
            ++depth;
        } else if (arg[i] == ')' && depth > 0) {
            --depth;
        } else if (arg[i] == '%' && depth == 0) {
            pct_at = i;
        }
    }
    has_pct = false;
    color = trim_sv(arg);
    if (pct_at == std::string_view::npos) {
        return !color.empty();
    }
    size_t start = pct_at;
    while (start > 0 && (std::isdigit(static_cast<unsigned char>(arg[start - 1])) ||
                         arg[start - 1] == '.' || arg[start - 1] == '+' || arg[start - 1] == '-')) {
        --start;
    }
    char* end = nullptr;
    const std::string num(arg.substr(start, pct_at - start));
    pct = std::strtof(num.c_str(), &end);
    if (end == num.c_str() || pct < 0.f || pct > 100.f) {
        return false;
    }
    has_pct = true;
    color = trim_sv(arg.substr(0, start));
    return !color.empty();
}

std::optional<Abs> mix_colors(std::string_view inner) {
    const std::vector<std::string_view> parts = split_top_commas(inner);
    if (parts.size() != 3) {
        return std::nullopt;
    }
    Space space = Space::Srgb;
    HueMethod hue = HueMethod::Shorter;
    if (!parse_method(parts[0], space, hue)) {
        return std::nullopt;
    }
    std::string_view ctext[2];
    bool has_pct[2] = {};
    float pct[2] = {};
    for (int i = 0; i < 2; ++i) {
        if (!split_pct(parts[static_cast<size_t>(i + 1)], ctext[i], has_pct[i], pct[i])) {
            return std::nullopt;
        }
    }
    float p1 = 0.5f;
    float p2 = 0.5f;
    if (has_pct[0] && has_pct[1]) {
        p1 = pct[0] / 100.f;
        p2 = pct[1] / 100.f;
    } else if (has_pct[0]) {
        p1 = pct[0] / 100.f;
        p2 = 1.f - p1;
    } else if (has_pct[1]) {
        p2 = pct[1] / 100.f;
        p1 = 1.f - p2;
    }
    if (p1 < 0.f || p2 < 0.f) {
        return std::nullopt;
    }
    const std::optional<Abs> a1 = parse_abs(ctext[0]);
    const std::optional<Abs> a2 = parse_abs(ctext[1]);
    if (!a1 || !a2) {
        return std::nullopt;
    }
    Abs c1 = to_space(*a1, space);
    Abs c2 = to_space(*a2, space);
    const int hi = hue_index(space);
    if (space == Space::Oklch || space == Space::Lch) {
        const float chroma_eps = space == Space::Oklch ? 0.000004f : 0.0015f;
        if (!missing(c1.c[1]) && std::fabs(c1.c[1]) <= chroma_eps) {
            c1.c[hi] = nan_v();
        }
        if (!missing(c2.c[1]) && std::fabs(c2.c[1]) <= chroma_eps) {
            c2.c[hi] = nan_v();
        }
    }
    for (int i = 0; i < 3; ++i) {
        if (missing(c1.c[i]) && !missing(c2.c[i])) {
            c1.c[i] = c2.c[i];
        } else if (missing(c2.c[i]) && !missing(c1.c[i])) {
            c2.c[i] = c1.c[i];
        } else if (missing(c1.c[i]) && missing(c2.c[i])) {
            c1.c[i] = c2.c[i] = 0.f;
        }
    }
    if (missing(c1.a) && !missing(c2.a)) {
        c1.a = c2.a;
    } else if (missing(c2.a) && !missing(c1.a)) {
        c2.a = c1.a;
    }
    if (missing(c1.a)) {
        c1.a = 1.f;
    }
    if (missing(c2.a)) {
        c2.a = 1.f;
    }
    if (polar_space(space)) {
        c2.c[hi] = hue_towards(c1.c[hi], c2.c[hi], hue);
    }
    const float sum = p1 + p2;
    if (sum <= 0.f) {
        Abs empty;
        empty.space = space;
        empty.a = 0.f;
        return empty;
    }
    float alpha_scale = 1.f;
    if (sum > 1.f) {
        p1 /= sum;
        p2 /= sum;
    } else if (sum < 1.f) {
        alpha_scale = sum;
        p1 /= sum;
        p2 /= sum;
    }
    const bool polar = polar_space(space);
    Abs mixed;
    mixed.space = space;
    const float alpha = c1.a * p1 + c2.a * p2;
    for (int i = 0; i < 3; ++i) {
        const bool hue_comp = polar && i == hi;
        const float v1 = hue_comp ? c1.c[i] : c1.c[i] * c1.a;
        const float v2 = hue_comp ? c2.c[i] : c2.c[i] * c2.a;
        const float pre = v1 * p1 + v2 * p2;
        mixed.c[i] = hue_comp || alpha <= 0.f ? pre : pre / alpha;
    }
    if (polar) {
        mixed.c[hi] = std::fmod(mixed.c[hi], 360.f);
        if (mixed.c[hi] < 0.f) {
            mixed.c[hi] += 360.f;
        }
    }
    mixed.a = alpha * alpha_scale;
    return mixed;
}

}  // namespace modern_color

}  // namespace css
}  // namespace zui
