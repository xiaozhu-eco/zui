#include <gtest/gtest.h>

#include "css/engine/engine.h"
#include "css/engine/nesting.h"
#include "css/shorthand/shorthand.h"
#include "css/values.h"
#include "html/document.h"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

const std::string* prop(const zui::html::Node* n, const char* key) {
    return n == nullptr ? nullptr : n->style(key);
}

bool eq(const zui::html::Node* n, const char* key, const char* expect) {
    const std::string* v = prop(n, key);
    if (v == nullptr || *v != expect) {
        ADD_FAILURE() << key << " = " << (v ? *v : std::string("<missing>")) << " want " << expect;
        return false;
    }
    return true;
}

}  // namespace

TEST(CssShorthand, Longhands) {
    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/shorthand.html", doc, error)) {
        FAIL() << error << "\n";
    }
    zui::css::Engine css;
    if (!css.load_sheets(doc, error)) {
        FAIL() << error << "\n";
    }
    css.cascade(doc);

    const zui::html::Node* b = zui::html::find(doc, "b");
    const zui::html::Node* over = zui::html::find(doc, "over");
    const zui::html::Node* m = zui::html::find(doc, "m");
    const zui::html::Node* p = zui::html::find(doc, "p");
    const zui::html::Node* f = zui::html::find(doc, "f");
    const zui::html::Node* ov = zui::html::find(doc, "ov");
    const zui::html::Node* g = zui::html::find(doc, "g");
    const zui::html::Node* bg = zui::html::find(doc, "bg");
    const zui::html::Node* bgimg = zui::html::find(doc, "bgimg");
    const zui::html::Node* bgboth = zui::html::find(doc, "bgboth");
    const zui::html::Node* bgpos = zui::html::find(doc, "bgpos");
    const zui::html::Node* bglayer = zui::html::find(doc, "bglayer");
    const zui::html::Node* bglayers = zui::html::find(doc, "bglayers");
    const zui::html::Node* bgtext = zui::html::find(doc, "bgtext");
    const zui::html::Node* bgtomato = zui::html::find(doc, "bgtomato");
    const zui::html::Node* bgr = zui::html::find(doc, "bgr");
    const zui::html::Node* radius_slash = zui::html::find(doc, "radius-slash");
    const zui::html::Node* mauto = zui::html::find(doc, "mauto");
    const zui::html::Node* mleft = zui::html::find(doc, "mleft");
    const zui::html::Node* fn = zui::html::find(doc, "fn");
    const zui::html::Node* fn_reset = zui::html::find(doc, "fn-reset");
    const zui::html::Node* fn_stretch = zui::html::find(doc, "fn-stretch");
    const zui::html::Node* fn_sys = zui::html::find(doc, "fn-sys");
    const zui::html::Node* in = zui::html::find(doc, "in");
    if (b == nullptr || over == nullptr || m == nullptr || p == nullptr || f == nullptr ||
        ov == nullptr || g == nullptr || bg == nullptr || bgimg == nullptr || bgboth == nullptr ||
        bgpos == nullptr || bglayer == nullptr || bglayers == nullptr || bgtext == nullptr ||
        bgtomato == nullptr ||
        bgr == nullptr || radius_slash == nullptr || mauto == nullptr || mleft == nullptr ||
        fn == nullptr || fn_reset == nullptr || fn_stretch == nullptr || fn_sys == nullptr ||
        in == nullptr) {
        FAIL() << "missing node\n";
    }
    if (prop(b, "border") != nullptr) {
        FAIL() << "border shorthand leaked into computed\n";
    }
    if (!eq(b, "border-top-width", "6px") || !eq(b, "border-right-width", "6px") ||
        !eq(b, "border-top-style", "solid") || !eq(b, "border-left-color", "#7cb7ff")) {
        FAIL() << "check 5 failed";
    }
    if (!eq(over, "border-top-color", "#00ff00") || !eq(over, "border-right-color", "red")) {
        FAIL() << "check 6 failed";
    }
    if (!eq(m, "margin-top", "1px") || !eq(m, "margin-right", "2px") ||
        !eq(m, "margin-bottom", "1px") || !eq(m, "margin-left", "2px")) {
        FAIL() << "check 7 failed";
    }
    if (!eq(p, "padding-top", "4px") || !eq(p, "padding-right", "5px") ||
        !eq(p, "padding-bottom", "6px") || !eq(p, "padding-left", "5px")) {
        FAIL() << "check 8 failed";
    }
    if (!eq(f, "flex-grow", "1") || !eq(f, "flex-shrink", "1")) {
        FAIL() << "check 9 failed";
    }
    if (!eq(ov, "overflow-x", "hidden") || !eq(ov, "overflow-y", "auto")) {
        FAIL() << "check 10 failed";
    }
    if (!eq(g, "row-gap", "4px") || !eq(g, "column-gap", "8px")) {
        FAIL() << "check 11 failed";
    }
    if (!eq(bg, "background-color", "#112233") || !eq(bg, "background-image", "none") ||
        !eq(bg, "background-repeat", "repeat") || !eq(bg, "background-position", "0% 0%") ||
        !eq(bg, "background-size", "auto") || !eq(bg, "background-origin", "padding-box") ||
        !eq(bg, "background-clip", "border-box") || !eq(bg, "background-attachment", "scroll")) {
        FAIL() << "check 12 failed";
    }
    if (!eq(bgimg, "background-image", "linear-gradient(165deg, #2f6d50, #173728)") ||
        !eq(bgimg, "background-color", "transparent")) {
        FAIL() << "check 12b failed";
    }
    if (!eq(bgboth, "background-image", "linear-gradient(to right, #000000, #ffffff)") ||
        !eq(bgboth, "background-color", "#112233")) {
        FAIL() << "check 12c failed";
    }
    if (!eq(bgpos, "background-position", "center") || !eq(bgpos, "background-size", "cover") ||
        !eq(bgpos, "background-repeat", "no-repeat")) {
        FAIL() << "check 12d failed";
    }
    if (!eq(bglayer, "background-image", "linear-gradient(to right, #000000, #ffffff)") ||
        !eq(bglayer, "background-color", "#eeeeee") ||
        !eq(bglayer, "background-position", "center") || !eq(bglayer, "background-size", "cover") ||
        !eq(bglayer, "background-repeat", "no-repeat")) {
        FAIL() << "check 12e failed";
    }
    if (!eq(bglayers, "background-image",
            "linear-gradient(#ff0000, #00ff00), linear-gradient(#0000ff, #ffffff)") ||
        !eq(bglayers, "background-position", "4px 6px, right bottom") ||
        !eq(bglayers, "background-size", "20px 10px, 8px 12px") ||
        !eq(bglayers, "background-repeat", "no-repeat, repeat-x") ||
        !eq(bglayers, "background-attachment", "fixed, scroll") ||
        !eq(bglayers, "background-origin", "padding-box, padding-box") ||
        !eq(bglayers, "background-clip", "border-box, border-box") ||
        !eq(bglayers, "background-color", "#abcdef")) {
        FAIL() << "check 12e2 multi-layer background failed";
    }
    if (!eq(bgtext, "background-clip", "text") || !eq(bgtext, "background-origin", "padding-box")) {
        FAIL() << "check 12f failed";
    }
    if (!eq(bgtomato, "background-color", "tomato") || !eq(bgtomato, "background-image", "none")) {
        FAIL() << "check 12g failed";
    }
    if (!eq(bgr, "background-color", "#cccccc") || !eq(bgr, "background-repeat", "repeat")) {
        FAIL() << "check 12h reset failed";
    }
    if (!eq(radius_slash, "border-top-left-radius", "1px 2px")) {
        FAIL() << "check 12i border-radius slash failed";
    }
    if (!eq(fn, "font-style", "italic") || !eq(fn, "font-weight", "700") ||
        !eq(fn, "font-size", "18px") || !eq(fn, "line-height", "1.4") ||
        !eq(fn, "font-family", "sans-serif") || !eq(fn, "font-variant", "normal") ||
        !eq(fn, "font-stretch", "normal")) {
        FAIL() << "check 13 failed";
    }
    // Omitted longhands of `font` reset to initial even when the parent set them.
    if (!eq(fn_reset, "font-style", "normal") || !eq(fn_reset, "font-variant", "normal") ||
        !eq(fn_reset, "font-weight", "normal") || !eq(fn_reset, "font-stretch", "normal") ||
        !eq(fn_reset, "font-size", "16px") || !eq(fn_reset, "line-height", "normal") ||
        !eq(fn_reset, "font-family", "sans-serif") || !eq(fn_reset, "font-kerning", "auto") ||
        !eq(fn_reset, "font-feature-settings", "normal") ||
        !eq(fn_reset, "font-size-adjust", "none")) {
        FAIL() << "check 13b font shorthand reset failed";
    }
    if (!eq(fn_stretch, "font-style", "italic") || !eq(fn_stretch, "font-variant", "small-caps") ||
        !eq(fn_stretch, "font-stretch", "condensed") || !eq(fn_stretch, "font-weight", "700") ||
        !eq(fn_stretch, "font-size", "14px") || !eq(fn_stretch, "line-height", "1.2") ||
        !eq(fn_stretch, "font-family", "Georgia, serif")) {
        FAIL() << "check 13c font stretch/variant failed";
    }
    if (!eq(fn_sys, "font-family", "system-ui") || !eq(fn_sys, "font-style", "normal") ||
        !eq(fn_sys, "font-variant", "normal") || !eq(fn_sys, "font-stretch", "normal") ||
        !eq(fn_sys, "line-height", "normal")) {
        FAIL() << "check 13d system font failed";
    }
    if (!eq(in, "top", "10px") || !eq(in, "right", "20px") || !eq(in, "bottom", "10px") ||
        !eq(in, "left", "20px")) {
        FAIL() << "check 14 failed";
    }
    if (!eq(mauto, "margin-top", "0") || !eq(mauto, "margin-right", "auto") ||
        !eq(mauto, "margin-bottom", "0") || !eq(mauto, "margin-left", "auto")) {
        FAIL() << "check 15 failed";
    }
    if (!eq(mleft, "margin-left", "auto") || !eq(mleft, "margin-right", "auto")) {
        FAIL() << "check 16 failed";
    }
}

