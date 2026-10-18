// lexbor component values serialized back to CSS text, the shorthand -> longhand table, and
// the CSS-wide keyword expansion every shorthand shares.

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

struct Buf {
    std::string s;
};

lxb_status_t append_cb(const lxb_char_t* data, size_t len, void* ctx) {
    static_cast<Buf*>(ctx)->s.append(reinterpret_cast<const char*>(data), len);
    return LXB_STATUS_OK;
}

template <typename Fn, typename T> std::string ser_cb(Fn fn, const T* value) {
    Buf b;
    fn(value, append_cb, &b);
    return b.s;
}

std::string ser_length(const lxb_css_value_length_percentage_t& lp) {
    if (lp.type == LXB_CSS_VALUE__UNDEF) {
        return "0";
    }
    // `margin: 0 auto` stores AUTO in the length-percentage slot. Serialize it as the
    // keyword; falling through to the empty-string fallback used to turn auto into 0 and
    // killed `margin: 0 auto` centering for `.flow`.
    if (lp.type == LXB_CSS_VALUE_AUTO) {
        return "auto";
    }
    std::string s = ser_cb(lxb_css_value_length_percentage_sr, &lp);
    return s.empty() ? "0" : s;
}

std::string ser_border_width(const lxb_css_value_length_type_t& w) {
    if (w.type == LXB_CSS_VALUE__UNDEF) {
        return "medium";
    }
    std::string s = ser_cb(lxb_css_value_length_type_sr, &w);
    return s.empty() ? "medium" : s;
}

std::string ser_ident(lxb_css_value_type_t type, const char* fallback) {
    if (type == LXB_CSS_VALUE__UNDEF) {
        return fallback;
    }
    Buf b;
    if (lxb_css_value_serialize(type, append_cb, &b) != LXB_STATUS_OK || b.s.empty()) {
        return fallback;
    }
    return b.s;
}

std::string ser_color(const lxb_css_value_color_t& c) {
    if (c.type == LXB_CSS_VALUE__UNDEF || c.type == LXB_CSS_COLOR_CURRENTCOLOR) {
        return "currentcolor";
    }
    std::string s = ser_cb(lxb_css_value_color_serialize, &c);
    return s.empty() ? "currentcolor" : s;
}

void add(Parts& out, const char* name, std::string value) {
    out.emplace_back(name, std::move(value));
}

