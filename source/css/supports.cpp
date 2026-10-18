#include "css/values.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#include <lexbor/css/css.h>

#include "css/values_internal.h"

namespace zui {
namespace css {

namespace values_detail {

bool property_supported(std::string_view name) {
    if (name.size() >= 2 && name[0] == '-' && name[1] == '-') {
        return true;
    }
    static constexpr const char* kProps[] = {
        "display",
        "position",
        "top",
        "right",
        "bottom",
        "left",
        "inset",
        "width",
        "height",
        "min-width",
        "min-height",
        "max-width",
        "max-height",
        "margin",
        "margin-top",
        "margin-right",
        "margin-bottom",
        "margin-left",
        "padding",
        "padding-top",
        "padding-right",
        "padding-bottom",
        "padding-left",
        "border",
        "border-width",
        "border-style",
        "border-color",
        "border-radius",
        "border-top-left-radius",
        "border-top-right-radius",
        "border-bottom-right-radius",
        "border-bottom-left-radius",
        "box-sizing",
        "overflow",
        "overflow-x",
        "overflow-y",
        "visibility",
        "opacity",
        "color",
        "background",
        "background-color",
        "background-image",
        "background-size",
        "background-position",
        "background-repeat",
        "background-clip",
        "background-origin",
        "font",
        "font-family",
        "font-size",
        "font-weight",
        "font-style",
        "font-variant",
        "font-stretch",
        "font-kerning",
        "font-feature-settings",
        "font-size-adjust",
        "line-height",
        "letter-spacing",
        "word-spacing",
        "white-space",
        "word-break",
        "overflow-wrap",
        "word-wrap",
        "text-align",
        "text-indent",
        "text-transform",
        "text-decoration",
        "text-decoration-line",
        "text-decoration-style",
        "text-decoration-color",
        "text-decoration-thickness",
        "text-shadow",
        "text-overflow",
        "vertical-align",
        "writing-mode",
        "direction",
        "float",
        "clear",
        "z-index",
        "transform",
        "transform-origin",
        "perspective",
        "filter",
        "mask",
        "mask-image",
        "clip-path",
        "mix-blend-mode",
        "box-shadow",
        "outline",
        "outline-width",
        "outline-style",
        "outline-color",
        "outline-offset",
        "flex",
        "flex-direction",
        "flex-wrap",
        "flex-flow",
        "flex-grow",
        "flex-shrink",
        "flex-basis",
        "order",
        "justify-content",
        "align-items",
        "align-self",
        "align-content",
        "gap",
        "row-gap",
        "column-gap",
        "grid",
        "grid-template",
        "grid-template-columns",
        "grid-template-rows",
        "grid-template-areas",
        "grid-auto-columns",
        "grid-auto-rows",
        "grid-auto-flow",
        "grid-column",
        "grid-row",
        "grid-area",
        "justify-items",
        "justify-self",
        "place-items",
        "place-content",
        "place-self",
        "content",
        "counter-reset",
        "counter-increment",
        "list-style",
        "list-style-type",
        "list-style-position",
        "appearance",
        "cursor",
        "pointer-events",
        "user-select",
        "contain",
        "content-visibility",
        "contain-intrinsic-size",
        "aspect-ratio",
        "object-fit",
        "object-position",
        "transition",
        "transition-property",
        "transition-duration",
        "transition-delay",
        "transition-timing-function",
        "animation",
        "scrollbar-gutter",
        "empty-cells",
        "border-collapse",
        "border-spacing",
        "table-layout",
        "caption-side",
        "columns",
        "column-count",
        "column-width",
        "column-gap",
        "break-before",
        "break-after",
        "break-inside",
    };
    const std::string n = lower_copy(name);
    for (const char* p : kProps) {
        if (n == p) {
            return true;
        }
    }
    return false;
}

// CSS Conditional 4 `selector()`. Lexbor accepts a selector it can parse, including
// `:has()`, `:is()`, and `::before`, and rejects an unknown pseudo. That is the same
// answer Edge gives for the selectors pages use to gate a rule.
bool selector_supported(std::string_view selector) {
    selector = trim_view(selector);
    if (selector.empty()) {
        return false;
    }
    lxb_css_parser_t* parser = lxb_css_parser_create();
    if (parser == nullptr || lxb_css_parser_init(parser, nullptr) != LXB_STATUS_OK) {
        if (parser != nullptr) {
            lxb_css_parser_destroy(parser, true);
        }
        return false;
    }
    lxb_css_selector_list_t* list = lxb_css_selectors_parse(
        parser, reinterpret_cast<const lxb_char_t*>(selector.data()), selector.size());
    const bool ok = list != nullptr && list->first != nullptr && parser->status == LXB_STATUS_OK;
    lxb_css_parser_destroy(parser, true);
    return ok;
}

struct Supports {
    Scan sc;
    bool failed = false;