/// CSS Values 4 section 3.2: a shorthand whose value is `inherit` / `unset` / `initial` takes
/// that keyword for every longhand it expands to. Without it the declaration is dropped whole,
/// which is how `button { font: inherit }` used to lose to the UA's `font: 400 13.3333px Arial`.
TEST(CssShorthand, WideKeywords) {
    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/shorthand.html", doc, error)) {
        FAIL() << error << "\n";
    }
    zui::css::Engine css;
    if (!css.load_sheets(doc, error)) {
        FAIL() << error << "\n";
    }
    css.cascade(doc);

    const zui::html::Node* font = zui::html::find(doc, "kw-font");
    const zui::html::Node* button = zui::html::find(doc, "kw-button");
    const zui::html::Node* over = zui::html::find(doc, "kw-overflow");
    const zui::html::Node* margin = zui::html::find(doc, "kw-margin");
    const zui::html::Node* border = zui::html::find(doc, "kw-border");
    const zui::html::Node* unset = zui::html::find(doc, "kw-fn-unset");
    if (font == nullptr || button == nullptr || over == nullptr || margin == nullptr ||
        border == nullptr || unset == nullptr) {
        FAIL() << "missing node\n";
    }
    // `font: inherit` carries every longhand, including ones the element set itself in an earlier
    // declaration: the shorthand resets all of them, so the 10px/1 sans-serif above must not
    // survive.
    if (!eq(font, "font-size", "24px") || !eq(font, "font-family", "serif") ||
        !eq(font, "font-style", "italic") || !eq(font, "font-weight", "700") ||
        !eq(font, "line-height", "2") || !eq(font, "font-size-adjust", "0.5")) {
        FAIL() << "check 20 font: inherit failed";
    }
    // The author's `font: inherit` beats the UA sheet's button font (the calendar's top bar).
    if (!eq(button, "font-size", "24px") || !eq(button, "font-family", "serif")) {
        FAIL() << "check 21 button { font: inherit } failed";
    }
    // Shorthands the lexbor parser owns natively still have to reach every longhand.
    if (!eq(over, "overflow-x", "hidden") || !eq(over, "overflow-y", "auto")) {
        FAIL() << "check 22 overflow: inherit failed";
    }
    if (!eq(margin, "margin-top", "7px") || !eq(margin, "margin-bottom", "9px") ||
        !eq(margin, "margin-left", "10px")) {
        FAIL() << "check 23 margin: inherit failed";
    }
    if (!eq(border, "border-top-width", "2px") || !eq(border, "border-top-style", "dashed") ||
        !eq(border, "border-left-color", "#123456")) {
        FAIL() << "check 24 border: inherit failed";
    }
    // `unset` on a shorthand: inherited longhands copy the parent, non-inherited ones reset.
    if (!eq(unset, "font-size", "24px") || !eq(unset, "font-family", "serif") ||
        !eq(unset, "line-height", "2")) {
        FAIL() << "check 25 font: unset failed";
    }
}

