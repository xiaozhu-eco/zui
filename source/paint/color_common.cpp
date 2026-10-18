#include "paint/color_internal.h"
#include "paint/display_list.h"
#include "paint/painter_internal.h"

#include "ascii.h"
#include "css/values.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace zui {
namespace paint {
namespace color_detail {

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

std::string lowered(std::string_view s) { return ascii_lower(s); }

/// Split on commas outside parentheses, so `rgba(1, 2, 3, .5)` stays one argument.
std::vector<std::string_view> split_args(std::string_view s) {
    std::vector<std::string_view> out;
    size_t start = 0;
    int depth = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '(') {
            ++depth;
        } else if (c == ')') {
            depth = depth > 0 ? depth - 1 : 0;
        } else if (c == ',' && depth == 0) {
            out.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    out.push_back(s.substr(start));
    return out;
}

/// Scan the leading `<number>` and leave `rest` on the unit.
bool scan_number(std::string_view s, float& value, std::string_view& rest) {
    s = trim(s);
    size_t n = 0;
    bool digit = false;
    while (n < s.size()) {
        const char c = s[n];
        if (std::isdigit(static_cast<unsigned char>(c))) {
            digit = true;
        } else if (c != '.' && c != '+' && c != '-' && c != 'e' && c != 'E') {
            break;
        }
        ++n;
    }
    if (!digit) {
        return false;
    }
    const std::string num(s.substr(0, n));
    char* stop = nullptr;
    value = std::strtof(num.c_str(), &stop);
    if (stop == num.c_str() || *stop != '\0') {
        return false;
    }
    rest = trim(s.substr(n));
    return true;
}

/// One of the four CSS angle units. The computed value may be any of them, and the line geometry
/// below only wants degrees.
bool parse_angle(std::string_view s, float& deg) {
    float v = 0;
    std::string_view unit;
    if (!scan_number(s, v, unit)) {
        return false;
    }
    if (equals_ci(unit, "deg")) {
        deg = v;
    } else if (equals_ci(unit, "grad")) {
        deg = v * 0.9f;
    } else if (equals_ci(unit, "rad")) {
        deg = v * 180.0f / kPi;
    } else if (equals_ci(unit, "turn")) {
        deg = v * 360.0f;
    } else {
        return false;
    }
    return true;
}

/// A `<length-percentage>` stop position, resolved against the gradient line length.
bool parse_stop_position(std::string_view s, float line_len, float& fraction) {
    float v = 0;
    std::string_view unit;
    if (!scan_number(s, v, unit)) {
        return false;
    }
    if (unit == "%") {
        fraction = v * 0.01f;
    } else if (unit.empty() || equals_ci(unit, "px")) {
        fraction = line_len > 0.0f ? v / line_len : 0.0f;
    } else {
        return false;
    }
    return true;
}

/// Whitespace tokens outside parentheses, so `rgb(1, 2, 3)` and `calc(50% - 4px)` stay one token.
void tokenize_ws(std::string_view s, std::vector<std::string>& toks) {
    std::string cur;
    int depth = 0;
    for (char ch : s) {
        if (ch == '(') {
            ++depth;
            cur.push_back(ch);
            continue;
        }
        if (ch == ')') {
            depth = depth > 0 ? depth - 1 : 0;
            cur.push_back(ch);
            continue;
        }
        if (depth == 0 && std::isspace(static_cast<unsigned char>(ch))) {
            if (!cur.empty()) {
                toks.push_back(std::move(cur));
                cur.clear();
            }
            continue;
        }
        cur.push_back(ch);
    }
    if (!cur.empty()) {
        toks.push_back(std::move(cur));
    }
}

Color mix_premul(Color a, Color b, float f) {
    f = std::max(0.0f, std::min(1.0f, f));
    const float wa = static_cast<float>(a.a) / 255.0f;
    const float wb = static_cast<float>(b.a) / 255.0f;
    const float w = wa + (wb - wa) * f;
    auto channel = [&](std::uint8_t ca, std::uint8_t cb) {
        const float pa = static_cast<float>(ca) * wa;
        const float pb = static_cast<float>(cb) * wb;
        const float p = pa + (pb - pa) * f;
        const float v = w > 0.0f ? p / w : 0.0f;
        return static_cast<std::uint8_t>(std::lround(std::max(0.0f, std::min(255.0f, v))));
    };
    return Color{channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b),
                 static_cast<std::uint8_t>(std::lround(std::max(0.0f, std::min(255.0f, w * 255.0f))))};
}

Color resolve_gradient_stop(std::string_view text, const layout::Box* color_box) {
    if (color_box == nullptr) {
        return parse_color(text);
    }
    const std::string owned(text);
    return painter_detail::paint_color(*color_box, &owned);
}

bool known_interpolation_space(const std::string& name) {
    return name == "srgb" || name == "srgb-linear" || name == "display-p3" || name == "lab" ||
           name == "lch" || name == "oklab" || name == "oklch" || name == "hsl" || name == "hwb" ||
           name == "xyz" || name == "xyz-d65" || name == "xyz-d50";
}

bool polar_interpolation_space(const std::string& name) {
    return name == "hsl" || name == "hwb" || name == "lch" || name == "oklch";
}

/// Pull `in <space> [shorter|longer|increasing|decreasing hue]` out of a gradient prelude.
/// An absent method leaves `spec` empty (legacy premultiplied sRGB). An unknown space, or a
/// hue keyword on a rectangular space, rejects the gradient. a98-rgb / prophoto-rgb / rec2020
/// stay unsupported.
bool take_interpolation_method(std::vector<std::string>& toks, std::string& spec) {
    spec.clear();
    int found = -1;
    for (size_t i = 0; i < toks.size(); ++i) {
        if (lowered(toks[i]) != "in") {
            continue;
        }
        if (found >= 0) {
            return false;
        }
        found = static_cast<int>(i);
    }
    if (found < 0) {
        return true;
    }
    const size_t i = static_cast<size_t>(found);
    if (i + 1 >= toks.size()) {
        return false;
    }
    const std::string space = lowered(toks[i + 1]);
    if (!known_interpolation_space(space)) {
        return false;
    }
    size_t end = i + 2;
    std::string hue;
    if (end < toks.size()) {
        const std::string word = lowered(toks[end]);
        const bool hue_word = word == "shorter" || word == "longer" || word == "increasing" ||
                              word == "decreasing";
        if (hue_word) {
            if (!polar_interpolation_space(space) || end + 1 >= toks.size() ||
                lowered(toks[end + 1]) != "hue") {
                return false;
            }
            hue = " " + word + " hue";
            end += 2;
        }
    }
    spec = "in " + space + hue;
    toks.erase(toks.begin() + static_cast<std::ptrdiff_t>(i),
               toks.begin() + static_cast<std::ptrdiff_t>(end));
    return true;
}

/// `t` is the weight of `b`. Rectangular and polar mixes go through `color-mix`, which already
/// premultiplies and applies the hue method. Endpoints stay the stop colours so an 8-bit
/// round-trip cannot move them.
Color mix_in_method(Color a, Color b, float t, const std::string& spec) {
    t = std::max(0.0f, std::min(1.0f, t));
    if (t <= 0.0f) {
        return a;
    }
    if (t >= 1.0f) {
        return b;
    }
    char buf[192];
    std::snprintf(buf, sizeof(buf),
                  "color-mix(%s, #%02x%02x%02x%02x %.6f%%, #%02x%02x%02x%02x)", spec.c_str(),
                  static_cast<unsigned>(b.r), static_cast<unsigned>(b.g),
                  static_cast<unsigned>(b.b), static_cast<unsigned>(b.a),
                  static_cast<double>(t) * 100.0, static_cast<unsigned>(a.r),
                  static_cast<unsigned>(a.g), static_cast<unsigned>(a.b),
                  static_cast<unsigned>(a.a));
    if (const std::optional<css::CssRgba> mixed = css::parse_css_color(buf)) {
        return Color{mixed->r, mixed->g, mixed->b, mixed->a};
    }
    return mix_premul(a, b, t);
}

/// The fragment shader still lerps in premultiplied sRGB. Sampling the specified space at
/// 1/128 of each span keeps that chord inside a channel on ordinary boxes. A hint bends the
/// sample weight with the same power curve Blink uses, evaluated in this space rather than sRGB.
void resample_in_method(std::vector<GradientStop>& stops, const std::vector<GradientHint>& hints,
                        const std::string& spec) {
    if (spec.empty() || stops.size() < 2) {
        return;
    }
    constexpr int kSteps = 128;
    std::vector<GradientStop> sampled;
    sampled.reserve(stops.size() * static_cast<std::size_t>(kSteps));
    std::size_t h = 0;
    for (std::size_t i = 0; i < stops.size(); ++i) {
        sampled.push_back(stops[i]);
        if (i + 1 >= stops.size()) {
            break;
        }
        const float left = stops[i].offset;
        const float right = stops[i + 1].offset;
        const float total = right - left;
        float hint = -1.0f;
        while (h < hints.size() && hints[h].after == i) {
            if (hint < 0.0f) {
                float pos = hints[h].position;
                if (pos < left) {
                    pos = left;
                }
                if (pos > right) {
                    pos = right;
                }
                if (pos > left && pos < right && total > 0.0f) {
                    hint = pos;
                }
            }
            ++h;
        }
        if (total <= 0.0f) {
            continue;
        }
        const int n = std::max(1, static_cast<int>(std::ceil(total * static_cast<float>(kSteps))));
        const float hint_rel = hint >= 0.0f ? (hint - left) / total : 0.0f;
        const float exponent =
            hint >= 0.0f && hint_rel > 0.0f && hint_rel < 1.0f ? std::log(0.5f) / std::log(hint_rel)
                                                                : 1.0f;
        for (int k = 1; k < n; ++k) {
            const float point_rel = static_cast<float>(k) / static_cast<float>(n);
            const float weight = hint >= 0.0f ? std::pow(point_rel, exponent) : point_rel;
            sampled.push_back(GradientStop{
                left + total * point_rel,
                mix_in_method(stops[i].color, stops[i + 1].color, weight, spec)});
        }
    }
    stops = std::move(sampled);
}

/// Blink replaces one legacy (premultiplied sRGB) hint with nine stops. The long side is
/// sampled from its midpoint; the short side gets two stops. Weight is
/// point^(ln 0.5 / ln hint). A hint parked on either neighbour is dropped.
void expand_srgb_hints(std::vector<GradientStop>& stops, const std::vector<GradientHint>& hints) {
    if (hints.empty() || stops.size() < 2) {
        return;
    }
    std::vector<GradientStop> with_hints;
    with_hints.reserve(stops.size() + hints.size() * 9);
    std::size_t h = 0;
    for (std::size_t i = 0; i < stops.size(); ++i) {
        with_hints.push_back(stops[i]);
        while (h < hints.size() && hints[h].after == i && i + 1 < stops.size()) {
            const float left = stops[i].offset;
            const float right = stops[i + 1].offset;
            float hint = hints[h].position;
            if (hint < left) {
                hint = left;
            }
            if (hint > right) {
                hint = right;
            }
            const float total = right - left;
            if (hint > left && hint < right && total > 0.0f) {
                const float left_dist = hint - left;
                const float right_dist = right - hint;
                float offs[9];
                if (left_dist > right_dist) {
                    for (int y = 0; y < 7; ++y) {
                        offs[y] = left + left_dist * static_cast<float>(7 + y) / 13.0f;
                    }
                    offs[7] = hint + right_dist / 3.0f;
                    offs[8] = hint + right_dist * 2.0f / 3.0f;
                } else {
                    offs[0] = left + left_dist / 3.0f;
                    offs[1] = left + left_dist * 2.0f / 3.0f;
                    for (int y = 0; y < 7; ++y) {
                        offs[y + 2] = hint + right_dist * static_cast<float>(y) / 13.0f;
                    }
                }
                const float hint_rel = left_dist / total;
                const float exponent = std::log(0.5f) / std::log(hint_rel);
                for (float off : offs) {
                    const float point_rel = (off - left) / total;
                    const float weight = std::pow(point_rel, exponent);
                    with_hints.push_back(GradientStop{
                        off, mix_premul(stops[i].color, stops[i + 1].color, weight)});
                }
            }
            ++h;
        }
    }
    stops = std::move(with_hints);
}

bool parse_length_against(std::string_view s, float basis, float& px) {
    // Length syntax is css::resolve_length. A percentage stays a number here and is placed on
    // the gradient box by the caller; that placement is not a cascade percentage.
    const css::ResolvedLength resolved = css::resolve_length(s, 16.f, 16.f);
    if (!resolved.valid) {
        return false;
    }
    if (resolved.is_percent) {
        px = basis * resolved.px * 0.01f;
        return true;
    }
    px = resolved.px;
    return true;
}

Color lerp_stops(const Gradient& g, float t) {
    if (g.stops.empty()) {
        return Color::transparent();
    }
    if (g.stops.size() == 1) {
        return g.stops.front().color;
    }
    if (g.repeating) {
        const float t0 = g.stops.front().offset;
        const float t1 = g.stops.back().offset;
        const float period = t1 - t0;
        if (period > 1e-6f) {
            float u = std::fmod(t - t0, period);
            if (u < 0.f) {
                u += period;
            }
            t = t0 + u;
        }
    }
    if (t <= g.stops.front().offset) {
        return g.stops.front().color;
    }
    for (size_t i = 1; i < g.stops.size(); ++i) {
        const GradientStop& b = g.stops[i];
        if (t > b.offset) {
            continue;
        }
        const GradientStop& a = g.stops[i - 1];
        const float span = b.offset - a.offset;
        const float f = span > 0.0f ? (t - a.offset) / span : 0.0f;
        const float wa = static_cast<float>(a.color.a) / 255.0f;
        const float wb = static_cast<float>(b.color.a) / 255.0f;
        const float w = wa + (wb - wa) * f;
        auto channel = [&](std::uint8_t ca, std::uint8_t cb) {
            const float pa = static_cast<float>(ca) * wa;
            const float pb = static_cast<float>(cb) * wb;
            const float p = pa + (pb - pa) * f;
            const float v = w > 0.0f ? p / w : 0.0f;
            return static_cast<std::uint8_t>(std::lround(std::max(0.0f, std::min(255.0f, v))));
        };
        return Color{channel(a.color.r, b.color.r), channel(a.color.g, b.color.g),
                     channel(a.color.b, b.color.b),
                     static_cast<std::uint8_t>(std::lround(w * 255.0f))};
    }
    return g.stops.back().color;
}

}  // namespace color_detail

}  // namespace paint
}  // namespace zui
