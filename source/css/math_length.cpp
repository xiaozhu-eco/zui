#include "css/values.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "css/values_internal.h"

namespace zui {
namespace css {

namespace values_detail {


// A `<length-percentage>` or a bare number while a `calc()` is being evaluated. `number` marks the
// bare-number case, which only `*` and `/` accept as an operand.
struct MathValue {
    bool valid = false;
    bool number = false;
    bool percent = false;
    float value = 0;
};

MathValue math_number(float value) { return MathValue{true, true, false, value}; }

bool same_kind(const MathValue& a, const MathValue& b) {
    return a.valid && b.valid && !a.number && !b.number && a.percent == b.percent;
}

/// One term of a `calc()`-like expression. It is deliberately small: CSS lengths only need `+`,
/// `-`, `*`, `/`, parentheses and `min()` / `max()` / `clamp()`.
class MathExpr {
public:
    MathExpr(std::string_view text, float font_size, float root_font_size,
             float viewport_width = 0.f, float viewport_height = 0.f, float percent_basis = -1.f,
             FontRelativeUnits font_rel = {})
        : s_(text), font_(font_size), root_(root_font_size), viewport_w_(viewport_width),
          viewport_h_(viewport_height), percent_basis_(percent_basis), font_rel_(font_rel) {}

    MathValue run() {
        MathValue out = sum();
        skip();
        if (!out.valid || i_ != s_.size()) {
            return {};
        }
        return out;
    }

private:
    void skip() {
        while (i_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[i_]))) {
            ++i_;
        }
    }

    bool eat(char c) {
        skip();
        if (i_ < s_.size() && s_[i_] == c) {
            ++i_;
            return true;
        }
        return false;
    }

    bool peek(char c) {
        skip();
        return i_ < s_.size() && s_[i_] == c;
    }

    std::string read_ident() {
        skip();
        const std::size_t start = i_;
        while (i_ < s_.size() && (std::isalpha(static_cast<unsigned char>(s_[i_])) || s_[i_] == '-')) {
            ++i_;
        }
        std::string out(s_.substr(start, i_ - start));
        for (char& c : out) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return out;
    }