// `border: 1px solid var(--line)` is deferred until substitution. `--line` is inherited from
// :root, so the shorthand lookup has to see it. Otherwise a search field keeps the user-agent
// `2px inset` border and paints a dark top/left rim.
TEST(CssShorthand, BorderVarInheritsCustomProperty) {
    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/shorthand.html", doc, error)) {
        FAIL() << error << "\n";
    }
    zui::css::Engine css;
    if (!css.load_sheets(doc, error)) {
        FAIL() << error << "\n";
    }
    css.cascade(doc);

    const zui::html::Node* field = zui::html::find(doc, "search-line");
    const zui::html::Node* local = zui::html::find(doc, "search-local");
    if (field == nullptr || local == nullptr) {
        FAIL() << "missing node\n";
    }
    if (!eq(field, "border-top-style", "solid") || !eq(field, "border-right-style", "solid") ||
        !eq(field, "border-top-width", "1px") || !eq(field, "border-left-width", "1px") ||
        !eq(field, "border-top-color", "rgba(16, 24, 20, 0.1)") ||
        !eq(field, "border-bottom-color", "rgba(16, 24, 20, 0.1)")) {
        FAIL() << "inherited var() in border was dropped";
    }
    if (!eq(local, "border-top-style", "inset") || !eq(local, "border-top-width", "2px")) {
        FAIL() << "invalid local custom property fell through to the inherited border";
    }
}

