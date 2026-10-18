#include "paint/painter_internal.h"

#include "paint/display_list.h"

#include "perf/perf_budget.h"
#include "css/transition/transition.h"
#include "image/image.h"
#include "layout/clip_path.h"
#include "layout/overflow_clip.h"
#include "layout/paint_map.h"
#include "layout/paint_transform.h"
#include "layout/scrollbar_h.h"
#include "layout/scrollbar_v.h"
#include "layout/stacking.h"

#include "text/font_chain.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace zui {
namespace paint {
namespace painter_detail {
/// `drop-shadow()` is `<color>? && <length>{2,3}`. `inset` and a spread length are not part of
/// the filter function; a declaration that uses them is dropped.
bool parse_one_drop_shadow(std::string_view arg, Color current, DropShadow& shadow) {
    std::vector<std::string> parts;
    std::string cur;
    int depth = 0;
    for (char ch : arg) {
        if (ch == '(') {
            ++depth;
            cur.push_back(ch);
            continue;
        }
        if (ch == ')') {
            depth = std::max(0, depth - 1);
            cur.push_back(ch);
            continue;
        }
        if (depth == 0 && std::isspace(static_cast<unsigned char>(ch))) {
            if (!cur.empty()) {
                parts.push_back(cur);
                cur.clear();
            }
            continue;
        }
        cur.push_back(ch);
    }
    if (!cur.empty()) {
        parts.push_back(cur);
    }
    bool inset = false;
    std::string color_tok;
    std::vector<std::string> lengths;
    for (const std::string& p : parts) {
        const std::string l = lower_ascii(p);
        if (l == "inset") {
            inset = true;
            continue;
        }
        bool bad = false;
        float dummy = 0.f;
        if (p == "0" || (parse_bg_length(p, 0.f, dummy, bad) && !bad)) {
            lengths.push_back(p);
            continue;
        }
        if (color_tok.empty()) {
            color_tok = p;
        }
    }
    if (inset || lengths.size() < 2 || lengths.size() > 3) {
        return false;
    }
    auto len_px = [](const std::string& tok, float& px) {
        bool bad = false;
        if (tok == "0") {
            px = 0.f;
            return true;
        }
        return parse_bg_length(tok, 0.f, px, bad) && !bad;
    };
    float ox = 0.f;
    float oy = 0.f;
    float blur = 0.f;
    if (!len_px(lengths[0], ox) || !len_px(lengths[1], oy)) {
        return false;
    }
    if (lengths.size() == 3 && !len_px(lengths[2], blur)) {
        return false;
    }
    if (blur < 0.f) {
        return false;
    }
    shadow.dx = ox;
    shadow.dy = oy;
    shadow.blur = blur;
    if (color_tok.empty() || lower_ascii(color_tok) == "currentcolor") {
        shadow.color = current;
    } else {
        shadow.color = parse_color(color_tok);
    }
    return true;
}

void identity_color_matrix(float m[20]) {
    for (int i = 0; i < 20; ++i) {
        m[i] = 0.f;
    }
    m[0] = m[6] = m[12] = m[18] = 1.f;
}

bool color_matrix_identity(const float m[20]) {
    for (int i = 0; i < 20; ++i) {
        const float expect = (i == 0 || i == 6 || i == 12 || i == 18) ? 1.f : 0.f;
        if (std::fabs(m[i] - expect) > 0.0005f) {
            return false;
        }
    }
    return true;
}

/// Number or percentage. Empty uses `fallback`. Negatives are rejected. `clamp_unit` implements
/// the spec clamp for grayscale / sepia / invert (amounts above 1 become 1).
bool filter_amount(std::string_view arg, float fallback, bool clamp_unit, float& out) {
    arg = trim_sv_local(arg);
    if (arg.empty()) {
        out = fallback;
        return true;
    }
    const std::string text(arg);
    char* end = nullptr;
    const float v = std::strtof(text.c_str(), &end);
    if (end == text.c_str()) {
        return false;
    }
    const std::string_view unit = trim_sv_local(std::string_view(end));
    float amount = v;
    if (unit == "%") {
        amount = v / 100.f;
    } else if (!unit.empty()) {
        return false;
    }
    if (amount < 0.f) {
        return false;
    }
    if (clamp_unit && amount > 1.f) {
        amount = 1.f;
    }
    out = amount;
    return true;
}

bool filter_hue_degrees(std::string_view arg, float& degrees) {
    arg = trim_sv_local(arg);
    if (arg.empty()) {
        degrees = 0.f;
        return true;
    }
    const std::string text(arg);
    char* end = nullptr;
    const float v = std::strtof(text.c_str(), &end);
    if (end == text.c_str()) {
        return false;
    }
    const std::string_view unit = trim_sv_local(std::string_view(end));
    if (unit.empty()) {
        if (v != 0.f) {
            return false;
        }
        degrees = 0.f;
        return true;
    }
    if (unit == "deg") {
        degrees = v;
    } else if (unit == "turn") {
        degrees = v * 360.f;
    } else if (unit == "rad") {
        degrees = v * (180.f / 3.14159265f);
    } else if (unit == "grad") {
        degrees = v * 0.9f;
    } else {
        return false;
    }
    return true;
}

/// Blink's FilterEffectBuilder matrices. Filter functions run in encoded sRGB (Filter Effects 1:
/// color-interpolation-filters does not apply), so the coefficients multiply the 0–1 channel
/// values and are not linearized first.
void grayscale_matrix(float amount, float m[20]) {
    const float kept = std::clamp(1.f - amount, 0.f, 1.f);
    identity_color_matrix(m);
    m[0] = 0.2126f + 0.7874f * kept;
    m[1] = 0.7152f - 0.7152f * kept;
    m[2] = 0.0722f - 0.0722f * kept;
    m[5] = 0.2126f - 0.2126f * kept;
    m[6] = 0.7152f + 0.2848f * kept;
    m[7] = 0.0722f - 0.0722f * kept;
    m[10] = 0.2126f - 0.2126f * kept;
    m[11] = 0.7152f - 0.7152f * kept;
    m[12] = 0.0722f + 0.9278f * kept;
}

void sepia_matrix(float amount, float m[20]) {
    const float kept = std::clamp(1.f - amount, 0.f, 1.f);
    identity_color_matrix(m);
    m[0] = 0.393f + 0.607f * kept;
    m[1] = 0.769f - 0.769f * kept;
    m[2] = 0.189f - 0.189f * kept;
    m[5] = 0.349f - 0.349f * kept;
    m[6] = 0.686f + 0.314f * kept;
    m[7] = 0.168f - 0.168f * kept;
    m[10] = 0.272f - 0.272f * kept;
    m[11] = 0.534f - 0.534f * kept;
    m[12] = 0.131f + 0.869f * kept;
}

void saturate_matrix(float amount, float m[20]) {
    identity_color_matrix(m);
    m[0] = 0.213f + 0.787f * amount;
    m[1] = 0.715f - 0.715f * amount;
    m[2] = 0.072f - 0.072f * amount;
    m[5] = 0.213f - 0.213f * amount;
    m[6] = 0.715f + 0.285f * amount;
    m[7] = 0.072f - 0.072f * amount;
    m[10] = 0.213f - 0.213f * amount;
    m[11] = 0.715f - 0.715f * amount;
    m[12] = 0.072f + 0.928f * amount;
}

void hue_rotate_matrix(float degrees, float m[20]) {
    const float rad = degrees * 3.14159265f / 180.f;
    const float c = std::cos(rad);
    const float s = std::sin(rad);
    identity_color_matrix(m);
    m[0] = 0.213f + c * 0.787f - s * 0.213f;
    m[1] = 0.715f - c * 0.715f - s * 0.715f;
    m[2] = 0.072f - c * 0.072f + s * 0.928f;
    m[5] = 0.213f - c * 0.213f + s * 0.143f;
    m[6] = 0.715f + c * 0.285f + s * 0.140f;
    m[7] = 0.072f - c * 0.072f - s * 0.283f;
    m[10] = 0.213f - c * 0.213f - s * 0.787f;
    m[11] = 0.715f - c * 0.715f + s * 0.715f;
    m[12] = 0.072f + c * 0.928f + s * 0.072f;
}

void invert_matrix(float amount, float m[20]) {
    const float a = std::clamp(amount, 0.f, 1.f);
    identity_color_matrix(m);
    const float slope = 1.f - 2.f * a;
    m[0] = m[6] = m[12] = slope;
    m[4] = m[9] = m[14] = a;
}

void contrast_matrix(float amount, float m[20]) {
    identity_color_matrix(m);
    m[0] = m[6] = m[12] = amount;
    const float bias = 0.5f - 0.5f * amount;
    m[4] = m[9] = m[14] = bias;
}

void brightness_matrix(float amount, float m[20]) {
    identity_color_matrix(m);
    m[0] = m[6] = m[12] = amount;
}

/// CSS Filter Effects 1 subset: `blur()` / `brightness()` / `contrast()` / `drop-shadow()` /
/// `grayscale()` / `hue-rotate()` / `invert()` / `opacity()` / `saturate()` / `sepia()`,
/// space-separated and applied in that order. `current` is the element's color, used when a
/// shadow omits its color. A lone `brightness()` stays on the per-primitive multiply so an
/// existing swatch stays byte-identical. Any other color function keeps every color step,
/// including an earlier brightness, as its own matrix so each one clamps before the next.
bool parse_css_filter(std::string_view css, FilterEffects& out, Color current) {
    out = FilterEffects{};
    css = trim_sv_local(css);
    if (css.empty() || lower_ascii(std::string(css)) == "none") {
        return false;
    }
    bool any = false;
    bool folded_color = false;
    float steps[FilterEffects::kMaxFilterColorMatrices][20];
    int step_count = 0;
    auto push_step = [&](const float step[20]) {
        if (color_matrix_identity(step)) {
            return;
        }
        if (step_count < FilterEffects::kMaxFilterColorMatrices) {
            for (int n = 0; n < 20; ++n) {
                steps[step_count][n] = step[n];
            }
            ++step_count;
            return;
        }
        concat_color_matrix(steps[FilterEffects::kMaxFilterColorMatrices - 1], step, false);
    };
    size_t i = 0;
    while (i < css.size()) {
        while (i < css.size() && std::isspace(static_cast<unsigned char>(css[i]))) {
            ++i;
        }
        if (i >= css.size()) {
            break;
        }
        const size_t name_begin = i;
        // Function names include hyphens (`drop-shadow`). A leading hyphen is not a name.
        while (i < css.size() && (std::isalpha(static_cast<unsigned char>(css[i])) ||
                                  (css[i] == '-' && i > name_begin))) {
            ++i;
        }
        const std::string name = lower_ascii(std::string(css.substr(name_begin, i - name_begin)));
        while (i < css.size() && std::isspace(static_cast<unsigned char>(css[i]))) {
            ++i;
        }
        if (i >= css.size() || css[i] != '(') {
            break;
        }
        ++i;
        const size_t arg_begin = i;
        int depth = 1;
        while (i < css.size() && depth > 0) {
            if (css[i] == '(') {
                ++depth;
            } else if (css[i] == ')') {
                --depth;
            }
            if (depth > 0) {
                ++i;
            }
        }
        const std::string_view arg = trim_sv_local(css.substr(arg_begin, i - arg_begin));
        if (i < css.size() && css[i] == ')') {
            ++i;
        }
        char* end = nullptr;
        const std::string arg_s(arg);
        const float v = std::strtof(arg_s.c_str(), &end);
        std::string_view unit =
            end != nullptr ? trim_sv_local(std::string_view(end)) : std::string_view{};
        if (name == "blur") {
            out.blur = std::max(out.blur, std::max(0.f, v));
            any = true;
            (void)unit;
        } else if (name == "brightness") {
            float b = unit == "%" ? v / 100.f : v;
            b = std::max(0.f, b);
            float step[20];
            brightness_matrix(b, step);
            push_step(step);
            out.brightness *= b;
            any = true;
        } else if (name == "opacity") {
            float o = unit == "%" ? v / 100.f : v;
            out.opacity *= std::max(0.f, std::min(1.f, o));
            any = true;
        } else if (name == "grayscale" || name == "sepia" || name == "invert" ||
                   name == "contrast" || name == "saturate") {
            const bool clamp_unit = name == "grayscale" || name == "sepia" || name == "invert";
            float amount = 0.f;
            if (filter_amount(arg, 1.f, clamp_unit, amount)) {
                float step[20];
                if (name == "grayscale") {
                    grayscale_matrix(amount, step);
                } else if (name == "sepia") {
                    sepia_matrix(amount, step);
                } else if (name == "invert") {
                    invert_matrix(amount, step);
                } else if (name == "contrast") {
                    contrast_matrix(amount, step);
                } else {
                    saturate_matrix(amount, step);
                }
                push_step(step);
                folded_color = true;
                any = true;
            }
        } else if (name == "hue-rotate") {
            float degrees = 0.f;
            if (filter_hue_degrees(arg, degrees)) {
                float step[20];
                hue_rotate_matrix(degrees, step);
                push_step(step);
                folded_color = true;
                any = true;
            }
        } else if (name == "drop-shadow") {
            DropShadow shadow;
            if (parse_one_drop_shadow(arg, current, shadow)) {
                out.shadows.push_back(shadow);
                any = true;
            }
        }
    }
    if (folded_color) {
        out.color_matrix_count = step_count;
        out.has_color_matrix = step_count > 0;
        out.color_matrix.assign(static_cast<std::size_t>(step_count * 20), 0.f);
        for (int s = 0; s < step_count; ++s) {
            for (int n = 0; n < 20; ++n) {
                out.color_matrix[static_cast<std::size_t>(s * 20 + n)] = steps[s][n];
            }
        }
        // Brightness in this list is one of the steps. Leaving the per-primitive multiply in
        // place would apply it twice.
        out.brightness = 1.f;
    }
    return any && out.any();
}

}  // namespace painter_detail
}  // namespace paint
}  // namespace zui