    MathValue factor() {
        skip();
        if (eat('(')) {
            MathValue inner = sum();
            if (!eat(')')) {
                return {};
            }
            return inner;
        }
        if (i_ < s_.size() && std::isalpha(static_cast<unsigned char>(s_[i_]))) {
            const std::string name = read_ident();
            // `calc()` nests inside the other math functions - `max(20px, calc((100% - 41rem) /
            // 2))` is the reader's page margin - and it is nothing more than a parenthesised
            // expression, so it takes the same path as `(`.
            if (name == "calc") {
                if (!eat('(')) {
                    return {};
                }
                MathValue inner = sum();
                if (!eat(')')) {
                    return {};
                }
                return inner;
            }
            if (name == "min" || name == "max" || name == "clamp") {
                return function(name);
            }
            return {};
        }
        const std::size_t start = i_;
        if (i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) {
            ++i_;
        }
        bool digits = false;
        while (i_ < s_.size() &&
               (std::isdigit(static_cast<unsigned char>(s_[i_])) || s_[i_] == '.')) {
            ++i_;
            digits = true;
        }
        if (!digits) {
            return {};
        }
        const float number = std::strtof(std::string(s_.substr(start, i_ - start)).c_str(), nullptr);
        std::string unit = read_ident();
        // `read_ident` only takes letters, so a percentage sign has to be taken on its own.
        if (unit.empty() && i_ < s_.size() && s_[i_] == '%') {
            ++i_;
            unit = "%";
        }
        if (unit.empty()) {
            return math_number(number);
        }
        if (unit == "%") {
            if (percent_basis_ >= 0.f) {
                return MathValue{true, false, false, percent_basis_ * number / 100.f};
            }
            return MathValue{true, false, true, number};
        }
        // Container query units are a percentage of the query container. For a child of
        // that container the percentage basis is already the container's inline size, which
        // is what `50cqw` means (CSS Containment).
        if (unit == "cqw" || unit == "cqi" || unit == "cqh" || unit == "cqb" || unit == "cqmin" ||
            unit == "cqmax") {
            if (percent_basis_ >= 0.f) {
                return MathValue{true, false, false, percent_basis_ * number / 100.f};
            }
            return MathValue{true, false, true, number};
        }
        // Viewport units (CSS Values section 5.1): the initial containing block is the viewport.
        // Small / large / dynamic viewports are the same here (no retracting browser UI), and the
        // logical `vi` / `vb` map to the inline / block axis, which is horizontal for the writing
        // modes this engine lays out.
        float viewport = 0.f;
        if (unit == "vw" || unit == "dvw" || unit == "svw" || unit == "lvw" || unit == "vi") {
            viewport = viewport_w_ * 0.01f;
        } else if (unit == "vh" || unit == "dvh" || unit == "svh" || unit == "lvh" ||
                   unit == "vb") {
            viewport = viewport_h_ * 0.01f;
        } else if (unit == "vmin") {
            viewport = std::min(viewport_w_, viewport_h_) * 0.01f;
        } else if (unit == "vmax") {
            viewport = std::max(viewport_w_, viewport_h_) * 0.01f;
        }
        if (viewport > 0.f) {
            return MathValue{true, false, false, number * viewport};
        }
        // Font-relative units (CSS Values §6.1.1). Prefer the caller's measured bases; otherwise
        // the spec fallbacks: ch/ex → 0.5em, lh → 1em (used line-height unavailable).
        if (unit == "ch") {
            const float ch = font_rel_.ch > 0.f ? font_rel_.ch : font_ * 0.5f;
            return MathValue{true, false, false, number * ch};
        }
        if (unit == "ex") {
            const float ex = font_rel_.ex > 0.f ? font_rel_.ex : font_ * 0.5f;
            return MathValue{true, false, false, number * ex};
        }
        if (unit == "lh") {
            const float lh = font_rel_.lh > 0.f ? font_rel_.lh : font_;
            return MathValue{true, false, false, number * lh};
        }
        const float basis = unit == "em" ? font_ : unit == "rem" ? root_ : 0.f;
        const float factor = unit == "em" || unit == "rem" ? basis : absolute_px(1.f, unit);
        if (!std::isfinite(factor)) {
            return {};
        }
        return MathValue{true, false, false, number * factor};
    }

    MathValue function(const std::string& name) {
        if (!eat('(')) {
            return {};
        }
        std::vector<MathValue> args;
        for (;;) {
            args.push_back(sum());
            if (!args.back().valid) {
                return {};
            }
            if (eat(',')) {
                continue;
            }
            break;
        }
        if (!eat(')')) {
            return {};
        }
        if (name == "clamp") {
            // `clamp(MIN, VAL, MAX)` is `max(MIN, min(VAL, MAX))`: a value outside the range is
            // clamped, not rejected. Exactly three arguments.
            if (args.size() != 3) {
                return {};
            }
            if (!same_kind(args[0], args[1]) || !same_kind(args[0], args[2])) {
                return {};
            }
            const float value = std::max(args[0].value, std::min(args[1].value, args[2].value));
            return MathValue{true, false, args[0].percent, value};
        }
        // CSS Values 4: `min()` and `max()` take one or more arguments.
        if (args.empty()) {
            return {};
        }
        for (std::size_t n = 1; n < args.size(); ++n) {
            if (!same_kind(args[0], args[n])) {
                return {};
            }
        }
        float value = args[0].value;
        for (std::size_t n = 1; n < args.size(); ++n) {
            value = name == "min" ? std::min(value, args[n].value) : std::max(value, args[n].value);
        }
        return MathValue{true, false, args[0].percent, value};
    }

    MathValue product() {
        MathValue left = factor();
        for (;;) {
            skip();
            if (i_ >= s_.size() || (s_[i_] != '*' && s_[i_] != '/')) {
                return left;
            }
            const char op = s_[i_++];
            const MathValue right = factor();
            if (!left.valid || !right.valid) {
                return {};
            }
            if (op == '*') {
                if (right.number) {
                    left.value *= right.value;
                } else if (left.number) {
                    left = MathValue{true, false, right.percent, left.value * right.value};
                } else {
                    return {};
                }
            } else {
                if (!right.number || right.value == 0.f) {
                    return {};
                }
                left.value /= right.value;
            }
        }
    }