TEST(CssShorthand, BackgroundInvalidAndColorTable) {
    using Parts = std::vector<std::pair<std::string, std::string>>;
    Parts parts;
    EXPECT_FALSE(zui::css::expand_shorthand_value("background", "foo", parts));
    parts.clear();
    EXPECT_FALSE(zui::css::expand_shorthand_value("background", "foo()", parts));
    parts.clear();
    EXPECT_FALSE(zui::css::expand_shorthand_value("background", "#ggg", parts));
    parts.clear();
    EXPECT_FALSE(zui::css::expand_shorthand_value("background", "red url(a.png), blue", parts));
    parts.clear();
    ASSERT_TRUE(zui::css::expand_shorthand_value(
        "background", "radial-gradient(circle, #000, #fff) padding-box", parts));
    bool saw_image = false;
    bool saw_origin = false;
    for (const auto& p : parts) {
        if (p.first == "background-image") {
            EXPECT_EQ(p.second, "radial-gradient(circle, #000, #fff)");
            saw_image = true;
        }
        if (p.first == "background-origin") {
            EXPECT_EQ(p.second, "padding-box");
            saw_origin = true;
        }
        if (p.first == "background-clip") {
            EXPECT_EQ(p.second, "padding-box");
        }
    }
    EXPECT_TRUE(saw_image);
    EXPECT_TRUE(saw_origin);

    auto tomato = zui::css::parse_css_color("tomato");
    ASSERT_TRUE(tomato.has_value());
    EXPECT_EQ(tomato->r, 255);
    EXPECT_EQ(tomato->g, 99);
    EXPECT_EQ(tomato->b, 71);
}