const std::vector<const char*>* longhands_of(const std::string& name) {
    static const std::vector<const char*> kMargin{"margin-top", "margin-right", "margin-bottom",
                                                  "margin-left"};
    static const std::vector<const char*> kPadding{"padding-top", "padding-right", "padding-bottom",
                                                   "padding-left"};
    static const std::vector<const char*> kBorder{
        "border-top-width", "border-right-width", "border-bottom-width", "border-left-width",
        "border-top-style", "border-right-style", "border-bottom-style", "border-left-style",
        "border-top-color", "border-right-color", "border-bottom-color", "border-left-color"};
    static const std::vector<const char*> kBorderTop{"border-top-width", "border-top-style",
                                                     "border-top-color"};
    static const std::vector<const char*> kBorderRight{"border-right-width", "border-right-style",
                                                       "border-right-color"};
    static const std::vector<const char*> kBorderBottom{
        "border-bottom-width", "border-bottom-style", "border-bottom-color"};
    static const std::vector<const char*> kBorderLeft{"border-left-width", "border-left-style",
                                                      "border-left-color"};
    static const std::vector<const char*> kBorderWidth{"border-top-width", "border-right-width",
                                                       "border-bottom-width", "border-left-width"};
    static const std::vector<const char*> kBorderStyle{"border-top-style", "border-right-style",
                                                       "border-bottom-style", "border-left-style"};
    static const std::vector<const char*> kBorderColor{"border-top-color", "border-right-color",
                                                       "border-bottom-color", "border-left-color"};
    static const std::vector<const char*> kOverflow{"overflow-x", "overflow-y"};
    static const std::vector<const char*> kGap{"row-gap", "column-gap"};
    static const std::vector<const char*> kInset{"top", "right", "bottom", "left"};
    static const std::vector<const char*> kBackground{
        "background-color", "background-image",  "background-repeat", "background-position",
        "background-size",  "background-origin", "background-clip",   "background-attachment"};
    static const std::vector<const char*> kFont{
        "font-style",  "font-variant", "font-weight",      "font-stretch", "font-size",
        "line-height", "font-family",  "font-size-adjust", "font-kerning", "font-feature-settings"};
    static const std::vector<const char*> kBorderRadius{
        "border-top-left-radius", "border-top-right-radius", "border-bottom-right-radius",
        "border-bottom-left-radius"};
    static const std::vector<const char*> kBorderImage{
        "border-image-source", "border-image-slice", "border-image-width", "border-image-outset",
        "border-image-repeat"};
    static const std::vector<const char*> kGridTemplate{"grid-template-areas", "grid-template-rows",
                                                        "grid-template-columns"};
    static const std::vector<const char*> kGrid{"grid-template-areas",   "grid-template-rows",
                                                "grid-template-columns", "grid-auto-rows",
                                                "grid-auto-columns",     "grid-auto-flow"};
    static const std::vector<const char*> kListStyle{"list-style-type", "list-style-position",
                                                     "list-style-image"};
    static const std::vector<const char*> kOutline{"outline-width", "outline-style",
                                                   "outline-color"};
    static const std::vector<const char*> kFlex{"flex-grow", "flex-shrink", "flex-basis"};
    static const std::vector<const char*> kFlexFlow{"flex-direction", "flex-wrap"};
    static const std::vector<const char*> kContainer{"container-name", "container-type"};
    // CSS Cascade 4 §3.2: `all` resets every property except `direction` and `unicode-bidi`.
    // The names are the longhands this engine stores. A name it never reads is a no-op revert.
    static const std::vector<const char*> kAll{
        "align-content", "align-items", "align-self", "appearance", "aspect-ratio",
        "accent-color", "animation-delay", "animation-direction", "animation-duration",
        "animation-fill-mode", "animation-iteration-count", "animation-name",
        "animation-play-state", "animation-timing-function", "backdrop-filter",
        "backface-visibility", "background-attachment", "background-clip", "background-color",
        "background-image", "background-origin", "background-position", "background-repeat",
        "background-size", "border-bottom-color", "border-bottom-left-radius",
        "border-bottom-right-radius", "border-bottom-style", "border-bottom-width",
        "border-collapse", "border-image-outset", "border-image-repeat", "border-image-slice",
        "border-image-source", "border-image-width", "border-left-color", "border-left-style",
        "border-left-width",
        "border-right-color", "border-right-style", "border-right-width", "border-spacing",
        "border-top-color", "border-top-left-radius", "border-top-right-radius",
        "border-top-style", "border-top-width", "bottom", "box-decoration-break", "box-shadow",
        "box-sizing", "break-after", "break-before", "break-inside", "caption-side",
        "caret-color", "clear", "clip-path", "color", "color-scheme", "column-count",
        "column-fill", "column-gap", "column-rule-color", "column-rule-style",
        "column-rule-width", "column-span", "column-width", "columns", "contain",
        "contain-intrinsic-height", "contain-intrinsic-width", "container-name",
        "container-type", "content", "content-visibility", "cursor", "display", "empty-cells",
        "filter", "flex-basis", "flex-direction", "flex-grow", "flex-shrink", "flex-wrap",
        "float", "font-family", "font-feature-settings", "font-kerning", "font-size",
        "font-size-adjust", "font-stretch", "font-style", "font-variant", "font-weight",
        "grid-area", "grid-auto-columns", "grid-auto-flow", "grid-auto-rows",
        "grid-column-end", "grid-column-start", "grid-row-end", "grid-row-start",
        "grid-template-areas", "grid-template-columns", "grid-template-rows", "height",
        "hyphens", "image-rendering", "isolation", "justify-content", "justify-items",
        "justify-self", "left", "letter-spacing", "line-break", "line-height",
        "list-style-image", "list-style-position", "list-style-type", "margin-bottom",
        "margin-left", "margin-right", "margin-top", "max-height", "max-width", "min-height",
        "min-width", "mix-blend-mode", "object-fit", "object-position", "offset-distance",
        "offset-path", "offset-rotate", "opacity", "order", "orphans", "outline-color",
        "outline-offset", "outline-style", "outline-width", "overflow-anchor", "overflow-wrap",
        "overflow-x", "overflow-y", "overscroll-behavior-x", "overscroll-behavior-y",
        "padding-bottom", "padding-left", "padding-right", "padding-top", "perspective",
        "perspective-origin", "pointer-events", "position", "print-color-adjust", "quotes",
        "resize", "right", "row-gap", "scroll-snap-align", "scroll-snap-type",
        "scrollbar-color", "scrollbar-gutter", "scrollbar-width", "shape-margin",
        "shape-outside", "tab-size", "table-layout", "text-align", "text-align-last",
        "text-combine-upright", "text-decoration", "text-decoration-color",
        "text-decoration-line", "text-decoration-style", "text-decoration-thickness",
        "text-emphasis-color", "text-emphasis-position", "text-emphasis-style", "text-indent",
        "text-orientation", "text-overflow", "text-shadow", "text-transform",
        "text-underline-position", "top", "touch-action", "transform", "transform-origin",
        "transform-style", "transition-delay", "transition-duration", "transition-property",
        "transition-timing-function", "user-select", "vertical-align", "visibility",
        "white-space", "widows", "width", "will-change", "word-break", "word-spacing",
        "word-wrap", "writing-mode", "z-index"};

    if (name == "all")
        return &kAll;
    if (name == "margin")
        return &kMargin;
    if (name == "padding")
        return &kPadding;
    if (name == "border")
        return &kBorder;
    if (name == "border-top")
        return &kBorderTop;
    if (name == "border-right")
        return &kBorderRight;
    if (name == "border-bottom")
        return &kBorderBottom;
    if (name == "border-left")
        return &kBorderLeft;
    if (name == "border-width")
        return &kBorderWidth;
    if (name == "border-style")
        return &kBorderStyle;
    if (name == "border-color")
        return &kBorderColor;
    if (name == "overflow")
        return &kOverflow;
    if (name == "gap")
        return &kGap;
    if (name == "inset")
        return &kInset;
    if (name == "background")
        return &kBackground;
    if (name == "font")
        return &kFont;
    if (name == "border-radius")
        return &kBorderRadius;
    if (name == "border-image")
        return &kBorderImage;
    if (name == "grid-template")
        return &kGridTemplate;
    if (name == "grid")
        return &kGrid;
    if (name == "list-style")
        return &kListStyle;
    if (name == "outline")
        return &kOutline;
    if (name == "flex")
        return &kFlex;
    if (name == "flex-flow")
        return &kFlexFlow;
    if (name == "container")
        return &kContainer;
    return nullptr;
}

