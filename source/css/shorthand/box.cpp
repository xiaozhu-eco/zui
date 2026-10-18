// The box and flex shorthands: margin / padding edges, the border sides and `flex`.

#include "css/shorthand/shorthand.h"
#include "css/shorthand/internal.h"

#include "css/values.h"

#include <lexbor/css/css.h>

#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zui {
namespace css {

using namespace shorthand_detail;

namespace shorthand_detail {

void distribute4(const std::string* v, int n, std::string out[4]) {
    if (n <= 1) {
        const std::string& a = n == 1 ? v[0] : std::string("0");
        out[0] = out[1] = out[2] = out[3] = a;
    } else if (n == 2) {
        out[0] = out[2] = v[0];
        out[1] = out[3] = v[1];
    } else if (n == 3) {
        out[0] = v[0];
        out[1] = out[3] = v[1];
        out[2] = v[2];
    } else {
        out[0] = v[0];
        out[1] = v[1];
        out[2] = v[2];
        out[3] = v[3];
    }
}

void expand_edges(const lxb_css_value_length_percentage_t* sides, const char* names[4],
                  Parts& out) {
    std::string got[4];
    int n = 0;
    for (int i = 0; i < 4; ++i) {
        if (sides[i].type == LXB_CSS_VALUE__UNDEF) {
            break;
        }
        got[n++] = ser_length(sides[i]);
    }
    std::string box[4];
    distribute4(got, n, box);
    for (int i = 0; i < 4; ++i) {
        add(out, names[i], box[i]);
    }
}

void expand_border_side(const char* side, const lxb_css_property_border_t& b, Parts& out) {
    const std::string width = std::string("border-") + side + "-width";
    const std::string style = std::string("border-") + side + "-style";
    const std::string color = std::string("border-") + side + "-color";
    // CSS Values 4 section 3.2: `border-top: inherit` carries the keyword to all three longhands.
    // (`initial` / `unset` are the reset below: those *are* a border's initial values.)
    if (b.style == LXB_CSS_VALUE_INHERIT) {
        add(out, width.c_str(), "inherit");
        add(out, style.c_str(), "inherit");
        add(out, color.c_str(), "inherit");
        return;
    }
    if (b.style == LXB_CSS_VALUE_INITIAL || b.style == LXB_CSS_VALUE_UNSET) {
        add(out, width.c_str(), "medium");
        add(out, style.c_str(), "none");
        add(out, color.c_str(), "currentcolor");
        return;
    }
    add(out, width.c_str(), ser_border_width(b.width));
    add(out, style.c_str(), ser_ident(b.style, "none"));
    add(out, color.c_str(), ser_color(b.color));
}

void expand_border_all(const lxb_css_property_border_t& b, Parts& out) {
    expand_border_side("top", b, out);
    expand_border_side("right", b, out);
    expand_border_side("bottom", b, out);
    expand_border_side("left", b, out);
}

void expand_flex(const lxb_css_property_flex_t& flex, Parts& out) {
    if (flex.type == LXB_CSS_FLEX_NONE) {
        add(out, "flex-grow", "0");
        add(out, "flex-shrink", "0");
        add(out, "flex-basis", "auto");
        return;
    }
    add(out, "flex-grow", ser_number(flex.grow, "1"));
    add(out, "flex-shrink", ser_number(flex.shrink, "1"));
    add(out, "flex-basis", ser_basis(flex.basis));
}

/// A non-negative `<number>`, not a length. `1px` and `calc(...)` are not numbers.
bool flex_number(const std::string& token, float* out) {
    if (token.empty()) {
        return false;
    }
    char* end = nullptr;
    const float number = std::strtof(token.c_str(), &end);
    if (end == token.c_str() || end == nullptr || *end != '\0' || number < 0.f) {
        return false;
    }
    if (out != nullptr) {
        *out = number;
    }
    return true;
}

/// `<'flex-basis'>`: a width, or one of the basis keywords. A bare `0` is a flex factor
/// until two factors have already been seen (CSS Flexbox section 7.1).
bool flex_basis_token(const std::string& token) {
    const std::string lower = lower_copy(token);
    if (lower == "auto" || lower == "content" || lower == "min-content" ||
        lower == "max-content" || lower == "fit-content") {
        return true;
    }
    if (lower.rfind("fit-content(", 0) == 0) {
        return true;
    }
    return looks_like_length_pct(lower) && !flex_number(lower, nullptr);
}

bool expand_flex_text(std::string_view value, Parts& out) {
    const std::string trimmed = trim_copy(value);
    if (const char* wide = wide_keyword_of(trimmed)) {
        return expand_wide_keyword("flex", wide, out);
    }
    if (lower_copy(trimmed) == "none") {
        add(out, "flex-grow", "0");
        add(out, "flex-shrink", "0");
        add(out, "flex-basis", "auto");
        return true;
    }
    // `none | [ <'flex-grow'> <'flex-shrink'>? || <'flex-basis'> ]`.
    // An omitted basis is `0%`, and an omitted grow or shrink is `1`.
    const std::vector<std::string> parts = tokens(trimmed);
    if (parts.empty() || parts.size() > 3) {
        return false;
    }
    std::string grow;
    std::string shrink;
    std::string basis;
    int factors = 0;
    for (const std::string& token : parts) {
        float number = 0.f;
        const bool is_number = flex_number(token, &number);
        if (flex_basis_token(token)) {
            if (!basis.empty()) {
                return false;
            }
            basis = token;
            continue;
        }
        // A unitless zero that already follows two factors is the basis (`flex: 1 1 0`).
        if (is_number && factors >= 2 && number == 0.f && basis.empty()) {
            basis = "0px";
            continue;
        }
        if (!is_number || factors >= 2) {
            return false;
        }
        if (factors == 0) {
            grow = token;
        } else {
            shrink = token;
        }
        ++factors;
    }
    if (basis.empty()) {
        basis = "0%";
    }
    if (grow.empty()) {
        grow = "1";
    }
    if (shrink.empty()) {
        shrink = "1";
    }
    add(out, "flex-grow", std::move(grow));
    add(out, "flex-shrink", std::move(shrink));
    add(out, "flex-basis", std::move(basis));
    return true;
}

}  // namespace shorthand_detail

}  // namespace css
}  // namespace zui