// CSS Color 5: percentages normalize to 1, then missing weight is the remainder. A sum
// below 1 scales alpha and does not change the mixed channels. sRGB endpoints stay on
// the 8-bit values, so 40% of 255 and 60% of 255 are exact.
TEST(CssColor, ColorMixAndModernSpaces) {
    const auto mix = zui::css::parse_css_color("color-mix(in srgb, rgb(255 0 0) 40%, rgb(0 0 255))");
    ASSERT_TRUE(mix.has_value());
    EXPECT_EQ(mix->r, 102);
    EXPECT_EQ(mix->g, 0);
    EXPECT_EQ(mix->b, 153);
    EXPECT_EQ(mix->a, 255);

    const auto even = zui::css::parse_css_color("color-mix(in srgb, rgb(255 0 0) 50%, rgb(0 0 255) 50%)");
    ASSERT_TRUE(even.has_value());
    EXPECT_EQ(even->r, 128);
    EXPECT_EQ(even->g, 0);
    EXPECT_EQ(even->b, 128);
    EXPECT_EQ(even->a, 255);

    const auto scaled =
        zui::css::parse_css_color("color-mix(in srgb, rgb(255 0 0) 40%, rgb(0 0 255) 40%)");
    ASSERT_TRUE(scaled.has_value());
    EXPECT_EQ(scaled->r, 128);
    EXPECT_EQ(scaled->g, 0);
    EXPECT_EQ(scaled->b, 128);
    EXPECT_EQ(scaled->a, 204);

    const auto fade = zui::css::parse_css_color("color-mix(in srgb, rgb(255 0 0) 50%, transparent)");
    ASSERT_TRUE(fade.has_value());
    EXPECT_EQ(fade->r, 255);
    EXPECT_EQ(fade->g, 0);
    EXPECT_EQ(fade->b, 0);
    EXPECT_EQ(fade->a, 128);

    const auto named = zui::css::parse_css_color("color-mix(in srgb, red 25%, white)");
    ASSERT_TRUE(named.has_value());
    EXPECT_EQ(named->r, 255);
    EXPECT_EQ(named->g, 191);
    EXPECT_EQ(named->b, 191);
    EXPECT_EQ(named->a, 255);

    const auto hwb = zui::css::parse_css_color("hwb(0 0% 0%)");
    ASSERT_TRUE(hwb.has_value());
    EXPECT_EQ(hwb->r, 255);
    EXPECT_EQ(hwb->g, 0);
    EXPECT_EQ(hwb->b, 0);

    // hwb(120 20% 10%): pure green scaled by 0.7 then plus 0.2 white. 0.9*255 is 229.5.
    const auto hwb_green = zui::css::parse_css_color("hwb(120 20% 10%)");
    ASSERT_TRUE(hwb_green.has_value());
    EXPECT_EQ(hwb_green->r, 51);
    EXPECT_EQ(hwb_green->g, 230);
    EXPECT_EQ(hwb_green->b, 51);

    const auto srgb = zui::css::parse_css_color("color(srgb 0.2 0.4 0.8)");
    ASSERT_TRUE(srgb.has_value());
    EXPECT_EQ(srgb->r, 51);
    EXPECT_EQ(srgb->g, 102);
    EXPECT_EQ(srgb->b, 204);

    const auto linear = zui::css::parse_css_color("color(srgb-linear 1 0 0)");
    ASSERT_TRUE(linear.has_value());
    EXPECT_EQ(linear->r, 255);
    EXPECT_EQ(linear->g, 0);
    EXPECT_EQ(linear->b, 0);

    const auto white = zui::css::parse_css_color("oklch(100% 0 0)");
    ASSERT_TRUE(white.has_value());
    EXPECT_EQ(white->r, 255);
    EXPECT_EQ(white->g, 255);
    EXPECT_EQ(white->b, 255);

    EXPECT_FALSE(zui::css::parse_css_color("color(a98-rgb 1 0 0)").has_value());
    EXPECT_FALSE(zui::css::parse_css_color("color(rec2020 1 0 0)").has_value());
    EXPECT_FALSE(zui::css::parse_css_color("color(prophoto-rgb 1 0 0)").has_value());
}