/// Writes `keyword` for every longhand of `name`. False when the name is not a shorthand this
/// file expands, which leaves the declaration to the longhand path.
bool expand_wide_keyword(const std::string& name, const char* keyword, Parts& out) {
    const std::vector<const char*>* longhands = longhands_of(name);
    if (longhands == nullptr) {
        return false;
    }
    for (const char* longhand : *longhands) {
        add(out, longhand, keyword);
    }
    return true;
}

std::string ser_number(const lxb_css_value_number_type_t& n, const char* fallback) {
    if (n.type == LXB_CSS_VALUE__UNDEF) {
        return fallback;
    }
    std::string s = ser_cb(lxb_css_property_flex_grow_serialize, &n);
    return s.empty() ? fallback : s;
}

std::string ser_basis(const lxb_css_property_flex_basis_t& basis) {
    if (basis.type == LXB_CSS_VALUE__UNDEF) {
        // CSS Flexbox section 7.1: omitting <flex-basis> from the `flex` shorthand makes its
        // specified value `0%`, not `0px`. The two differ the moment the flex container's main
        // size is indefinite: a percentage basis then defers to the item's content (section
        // 7.2.3), while a zero length stays zero. `flex: 1` is the shorthand everyone writes, so
        // serializing `0` here collapsed every auto-height column flex item (the kanban `.cards`
        // strip reported 48px where Chrome reports its 373.5px content height).
        return "0%";
    }
    std::string s = ser_cb(lxb_css_property_width_serialize, &basis);
    return s.empty() ? "0" : s;
}

}  // namespace shorthand_detail

}  // namespace css
}  // namespace zui