    bool selector_fn(bool& value);
    bool one_term(bool& value);
    bool in_parens(bool& value);
    bool condition(bool allow_or, bool& value);
};

// `selector(<complex-selector>)`. The closing paren is consumed. Strings keep their
// parentheses, so an attribute value cannot end the function early.
bool Supports::selector_fn(bool& value) {
    if (!sc.eat_ident("selector")) {
        failed = true;
        return false;
    }
    sc.skip();
    if (!sc.eat('(')) {
        failed = true;
        return false;
    }
    const size_t start = sc.i;
    int depth = 1;
    bool in_string = false;
    char quote = 0;
    while (sc.i < sc.s.size() && depth > 0) {
        const char c = sc.s[sc.i];
        if (in_string) {
            if (c == '\\' && sc.i + 1 < sc.s.size()) {
                sc.i += 2;
                continue;
            }
            if (c == quote) {
                in_string = false;
            }
            ++sc.i;
            continue;
        }
        if (c == '"' || c == '\'') {
            in_string = true;
            quote = c;
            ++sc.i;
            continue;
        }
        if (c == '(') {
            ++depth;
        } else if (c == ')') {
            --depth;
            if (depth == 0) {
                break;
            }
        }
        ++sc.i;
    }
    if (depth != 0) {
        failed = true;
        return false;
    }
    const std::string_view inner = sc.s.substr(start, sc.i - start);
    if (!sc.eat(')')) {
        failed = true;
        return false;
    }
    value = selector_supported(inner);
    return true;
}

// A supports-in-parens is either a parenthesized condition / declaration, or
// `selector(...)` with no extra parentheses.
bool Supports::one_term(bool& value) {
    sc.skip();
    if (sc.ident_is("selector")) {
        return selector_fn(value);
    }
    return in_parens(value);
}

bool Supports::in_parens(bool& value) {
    if (!sc.eat('(')) {
        failed = true;
        return false;
    }
    sc.skip();
    if (sc.ident_is("not") || (sc.i < sc.s.size() && sc.s[sc.i] == '(')) {
        if (!condition(true, value) || !sc.eat(')')) {
            failed = true;
            return false;
        }
        return true;
    }
    // `(selector(...))` is a parenthesized supports-condition whose only term is
    // the selector function.
    if (sc.ident_is("selector")) {
        if (!selector_fn(value) || !sc.eat(')')) {
            failed = true;
            return false;
        }
        return true;
    }
    std::string name;
    if (!sc.read_ident(name)) {
        failed = true;
        return false;
    }
    sc.skip();
    if (!sc.eat(':')) {
        failed = true;
        return false;
    }
    sc.skip();
    const size_t vs = sc.i;
    int depth = 1;
    while (sc.i < sc.s.size() && depth > 0) {
        if (sc.s[sc.i] == '(') {
            ++depth;
        } else if (sc.s[sc.i] == ')') {
            --depth;
            if (depth == 0) {
                break;
            }
        }
        ++sc.i;
    }
    if (depth != 0) {
        failed = true;
        return false;
    }
    std::string_view raw = sc.s.substr(vs, sc.i - vs);
    while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.back()))) {
        raw.remove_suffix(1);
    }
    if (!sc.eat(')')) {
        failed = true;
        return false;
    }
    value = property_supported(name) && !raw.empty();
    return true;
}

bool Supports::condition(bool allow_or, bool& value) {
    sc.skip();
    if (sc.eat_ident("not")) {
        bool inner = false;
        if (!one_term(inner)) {
            failed = true;
            return false;
        }
        sc.skip();
        if (sc.ident_is("and") || sc.ident_is("or")) {
            failed = true;
            return false;
        }
        value = !inner;
        return true;
    }
    bool acc = false;
    if (!one_term(acc)) {
        failed = true;
        return false;
    }
    sc.skip();
    if (sc.ident_is("and")) {
        while (sc.eat_ident("and")) {
            bool next = false;
            if (!one_term(next)) {
                failed = true;
                return false;
            }
            acc = acc && next;
            sc.skip();
        }
        if (sc.ident_is("or")) {
            failed = true;
            return false;
        }
        value = acc;
        return true;
    }
    if (sc.ident_is("or")) {
        if (!allow_or) {
            failed = true;
            return false;
        }
        while (sc.eat_ident("or")) {
            bool next = false;
            if (!one_term(next)) {
                failed = true;
                return false;
            }
            acc = acc || next;
            sc.skip();
        }
        value = acc;
        return true;
    }
    value = acc;
    return true;
}


}  // namespace values_detail

using namespace values_detail;

bool supports_matches(std::string_view condition) {
    Supports s{{condition, 0}, false};
    if (s.sc.eof()) {
        return true;
    }
    bool value = false;
    if (!s.condition(true, value) || s.failed || !s.sc.eof()) {
        return false;
    }
    return value;
}
}  // namespace css
}  // namespace zui