    MathValue sum() {
        MathValue left = product();
        for (;;) {
            skip();
            if (i_ >= s_.size() || (s_[i_] != '+' && s_[i_] != '-')) {
                return left;
            }
            const char op = s_[i_++];
            const MathValue right = product();
            // `+`/`-` combine two lengths of the same kind (or two percentages), never a number.
            if (!same_kind(left, right)) {
                return {};
            }
            left.value = op == '+' ? left.value + right.value : left.value - right.value;
        }
    }

    std::string_view s_;
    std::size_t i_ = 0;
    float font_ = 16.f;
    float root_ = 16.f;
    float viewport_w_ = 0.f;
    float viewport_h_ = 0.f;
    float percent_basis_ = -1.f;
    FontRelativeUnits font_rel_;
};


}  // namespace values_detail

using namespace values_detail;

namespace {

std::string_view trim_length(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

bool starts_with(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

/// `<number><unit>` without a heap copy. `strtof` needs a terminator; the token sits on the
/// stack unless it is absurdly long. The unit is lowercased into a second stack buffer.
ResolvedLength resolve_unit_length(std::string_view trimmed, float font, float root,
                                   float viewport_width, float viewport_height,
                                   const FontRelativeUnits& font_rel) {
    char buf[96];
    std::string owned;
    const char* cstr = nullptr;
    if (trimmed.size() < sizeof(buf)) {
        if (!trimmed.empty()) {
            std::memcpy(buf, trimmed.data(), trimmed.size());
        }
        buf[trimmed.size()] = '\0';
        cstr = buf;
    } else {
        owned.assign(trimmed.data(), trimmed.size());
        cstr = owned.c_str();
    }
    char* unit_begin = nullptr;
    const float number = std::strtof(cstr, &unit_begin);
    if (unit_begin == cstr) {
        return {};
    }
    const std::string_view raw_unit(unit_begin);
    char lower_buf[24];
    std::string lower_owned;
    std::string_view unit = raw_unit;
    if (raw_unit.size() < sizeof(lower_buf)) {
        for (std::size_t i = 0; i < raw_unit.size(); ++i) {
            lower_buf[i] =
                static_cast<char>(std::tolower(static_cast<unsigned char>(raw_unit[i])));
        }
        unit = std::string_view(lower_buf, raw_unit.size());
    } else {
        lower_owned.resize(raw_unit.size());
        for (std::size_t i = 0; i < raw_unit.size(); ++i) {
            lower_owned[i] =
                static_cast<char>(std::tolower(static_cast<unsigned char>(raw_unit[i])));
        }
        unit = lower_owned;
    }
    if (unit.empty() || unit == "px") {
        return ResolvedLength{true, false, number};
    }
    if (unit == "%") {
        return ResolvedLength{true, true, number};
    }
    // `cqw` / `cqi` (and the block-axis query units) resolve like a percentage of the
    // query container. The used-value pass resolves percentages against the containing
    // block, which for these cases is that container.
    if (unit == "cqw" || unit == "cqi" || unit == "cqh" || unit == "cqb" || unit == "cqmin" ||
        unit == "cqmax") {
        return ResolvedLength{true, true, number};
    }
    // Viewport units resolve against the initial containing block; a caller that does not know it
    // passes zero and the declaration stays invalid, which is what the callers did before they
    // could pass a viewport (CSS Values section 5.1).
    float viewport = 0.f;
    if (unit == "vw" || unit == "dvw" || unit == "svw" || unit == "lvw" || unit == "vi") {
        viewport = viewport_width * 0.01f;
    } else if (unit == "vh" || unit == "dvh" || unit == "svh" || unit == "lvh" || unit == "vb") {
        viewport = viewport_height * 0.01f;
    } else if (unit == "vmin") {
        viewport = std::min(viewport_width, viewport_height) * 0.01f;
    } else if (unit == "vmax") {
        viewport = std::max(viewport_width, viewport_height) * 0.01f;
    }
    if (viewport > 0.f) {
        return ResolvedLength{true, false, number * viewport};
    }
    if (unit == "ch") {
        const float ch = font_rel.ch > 0.f ? font_rel.ch : font * 0.5f;
        return ResolvedLength{true, false, number * ch};
    }
    if (unit == "ex") {
        const float ex = font_rel.ex > 0.f ? font_rel.ex : font * 0.5f;
        return ResolvedLength{true, false, number * ex};
    }
    if (unit == "lh") {
        const float lh = font_rel.lh > 0.f ? font_rel.lh : font;
        return ResolvedLength{true, false, number * lh};
    }
    const float factor = unit == "em" ? font : unit == "rem" ? root : absolute_px(1.f, unit);
    if (!std::isfinite(factor)) {
        return {};
    }
    return ResolvedLength{true, false, number * factor};
}

}  // namespace

ResolvedLength resolve_length(std::string_view text, float font_size, float root_font_size,
                              const FontRelativeUnits& font_rel) {
    return resolve_length(text, font_size, root_font_size, 0.f, 0.f, font_rel);
}

ResolvedLength resolve_length_basis(std::string_view text, float font_size, float root_font_size,
                                    float viewport_width, float viewport_height,
                                    float percent_basis, const FontRelativeUnits& font_rel) {
    const std::string_view trimmed = trim_length(text);
    if (trimmed.empty() || trimmed == "auto" || trimmed == "none") {
        return {};
    }
    const float font = font_size > 0 ? font_size : 16.f;
    const float root = root_font_size > 0 ? root_font_size : 16.f;
    const bool calc = starts_with(trimmed, "calc(") && trimmed.back() == ')';
    const bool fn = starts_with(trimmed, "min(") || starts_with(trimmed, "max(") ||
                    starts_with(trimmed, "clamp(");
    if (calc || (fn && trimmed.back() == ')')) {
        const std::string_view body =
            calc ? trimmed.substr(5, trimmed.size() - 6) : trimmed;
        MathExpr expr(body, font, root, viewport_width, viewport_height, percent_basis, font_rel);
        const MathValue value = expr.run();
        if (!value.valid || value.number || value.percent) {
            return {};
        }
        return ResolvedLength{true, false, value.value};
    }
    return resolve_unit_length(trimmed, font, root, viewport_width, viewport_height, font_rel);
}

ResolvedLength resolve_length_percent(std::string_view text, float percent_basis) {
    // The font sizes and the viewport are placeholders: a kept declaration had its `em` / `rem` /
    // viewport units baked into px while it was interned, so neither can change the result.
    return resolve_length_basis(text, 16.f, 16.f, 0.f, 0.f, percent_basis);
}

ResolvedLength resolve_length(std::string_view text, float font_size, float root_font_size,
                              float viewport_width, float viewport_height,
                              const FontRelativeUnits& font_rel) {
    const std::string_view trimmed = trim_length(text);
    if (trimmed.empty() || trimmed == "auto" || trimmed == "none") {
        return {};
    }
    const float font = font_size > 0 ? font_size : 16.f;
    const float root = root_font_size > 0 ? root_font_size : 16.f;
    if (starts_with(trimmed, "calc(") && trimmed.back() == ')') {
        MathExpr expr(trimmed.substr(5, trimmed.size() - 6), font, root, viewport_width,
                      viewport_height, -1.f, font_rel);
        const MathValue value = expr.run();
        if (!value.valid || value.number) {
            return {};
        }
        return ResolvedLength{true, value.percent, value.value};
    }
    const bool function_form = starts_with(trimmed, "min(") || starts_with(trimmed, "max(") ||
                               starts_with(trimmed, "clamp(");
    if (function_form && trimmed.back() == ')') {
        MathExpr expr(trimmed, font, root, viewport_width, viewport_height, -1.f, font_rel);
        const MathValue value = expr.run();
        if (!value.valid || value.number) {
            return {};
        }
        return ResolvedLength{true, value.percent, value.value};
    }
    // A bare `<number><unit>`; anything else (keywords, `var()`, ...) is left to the caller.
    return resolve_unit_length(trimmed, font, root, viewport_width, viewport_height, font_rel);
}
}  // namespace css
}  // namespace zui
