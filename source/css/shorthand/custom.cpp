// The deferred / custom-property shorthand expansion: a raw specified value split into the
// longhands the shorthand covers.

#include "css/shorthand/shorthand.h"
#include "css/shorthand/internal.h"

#include "css/values.h"

#include <lexbor/css/css.h>

#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zui {
namespace css {

using namespace shorthand_detail;

namespace shorthand_detail {

// CSS Backgrounds 3 §5.7. Omitted longhands reset to their initial values.
// `source || slice [ / width | / width? / outset ]? || repeat`, plus the `fill` keyword on slice.
bool expand_border_image(std::string_view value, Parts& out) {
    const std::vector<std::string> tok = tokens_with_slash(value);
    std::vector<std::string> head;
    std::vector<std::string> width;
    std::vector<std::string> outset;
    int slash = 0;
    std::vector<std::string>* cur = &head;
    for (const std::string& t : tok) {
        if (t == "/") {
            ++slash;
            if (slash == 1) {
                cur = &width;
            } else if (slash == 2) {
                cur = &outset;
            } else {
                return false;
            }
            continue;
        }
        cur->push_back(t);
    }
    auto is_repeat = [](const std::string& l) {
        return l == "stretch" || l == "repeat" || l == "round" || l == "space";
    };
    auto is_number_or_percent = [](const std::string& t) {
        if (t.empty() || t[0] == '-' || t[0] == '+') {
            return false;
        }
        std::size_t i = 0;
        bool digit = false;
        bool dot = false;
        for (; i < t.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(t[i]);
            if (std::isdigit(c)) {
                digit = true;
            } else if (t[i] == '.' && !dot) {
                dot = true;
            } else {
                break;
            }
        }
        if (!digit) {
            return false;
        }
        if (i == t.size()) {
            return true;
        }
        return t[i] == '%' && i + 1 == t.size();
    };
    auto is_width = [&](const std::string& t) {
        const std::string l = lower_copy(t);
        if (l == "auto" || is_number_or_percent(l)) {
            return true;
        }
        std::size_t i = 0;
        bool digit = false;
        bool dot = false;
        for (; i < l.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(l[i]);
            if (std::isdigit(c)) {
                digit = true;
            } else if (l[i] == '.' && !dot) {
                dot = true;
            } else {
                break;
            }
        }
        if (!digit || i >= l.size()) {
            return false;
        }
        const std::string unit = l.substr(i);
        return unit == "px" || unit == "em" || unit == "rem";
    };
    // `repeat` is `||` with the slice/width/outset group, so a keyword may sit after a slash
    // (`url(...) 20 / 20px stretch`). Pull those out before width and outset are validated.
    std::vector<std::string> repeats;
    auto take_repeats = [&](std::vector<std::string>& bucket) {
        std::vector<std::string> kept;
        kept.reserve(bucket.size());
        for (const std::string& t : bucket) {
            const std::string l = lower_copy(t);
            if (!is_repeat(l)) {
                kept.push_back(t);
                continue;
            }
            if (repeats.size() >= 2) {
                return false;
            }
            repeats.push_back(l);
        }
        bucket.swap(kept);
        return true;
    };
    if (!take_repeats(head) || !take_repeats(width) || !take_repeats(outset)) {
        return false;
    }
    std::string source = "none";
    bool saw_source = false;
    bool fill = false;
    std::vector<std::string> slices;
    for (const std::string& t : head) {
        const std::string l = lower_copy(t);
        if (is_repeat(l)) {
            if (repeats.size() >= 2) {
                return false;
            }
            repeats.push_back(l);
            continue;
        }
        if (l == "fill") {
            if (fill) {
                return false;
            }
            fill = true;
            continue;
        }
        if (l == "none" || t.find('(') != std::string::npos) {
            if (saw_source) {
                return false;
            }
            saw_source = true;
            source = t;
            continue;
        }
        if (!is_number_or_percent(t) || slices.size() >= 4) {
            return false;
        }
        slices.push_back(t);
    }
    if (width.size() > 4 || outset.size() > 4) {
        return false;
    }
    for (const std::string& t : width) {
        if (!is_width(t)) {
            return false;
        }
    }
    for (const std::string& t : outset) {
        const std::string l = lower_copy(t);
        if (l == "auto" || (!l.empty() && l.back() == '%') || !is_width(t)) {
            return false;
        }
    }
    auto join = [](const std::vector<std::string>& parts) {
        std::string s;
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (i != 0) {
                s.push_back(' ');
            }
            s += parts[i];
        }
        return s;
    };
    std::string slice = slices.empty() ? std::string("100%") : join(slices);
    if (fill) {
        slice.push_back(' ');
        slice += "fill";
    }
    std::string repeat = "stretch";
    if (repeats.size() == 1) {
        repeat = repeats[0];
    } else if (repeats.size() == 2) {
        repeat = repeats[0] + " " + repeats[1];
    }
    add(out, "border-image-source", std::move(source));
    add(out, "border-image-slice", std::move(slice));
    add(out, "border-image-width", width.empty() ? std::string("1") : join(width));
    add(out, "border-image-outset", outset.empty() ? std::string("0") : join(outset));
    add(out, "border-image-repeat", std::move(repeat));
    return true;
}

bool expand_custom(std::string name, std::string_view value, Parts& out) {
    name = lower_copy(std::move(name));
    // CSS Values 4 section 3.2 / Cascading 5 section 2: a shorthand whose value is a CSS-wide
    // keyword takes that keyword for every longhand it expands to (`font: inherit` inherits all
    // ten font longhands, `overflow: unset` unsets both axes). Without this branch the keyword
    // is a component value no shorthand below can use, every branch returns false and the whole
    // declaration is dropped - so the author's `button { font: inherit }` lost to the UA's
    // `font: 400 13.3333px Arial` and every button in the page kept the UA face and size.
    if (const char* keyword = wide_keyword_of(value); keyword != nullptr) {
        return expand_wide_keyword(name, keyword, out);
    }
    const auto tok = tokens(value);
    auto sides = [&](const char* a, const char* b, const char* c, const char* d) {
        std::string got[4];
        const int n = std::min(4, static_cast<int>(tok.size()));
        for (int i = 0; i < n; ++i) {
            got[i] = tok[static_cast<size_t>(i)];
        }
        std::string box[4];
        distribute4(got, n == 0 ? 0 : n, box);
        add(out, a, box[0]);
        add(out, b, box[1]);
        add(out, c, box[2]);
        add(out, d, box[3]);
    };
    if (name == "border-radius") {
        // CSS Backgrounds 3: 1–4 horizontal radii, optional `/` 1–4 vertical radii.
        // `tokens()` keeps `1px/2px` as one token, so split on top-level `/` first.
        const auto slash = split_slash(value);
        const auto horiz = tokens(trim_copy(slash[0]));
        const auto vert_tok =
            slash.size() >= 2 ? tokens(trim_copy(slash[1])) : std::vector<std::string>{};
        std::string got_h[4];
        std::string got_v[4];
        const int nh = std::min(4, static_cast<int>(horiz.size()));
        const int nv = std::min(4, static_cast<int>(vert_tok.size()));
        for (int i = 0; i < nh; ++i) {
            got_h[i] = horiz[static_cast<size_t>(i)];
        }
        for (int i = 0; i < nv; ++i) {
            got_v[i] = vert_tok[static_cast<size_t>(i)];
        }
        std::string box_h[4];
        distribute4(got_h, nh == 0 ? 0 : nh, box_h);
        if (nv > 0) {
            std::string box_v[4];
            distribute4(got_v, nv, box_v);
            add(out, "border-top-left-radius", box_h[0] + " " + box_v[0]);
            add(out, "border-top-right-radius", box_h[1] + " " + box_v[1]);
            add(out, "border-bottom-right-radius", box_h[2] + " " + box_v[2]);
            add(out, "border-bottom-left-radius", box_h[3] + " " + box_v[3]);
        } else {
            add(out, "border-top-left-radius", box_h[0]);
            add(out, "border-top-right-radius", box_h[1]);
            add(out, "border-bottom-right-radius", box_h[2]);
            add(out, "border-bottom-left-radius", box_h[3]);
        }
        return true;
    }
    if (name == "border-width") {
        sides("border-top-width", "border-right-width", "border-bottom-width", "border-left-width");
        return true;
    }
    if (name == "border-style") {
        sides("border-top-style", "border-right-style", "border-bottom-style", "border-left-style");
        return true;
    }
    if (name == "border-color") {
        sides("border-top-color", "border-right-color", "border-bottom-color", "border-left-color");
        return true;
    }
    if (name == "overflow") {
        const std::string x = tok.empty() ? "visible" : tok[0];
        const std::string y = tok.size() >= 2 ? tok[1] : x;
        add(out, "overflow-x", x);
        add(out, "overflow-y", y);
        return true;
    }
    if (name == "grid-template" || name == "grid") {
        return expand_grid_template(name == "grid", value, out);
    }
    if (name == "gap") {
        const std::string row = tok.empty() ? "0" : tok[0];
        const std::string col = tok.size() >= 2 ? tok[1] : row;
        add(out, "row-gap", row);
        add(out, "column-gap", col);
        return true;
    }
    if (name == "place-items" || name == "place-content" || name == "place-self") {
        // CSS Box Alignment section 5: `place-*` is `<align> <justify>?`, and a single value sets
        // both axes. Dropping it left every `.num { place-items: center }` (the calendar's date
        // circle) with the initial `normal`, which stretches a grid item instead of centring it -
        // and that moved the container's synthesized baseline by the centring offset.
        const bool items = name == "place-items";
        const bool content = name == "place-content";
        const std::string first = tok.empty() ? "normal" : tok[0];
        const std::string second = tok.size() >= 2 ? tok[1] : first;
        add(out, items ? "align-items" : (content ? "align-content" : "align-self"), first);
        add(out, items ? "justify-items" : (content ? "justify-content" : "justify-self"), second);
        return true;
    }
    if (name == "inset") {
        sides("top", "right", "bottom", "left");
        return true;
    }
    if (name == "margin") {
        sides("margin-top", "margin-right", "margin-bottom", "margin-left");
        return true;
    }
    if (name == "padding") {
        sides("padding-top", "padding-right", "padding-bottom", "padding-left");
        return true;
    }
    if (name == "list-style") {
        // CSS Lists 3 §3.6. `none` is ambiguous: it applies to whichever of type/image is not
        // otherwise set. Position keywords are only `inside` / `outside`. A bare `none` clears
        // both type and image (needed by `.md ul.task-list { list-style: none }`).
        // Omitted longhands take their initial values (Cascading: a shorthand resets all of them).
        std::string type;
        std::string position;
        std::string image;
        bool saw_none = false;
        for (const std::string& t : tok) {
            if (t == "inside" || t == "outside") {
                position = t;
            } else if (t == "none") {
                saw_none = true;
            } else if (t.rfind("url(", 0) == 0) {
                image = t;
            } else if (type.empty()) {
                type = t;
            }
        }
        if (saw_none) {
            if (type.empty()) {
                type = "none";
            }
            if (image.empty()) {
                image = "none";
            }
        }
        if (type.empty()) {
            type = "disc";
        }
        if (position.empty()) {
            position = "outside";
        }
        if (image.empty()) {
            image = "none";
        }
        add(out, "list-style-type", type);
        add(out, "list-style-position", position);
        add(out, "list-style-image", image);
        return true;
    }
    auto border_side = [&](const char* side) {
        std::string width = "medium";
        std::string style = "none";
        std::string color = "currentcolor";
        bool saw_width = false;
        for (const std::string& t : tok) {
            const std::string l = lower_copy(t);
            const bool style_kw = l == "none" || l == "hidden" || l == "dotted" || l == "dashed" ||
                                  l == "solid" || l == "double" || l == "groove" || l == "ridge" ||
                                  l == "inset" || l == "outset";
            const bool width_kw = l == "thin" || l == "medium" || l == "thick";
            const bool width_len =
                !l.empty() &&
                (std::isdigit(static_cast<unsigned char>(l[0])) || l[0] == '.' || l[0] == '+' ||
                 l[0] == '-' || l.rfind("calc(", 0) == 0 || l.rfind("min(", 0) == 0 ||
                 l.rfind("max(", 0) == 0 || l.rfind("clamp(", 0) == 0);
            if (style_kw) {
                style = t;
            } else if (!saw_width && (width_kw || width_len)) {
                width = t;
                saw_width = true;
            } else {
                color = t;
            }
        }
        add(out, (std::string("border-") + side + "-width").c_str(), width);
        add(out, (std::string("border-") + side + "-style").c_str(), style);
        add(out, (std::string("border-") + side + "-color").c_str(), color);
    };
    if (name == "border") {
        border_side("top");
        border_side("right");
        border_side("bottom");
        border_side("left");
        return true;
    }
    if (name == "border-top" || name == "border-right" || name == "border-bottom" ||
        name == "border-left") {
        border_side(name.c_str() + 7);
        return true;
    }
    if (name == "outline") {
        // CSS UI 4 section 2.4: the outline shorthand resets width/style/color; the initial style is
        // `none`, so `outline: 2px red` paints nothing on its own.
        std::string width = "medium";
        std::string style = "none";
        std::string color = "currentcolor";
        bool saw_width = false;
        bool saw_style = false;
        for (const std::string& t : tok) {
            const std::string l = lower_copy(t);
            const bool style_kw = l == "auto" || l == "none" || l == "dotted" || l == "dashed" ||
                                  l == "solid" || l == "double" || l == "groove" || l == "ridge" ||
                                  l == "inset" || l == "outset";
            const bool width_kw = l == "thin" || l == "medium" || l == "thick";
            const bool width_len =
                !l.empty() &&
                (std::isdigit(static_cast<unsigned char>(l[0])) || l[0] == '.' || l[0] == '+' ||
                 l[0] == '-' || l.rfind("calc(", 0) == 0 || l.rfind("min(", 0) == 0 ||
                 l.rfind("max(", 0) == 0 || l.rfind("clamp(", 0) == 0);
            if (style_kw && !saw_style) {
                style = t;
                saw_style = true;
            } else if (!saw_width && (width_kw || width_len)) {
                width = t;
                saw_width = true;
            } else {
                color = t;
            }
        }
        add(out, "outline-width", width);
        add(out, "outline-style", style);
        add(out, "outline-color", color);
        return true;
    }
    if (name == "background") {
        // CSS Backgrounds 3 §2.10: reset every longhand, then assign. Multi-layer stores the
        // topmost non-none image layer's geometry plus the final layer's colour (paint still
        // draws one image). Unknown tokens / colour on a non-final layer → whole value invalid.
        return expand_background(value, out);
    }
    if (name == "font") {
        // CSS Fonts 3/4 §3.7: `font` is a shorthand for style/variant/weight/stretch/size/
        // line-height/family, and also resets font-size-adjust, font-kerning, font-feature-settings
        // (and the font-variant longhands via `font-variant`). Omitted pieces take their initial
        // values. System fonts set the whole face at once.
        auto reset_font_extras = [&]() {
            add(out, "font-size-adjust", "none");
            add(out, "font-kerning", "auto");
            add(out, "font-feature-settings", "normal");
        };
        if (tok.size() == 1) {
            const std::string sys = lower_copy(tok[0]);
            if (sys == "caption" || sys == "icon" || sys == "menu" || sys == "message-box" ||
                sys == "small-caption" || sys == "status-bar") {
                // System fonts may only be set as a whole. Map to a stable UI face; pieces the
                // host does not expose fall back to the longhand initials (CSS Fonts 3 §3.7).
                const bool compact = sys == "small-caption" || sys == "status-bar";
                add(out, "font-style", "normal");
                add(out, "font-variant", "normal");
                add(out, "font-weight", "normal");
                add(out, "font-stretch", "normal");
                add(out, "font-size", compact ? "12px" : "16px");
                add(out, "line-height", "normal");
                add(out, "font-family", "system-ui");
                reset_font_extras();
                return true;
            }
        }
        auto is_stretch_kw = [](const std::string& l) {
            return l == "ultra-condensed" || l == "extra-condensed" || l == "condensed" ||
                   l == "semi-condensed" || l == "semi-expanded" || l == "expanded" ||
                   l == "extra-expanded" || l == "ultra-expanded";
        };
        auto is_size_token = [&](const std::string& head) {
            const std::string l = lower_copy(head);
            if (l == "xx-small" || l == "x-small" || l == "small" || l == "medium" ||
                l == "large" || l == "x-large" || l == "xx-large" || l == "xxx-large" ||
                l == "smaller" || l == "larger") {
                return true;
            }
            if (l.empty()) {
                return false;
            }
            const unsigned char c0 = static_cast<unsigned char>(l[0]);
            if (!(std::isdigit(c0) || c0 == '.')) {
                return false;
            }
            // Absolute / relative length or percentage; bare numbers are not a font-size.
            return looks_like_length_pct(l);
        };
        int size_at = -1;
        std::string size_tok;
        std::string line_tok;
        for (int i = 0; i < static_cast<int>(tok.size()); ++i) {
            const std::string raw = tok[static_cast<size_t>(i)];
            const auto slash = raw.find('/');
            const std::string head = slash == std::string::npos ? raw : raw.substr(0, slash);
            if (is_size_token(head)) {
                size_at = i;
                size_tok = head;
                if (slash != std::string::npos) {
                    line_tok = raw.substr(slash + 1);
                }
                break;
            }
        }
        if (size_at < 0) {
            return false;
        }
        std::string style = "normal";
        std::string variant = "normal";
        std::string weight = "normal";
        std::string stretch = "normal";
        for (int i = 0; i < size_at; ++i) {
            const std::string raw = tok[static_cast<size_t>(i)];
            const std::string l = lower_copy(raw);
            if (l == "italic" || l == "oblique") {
                style = raw;
            } else if (l == "small-caps") {
                // Only the CSS 2.1 font-variant values are allowed in the shorthand.
                variant = raw;
            } else if (l == "bold" || l == "bolder" || l == "lighter") {
                weight = raw;
            } else if (!l.empty() && std::isdigit(static_cast<unsigned char>(l[0]))) {
                // CSS Fonts 3: 100–900. Reject tokens that look like a length (those are size).
                bool all_digit = true;
                for (char c : l) {
                    if (!std::isdigit(static_cast<unsigned char>(c))) {
                        all_digit = false;
                        break;
                    }
                }
                if (all_digit && l.size() >= 3 && l.size() <= 4) {
                    weight = raw;
                }
            } else if (is_stretch_kw(l)) {
                stretch = raw;
            }
            // Bare `normal` is ambiguous across style/variant/weight/stretch; leaving the
            // corresponding longhand at its initial value is already correct.
        }
        add(out, "font-style", style);
        add(out, "font-variant", variant);
        add(out, "font-weight", weight);
        add(out, "font-stretch", stretch);
        add(out, "font-size", size_tok);
        size_t next = static_cast<size_t>(size_at) + 1;
        if (!line_tok.empty()) {
            add(out, "line-height", line_tok);
        } else if (next < tok.size() && tok[next] == "/" && next + 1 < tok.size()) {
            add(out, "line-height", tok[next + 1]);
            next += 2;
        } else {
            // The `font` shorthand resets `line-height` to `normal` when the value omits it.
            add(out, "line-height", "normal");
        }
        std::string family;
        for (size_t i = next; i < tok.size(); ++i) {
            if (!family.empty()) {
                family.push_back(' ');
            }
            family += tok[i];
        }
        if (family.empty()) {
            return false;
        }
        add(out, "font-family", family);
        reset_font_extras();
        return true;
    }
    if (name == "border-image") {
        return expand_border_image(value, out);
    }
    if (name == "container") {
        // CSS Containment 3: `<'container-name'> [ / <'container-type'> ]?`. A shorthand resets
        // every longhand, so a bare name sets `container-type` back to `normal`. `none` is the
        // name keyword, not a custom ident, and only when it is the whole name.
        const auto slash = split_slash(value);
        if (slash.empty() || slash.size() > 2) {
            return false;
        }
        const auto name_toks = tokens(trim_copy(slash[0]));
        if (name_toks.empty()) {
            return false;
        }
        std::string cname;
        if (name_toks.size() == 1 && lower_copy(name_toks[0]) == "none") {
            cname = "none";
        } else {
            for (const std::string& t : name_toks) {
                const std::string low = lower_copy(t);
                if (low == "none" || low == "inherit" || low == "initial" || low == "unset" ||
                    low == "revert" || low == "revert-layer" || low == "default") {
                    return false;
                }
                if (!cname.empty()) {
                    cname.push_back(' ');
                }
                cname += t;
            }
        }
        std::string ctype = "normal";
        if (slash.size() == 2) {
            const auto type_toks = tokens(trim_copy(slash[1]));
            if (type_toks.size() != 1) {
                return false;
            }
            const std::string low = lower_copy(type_toks[0]);
            if (low != "normal" && low != "size" && low != "inline-size") {
                return false;
            }
            ctype = low;
        }
        add(out, "container-name", std::move(cname));
        add(out, "container-type", std::move(ctype));
        return true;
    }
    return false;
}

}  // namespace shorthand_detail

}  // namespace css
}  // namespace zui