// CSS Nesting: a nested selector is relative to its parent. `&` is that parent, and a
// selector with no `&` is a descendant. The outside elements must stay unmatched.
TEST(CssNesting, ParentSelector) {
    EXPECT_EQ(zui::css::css_expand_nesting("body { margin: 0; }"), "body { margin: 0; }");
    const std::string expanded = zui::css::css_expand_nesting(
        ".card { color: white; .title { color: red; } &:hover { color: black; } > .direct { color: gold; } }");
    EXPECT_NE(expanded.find(":is(.card) .title"), std::string::npos);
    EXPECT_NE(expanded.find(":is(.card):hover"), std::string::npos);
    EXPECT_NE(expanded.find(":is(.card) > .direct"), std::string::npos);
    EXPECT_EQ(expanded.find('&'), std::string::npos);

    zui::html::Document doc;
    std::string error;
    ASSERT_TRUE(zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/nesting.html", doc, error)) << error;
    zui::css::Engine css;
    ASSERT_TRUE(css.load_sheets(doc, error)) << error;
    css.cascade(doc, 800, 600);

    EXPECT_TRUE(eq(zui::html::find(doc, "card"), "background-color", "#34495e"));
    EXPECT_TRUE(eq(zui::html::find(doc, "title"), "background-color", "#d23b3b"));
    EXPECT_TRUE(eq(zui::html::find(doc, "extra"), "background-color", "#2c63d2"));
    EXPECT_TRUE(eq(zui::html::find(doc, "on"), "background-color", "#2ecc71"));
    EXPECT_TRUE(eq(zui::html::find(doc, "direct"), "background-color", "#f1c40f"));
    EXPECT_TRUE(eq(zui::html::find(doc, "media"), "background-color", "#9b59b6"));
    EXPECT_TRUE(eq(zui::html::find(doc, "ac"), "background-color", "#e67e22"));
    EXPECT_TRUE(eq(zui::html::find(doc, "bc"), "background-color", "#e67e22"));

    auto unmatched = [&](const char* id, const char* color) {
        const std::string* v = prop(zui::html::find(doc, id), "background-color");
        EXPECT_TRUE(v == nullptr || *v != color) << id;
    };
    unmatched("outside", "#d23b3b");
    unmatched("on-out", "#2ecc71");
    unmatched("indirect", "#f1c40f");
    unmatched("c-out", "#e67e22");
    unmatched("card", "#000000");
}

/// CSS Box Alignment section 5: `place-items` / `place-content` / `place-self` are
/// `<align> <justify>?`, and a single value sets both axes. None of them expanded, so
/// `.num { place-items: center }` - the calendar's date circle - kept the initial `normal`,
/// which stretches a grid item instead of centring it.
TEST(CssShorthand, PlaceShorthands) {
    using Parts = std::vector<std::pair<std::string, std::string>>;
    auto expand = [](const char* name, const char* value) {
        Parts parts;
        const bool ok = zui::css::expand_shorthand_value(name, value, parts);
        EXPECT_TRUE(ok) << name << ": " << value;
        return parts;
    };
    auto value_of = [](const Parts& parts, const char* name) -> std::string {
        for (const auto& part : parts) {
            if (part.first == name) {
                return part.second;
            }
        }
        return "<missing>";
    };

    Parts one = expand("place-items", "center");
    EXPECT_EQ(value_of(one, "align-items"), "center");
    EXPECT_EQ(value_of(one, "justify-items"), "center");

    Parts two = expand("place-items", "end start");
    EXPECT_EQ(value_of(two, "align-items"), "end");
    EXPECT_EQ(value_of(two, "justify-items"), "start");

    Parts content = expand("place-content", "space-between");
    EXPECT_EQ(value_of(content, "align-content"), "space-between");
    EXPECT_EQ(value_of(content, "justify-content"), "space-between");

    Parts self = expand("place-self", "center end");
    EXPECT_EQ(value_of(self, "align-self"), "center");
    EXPECT_EQ(value_of(self, "justify-self"), "end");
}

/// CSS Containment 3: `container` is `<'container-name'> [ / <'container-type'> ]?`.
/// A bare name resets the type to `normal`. `none` is the name keyword.
TEST(CssShorthand, Container) {
    using Parts = std::vector<std::pair<std::string, std::string>>;
    auto value_of = [](const Parts& parts, const char* name) -> std::string {
        for (const auto& part : parts) {
            if (part.first == name) {
                return part.second;
            }
        }
        return "<missing>";
    };
    auto expand = [&](const char* value) {
        Parts parts;
        EXPECT_TRUE(zui::css::expand_shorthand_value("container", value, parts)) << value;
        return parts;
    };

    Parts named = expand("cards / size");
    EXPECT_EQ(value_of(named, "container-name"), "cards");
    EXPECT_EQ(value_of(named, "container-type"), "size");

    Parts bare = expand("cards");
    EXPECT_EQ(value_of(bare, "container-name"), "cards");
    EXPECT_EQ(value_of(bare, "container-type"), "normal");

    Parts unnamed = expand("none / inline-size");
    EXPECT_EQ(value_of(unnamed, "container-name"), "none");
    EXPECT_EQ(value_of(unnamed, "container-type"), "inline-size");

    Parts wide = expand("inherit");
    EXPECT_EQ(value_of(wide, "container-name"), "inherit");
    EXPECT_EQ(value_of(wide, "container-type"), "inherit");

    Parts bad;
    EXPECT_FALSE(zui::css::expand_shorthand_value("container", "cards / scroll-state", bad));
    EXPECT_TRUE(bad.empty());
}

bool missing_prop(const zui::html::Node* n, const char* key) {
    const std::string* v = prop(n, key);
    if (v != nullptr) {
        ADD_FAILURE() << key << " = " << *v << " want <missing>";
        return false;
    }
    return true;
}

// Cascade 4/5: revert drops the winning origin, revert-layer drops only that layer.
// Padding and mark background are UA values, so a stand-in that treats the keyword as unset
// cannot produce them. all:revert expands onto the longhands first.
TEST(CssRevert, OriginAndLayer) {
    zui::html::Document doc;
    std::string error;
    ASSERT_TRUE(zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/revert.html", doc, error))
        << error;
    zui::css::Engine css;
    ASSERT_TRUE(css.load_sheets(doc, error)) << error;
    css.cascade(doc);

    EXPECT_TRUE(eq(zui::html::find(doc, "mk"), "background-color", "#ffff00"));
    EXPECT_TRUE(eq(zui::html::find(doc, "btn"), "padding-top", "1px"));
    EXPECT_TRUE(eq(zui::html::find(doc, "btn"), "padding-right", "6px"));
    // Important revert still names the author origin, so the normal 30px in the same rule goes too.
    EXPECT_TRUE(eq(zui::html::find(doc, "btn-imp"), "padding-top", "1px"));
    EXPECT_TRUE(eq(zui::html::find(doc, "btn-imp-only"), "padding-top", "1px"));
    EXPECT_TRUE(eq(zui::html::find(doc, "allbtn"), "padding-top", "1px"));
    EXPECT_TRUE(eq(zui::html::find(doc, "allbtn"), "border-top-width", "2px"));
    EXPECT_TRUE(eq(zui::html::find(doc, "allbtn"), "box-sizing", "border-box"));
    EXPECT_TRUE(eq(zui::html::find(doc, "allbtn"), "display", "inline-block"));
    EXPECT_TRUE(missing_prop(zui::html::find(doc, "allbtn"), "background-color"));

    EXPECT_TRUE(eq(zui::html::find(doc, "lay"), "background-color", "#2255cc"));
    EXPECT_TRUE(missing_prop(zui::html::find(doc, "ori"), "background-color"));
    EXPECT_TRUE(eq(zui::html::find(doc, "ch"), "background-color", "#2255cc"));
    // Important revert-layer in the earlier layer also hides later layers' important values.
    EXPECT_TRUE(missing_prop(zui::html::find(doc, "il"), "background-color"));
    EXPECT_TRUE(eq(zui::html::find(doc, "at"), "background-color", "#8855cc"));
    EXPECT_TRUE(missing_prop(zui::html::find(doc, "sm"), "background-color"));
    EXPECT_TRUE(missing_prop(zui::html::find(doc, "imp"), "background-color"));
    EXPECT_TRUE(eq(zui::html::find(doc, "beat"), "background-color", "#33aa55"));
    EXPECT_TRUE(eq(zui::html::find(doc, "v"), "background-color", "#112233"));

    EXPECT_TRUE(eq(zui::html::find_tag(doc.root.get(), "body"), "margin-top", "8px"));
    EXPECT_TRUE(eq(zui::html::find(doc, "pm"), "margin-top", "1em"));
    EXPECT_TRUE(eq(zui::html::find(doc, "pm"), "margin-left", "0"));
}
