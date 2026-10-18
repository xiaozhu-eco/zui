#include <gtest/gtest.h>

#include "css/engine/engine.h"
#include "css/image_set.h"
#include "css/values.h"
#include "html/document.h"
#include "layout/box_tree/detail.h"
#include "layout/engine.h"
#include "layout/paint_map.h"
#include "layout/stacking.h"

#include "log/log.h"

#include <cmath>
#include <memory>
#include <string>

namespace {

const std::string* prop(const zui::html::Node* n, const char* key) { return n == nullptr ? nullptr : n->style(key); }

bool eq(const zui::html::Node* n, const char* key, const char* expect) {
    const std::string* v = prop(n, key);
    if (v == nullptr || *v != expect) {
        ADD_FAILURE() << key << " = " << (v ? *v : std::string("<missing>")) << " want " << expect;
        return false;
    }
    return true;
}

/// The probe pages that measured the reader's two symptoms live in `tests/fixtures` next to the
/// other fixtures (`布局可见性与规范修补方案.md` section 7), so the evidence in that document and
/// the gate below are the same files. This runs one page the way `zui_dump` does.
struct Probe {
    zui::html::Document doc;
    zui::css::Engine css;
    zui::layout::FreeTypeTextMetrics metrics;
    std::unique_ptr<zui::layout::LayoutEngine> layout;
    std::string error;

    bool run(const char* file, float width, float height) {
        const std::string path = std::string(ZUI_SOURCE_DIR "/tests/fixtures/") + file;
        if (!zui::html::load_file(path, doc, error)) {
            return false;
        }
        if (!css.load_sheets(doc, error)) {
            return false;
        }
        css.cascade(doc, width, height);
        if (!metrics.ready()) {
            error = "no font";
            return false;
        }
        layout = std::make_unique<zui::layout::LayoutEngine>(metrics);
        layout->layout(doc, zui::layout::Viewport{width, height});
        return true;
    }

    const zui::layout::Box* box(const char* id) {
        return layout->principal_box(zui::html::find(doc, id));
    }
};

/// The page section 7's cascade probes measure: @p siblings `button.ch` elements with unique ids
/// under `#chapters`, against the `:has()` fixture. Built in memory so the ten-times case costs
/// nothing to generate, and the stylesheet is still the fixture file.
zui::html::Document make_many_buttons(std::size_t siblings) {
    zui::html::Document doc;
    doc.source_path = ZUI_SOURCE_DIR "/tests/fixtures/many_has.html";
    doc.stylesheets.push_back(zui::html::Document::StylesheetSource{"many_has.css", {}});
    auto html = std::make_unique<zui::html::Node>();
    html->tag = "html";
    auto body = std::make_unique<zui::html::Node>();
    body->tag = "body";
    body->parent = html.get();
    auto chapters = std::make_unique<zui::html::Node>();
    chapters->tag = "div";
    chapters->id = "chapters";
    chapters->parent = body.get();
    for (std::size_t i = 0; i < siblings; ++i) {
        auto button = std::make_unique<zui::html::Node>();
        button->tag = "button";
        button->class_name = "ch";
        button->id = "ch" + std::to_string(i + 1);
        button->attrs["type"] = "button";
        button->text = "Chapter " + std::to_string(i + 1);
        button->parent = chapters.get();
        doc.ids[button->id] = button.get();
        chapters->children.push_back(std::move(button));
    }
    body->children.push_back(std::move(chapters));
    html->children.push_back(std::move(body));
    doc.root = std::move(html);
    return doc;
}

/// That page, cascaded against the shared `:has()` fixture.
struct ManyButtons {
    zui::html::Document doc;
    zui::css::Engine css;

    bool run(std::size_t siblings) {
        doc = make_many_buttons(siblings);
        std::string error;
        if (!css.load_sheets(doc, error)) {
            return false;
        }
        css.cascade(doc, 1100.0f, 720.0f);
        return true;
    }

    std::size_t has_calls() const { return css.has_calls(); }
};

/// A scroll container holding @p chapters one-line `.ch` items, styled by `long_list_auto.css` -
/// which is what asks the engine for `content-visibility: auto` and `contain-intrinsic-size`.
zui::html::Document make_long_list(std::size_t chapters) {
    zui::html::Document doc;
    doc.source_path = ZUI_SOURCE_DIR "/tests/fixtures/long_list_auto.css";
    doc.stylesheets.push_back(zui::html::Document::StylesheetSource{"long_list_auto.css", {}});
    auto html = std::make_unique<zui::html::Node>();
    html->tag = "html";
    auto body = std::make_unique<zui::html::Node>();
    body->tag = "body";
    body->parent = html.get();
    auto scroller = std::make_unique<zui::html::Node>();
    scroller->tag = "div";
    scroller->id = "scroller";
    scroller->parent = body.get();
    doc.ids[scroller->id] = scroller.get();
    for (std::size_t i = 0; i < chapters; ++i) {
        auto item = std::make_unique<zui::html::Node>();
        item->tag = "button";
        item->class_name = "ch";
        item->id = "ch" + std::to_string(i + 1);
        item->attrs["type"] = "button";
        item->text = "Chapter " + std::to_string(i + 1);
        item->parent = scroller.get();
        doc.ids[item->id] = item.get();
        scroller->children.push_back(std::move(item));
    }
    body->children.push_back(std::move(scroller));
    html->children.push_back(std::move(body));
    doc.root = std::move(html);
    return doc;
}

}  // namespace

TEST(CssSpec, CascadeAndLayout) {
    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/spec.html", doc, error)) {
        FAIL() << error << "\n";
    }
    zui::css::Engine css;
    if (!css.load_sheets(doc, error)) {
        FAIL() << error << "\n";
    }
    css.cascade(doc, 1100, 720);

    const zui::html::Node* wide = zui::html::find(doc, "wide");
    const zui::html::Node* narrow = zui::html::find(doc, "narrow");
    const zui::html::Node* hov = zui::html::find(doc, "hov");
    const zui::html::Node* pad = zui::html::find(doc, "pad");
    const zui::html::Node* sheet = zui::html::find(doc, "sheet");
    if (wide == nullptr || narrow == nullptr || hov == nullptr || pad == nullptr || sheet == nullptr) {
        FAIL() << "missing node\n";
    }
    if (!eq(wide, "color", "#abcdef") || !eq(wide, "display", "grid")) {
        FAIL() << "check 4 failed";
    }
    if (prop(narrow, "display") != nullptr && *prop(narrow, "display") == "flex") {
        FAIL() << "narrow media matched\n";
    }
    if (!eq(narrow, "color", "#010101")) {
        FAIL() << "check 6 failed";
    }
    // Default HostMediaFacts::hover is true (headless / no OS probe on this path).
    if (!eq(hov, "color", "#ff0000")) {
        FAIL() << "check 7 failed";
    }
    if (!eq(pad, "padding-top", "0px") || !eq(pad, "padding-right", "16px") || !eq(pad, "padding-bottom", "0") ||
        !eq(pad, "padding-left", "16px")) {
        FAIL() << "check 8 failed";
    }
    if (!eq(pad, "border-bottom-color", "#445566") || !eq(sheet, "display", "contents")) {
        FAIL() << "check 9 failed";
    }

    zui::layout::FreeTypeTextMetrics metrics;
    if (!metrics.ready()) {
        FAIL() << "no font\n";
    }
    zui::layout::LayoutEngine layout(metrics);
    layout.layout(doc, zui::layout::Viewport{1100.0f, 720.0f});
    // This test asserts on *layout* geometry, including boxes below the fold. A frame's paint walk
    // culls those (LAYOUT.md section 9 step B) and leaves them without world coordinates, so the
    // geometry is mapped for every box here.
    zui::layout::map_paint_every_box(layout.paint_map_session(), *layout.root());

    auto* app = layout.principal_box(zui::html::find(doc, "app"));
    auto* reader = layout.principal_box(zui::html::find(doc, "reader"));
    auto* toolbar = layout.principal_box(zui::html::find(doc, "toolbar"));
    auto* sheet_box = layout.principal_box(zui::html::find(doc, "sheet"));
    auto* minbox = layout.principal_box(zui::html::find(doc, "minbox"));
    if (app == nullptr || reader == nullptr || toolbar == nullptr || minbox == nullptr) {
        FAIL() << "missing box\n";
    }
    if (sheet_box != nullptr) {
        FAIL() << "display:contents generated a box\n";
    }
    if (toolbar->parent != reader) {
        FAIL() << "contents child was not promoted\n";
    }
    if (std::fabs(app->geo.height - 720.0f) > 1.0f) {
        FAIL() << "100dvh height " << app->geo.height << "\n";
    }
    if (std::fabs(minbox->geo.content_width - 40.0f) > 1.0f) {
        FAIL() << "min() width " << minbox->geo.content_width << "\n";
    }
    const float vh78 = 0.78f * 720.0f;
    if (std::fabs(minbox->geo.content_height - vh78) > 1.0f) {
        FAIL() << "min(78vh,680px) height " << minbox->geo.content_height << "\n";
    }
    if (std::fabs(minbox->geo.margin.top - 56.0f) > 1.0f) {
        FAIL() << "calc margin " << minbox->geo.margin.top << "\n";
    }
    
}

TEST(CssSpec, EnvSafeAreaAndKeyboardFromMediaState) {
    zui::css::MediaState media;
    media.safe_top = 12.f;
    media.safe_right = 0.f;
    media.safe_bottom = 34.f;
    media.safe_left = 8.f;
    media.keyboard_bottom = 280.f;

    zui::css::CustomLookup empty;
    auto top = zui::css::substitute("env(safe-area-inset-top)", empty, media);
    auto bottom = zui::css::substitute("env(safe-area-inset-bottom)", empty, media);
    auto left = zui::css::substitute("env(safe-area-inset-left)", empty, media);
    auto kb = zui::css::substitute("env(keyboard-inset-bottom)", empty, media);
    auto kb_top = zui::css::substitute("env(keyboard-inset-top)", empty, media);
    ASSERT_TRUE(top.has_value());
    ASSERT_TRUE(bottom.has_value());
    ASSERT_TRUE(left.has_value());
    ASSERT_TRUE(kb.has_value());
    ASSERT_TRUE(kb_top.has_value());
    EXPECT_EQ(*top, "12px");
    EXPECT_EQ(*bottom, "34px");
    EXPECT_EQ(*left, "8px");
    EXPECT_EQ(*kb, "280px");
    EXPECT_EQ(*kb_top, "0px");

    // Default MediaState → 0px (difftest / desktop).
    auto zero = zui::css::substitute("env(safe-area-inset-top)", empty);
    ASSERT_TRUE(zero.has_value());
    EXPECT_EQ(*zero, "0px");

    // Cascade path: padding resolves through HostMediaFacts insets.
    constexpr const char* kCss = "#box{padding-top:env(safe-area-inset-top);"
                                 "padding-bottom:env(keyboard-inset-bottom)}";
    zui::html::Document doc;
    auto box = std::make_unique<zui::html::Node>();
    box->tag = "div";
    box->id = "box";
    auto body = std::make_unique<zui::html::Node>();
    body->tag = "body";
    box->parent = body.get();
    doc.ids[box->id] = box.get();
    body->children.push_back(std::move(box));
    auto html = std::make_unique<zui::html::Node>();
    html->tag = "html";
    body->parent = html.get();
    html->children.push_back(std::move(body));
    doc.root = std::move(html);

    zui::css::Engine css;
    std::string error;
    ASSERT_TRUE(css.add_sheet_text(kCss, error)) << error;
    zui::HostMediaFacts host;
    host.safe_top = 12.f;
    host.keyboard_bottom = 280.f;
    css.cascade(doc, 800.f, 600.f, host);
    const zui::html::Node* node = zui::html::find(doc, "box");
    ASSERT_NE(node, nullptr);
    EXPECT_TRUE(eq(node, "padding-top", "12px"));
    EXPECT_TRUE(eq(node, "padding-bottom", "280px"));
}

TEST(CssSpec, PrefersColorSchemeFromHostMedia) {
    constexpr const char* kCss =
        "div{color:#111111}"
        "@media (prefers-color-scheme: dark){div{color:#eeeeee}}"
        "@media (prefers-color-scheme: light){div{color:#222222}}";

    auto make_doc = []() {
        zui::html::Document doc;
        auto div = std::make_unique<zui::html::Node>();
        div->tag = "div";
        div->id = "swatch";
        auto body = std::make_unique<zui::html::Node>();
        body->tag = "body";
        body->parent = nullptr;
        div->parent = body.get();
        doc.ids[div->id] = div.get();
        body->children.push_back(std::move(div));
        auto html = std::make_unique<zui::html::Node>();
        html->tag = "html";
        body->parent = html.get();
        html->children.push_back(std::move(body));
        doc.root = std::move(html);
        return doc;
    };

    {
        zui::html::Document doc = make_doc();
        zui::css::Engine css;
        std::string error;
        ASSERT_TRUE(css.add_sheet_text(kCss, error)) << error;
        zui::HostMediaFacts host;
        host.color_scheme = zui::ColorSchemePreference::Dark;
        css.cascade(doc, 800.f, 600.f, host);
        const zui::html::Node* swatch = zui::html::find(doc, "swatch");
        ASSERT_NE(swatch, nullptr);
        EXPECT_TRUE(eq(swatch, "color", "#eeeeee"));
    }
    {
        zui::html::Document doc = make_doc();
        zui::css::Engine css;
        std::string error;
        ASSERT_TRUE(css.add_sheet_text(kCss, error)) << error;
        zui::HostMediaFacts host;
        host.color_scheme = zui::ColorSchemePreference::Light;
        css.cascade(doc, 800.f, 600.f, host);
        const zui::html::Node* swatch = zui::html::find(doc, "swatch");
        ASSERT_NE(swatch, nullptr);
        EXPECT_TRUE(eq(swatch, "color", "#222222"));
    }
    {
        zui::html::Document doc = make_doc();
        zui::css::Engine css;
        std::string error;
        ASSERT_TRUE(css.add_sheet_text(kCss, error)) << error;
        // Default cascade (no host): light preference → light rule.
        css.cascade(doc, 800.f, 600.f);
        const zui::html::Node* swatch = zui::html::find(doc, "swatch");
        ASSERT_NE(swatch, nullptr);
        EXPECT_TRUE(eq(swatch, "color", "#222222"));
    }
}

TEST(CssSpec, EnvTitlebarAndFallback) {
    zui::css::CustomLookup empty;
    zui::css::MediaState media;
    auto title = zui::css::substitute("env(titlebar-area-width)", empty, media);
    ASSERT_TRUE(title.has_value());
    EXPECT_EQ(*title, "0px");
    auto fb = zui::css::substitute("env(not-a-real-env, 16px)", empty, media);
    ASSERT_TRUE(fb.has_value());
    EXPECT_EQ(*fb, "16px");
    auto known = zui::css::substitute("env(safe-area-inset-top, 99px)", empty, media);
    ASSERT_TRUE(known.has_value());
    EXPECT_EQ(*known, "0px");
    auto missing = zui::css::substitute("env(not-a-real-env)", empty, media);
    EXPECT_FALSE(missing.has_value());
}

TEST(CssSpec, SupportsCondition) {
    EXPECT_TRUE(zui::css::supports_matches("(display: flex)"));
    EXPECT_TRUE(zui::css::supports_matches("(transform: matrix(1, 0, 0, 1, 0, 0))"));
    EXPECT_FALSE(zui::css::supports_matches("(not-a-real-property: 1)"));
    EXPECT_TRUE(zui::css::supports_matches("not (not-a-real-property: 1)"));
    EXPECT_TRUE(zui::css::supports_matches("(display: grid) and (color: red)"));
    EXPECT_TRUE(zui::css::supports_matches("(display: flex) or (not-a-real-property: 1)"));
    EXPECT_TRUE(zui::css::supports_matches("(--theme: #fff)"));
    EXPECT_TRUE(zui::css::supports_matches("selector(.x)"));
    EXPECT_TRUE(zui::css::supports_matches("selector(div > span)"));
    EXPECT_TRUE(zui::css::supports_matches("selector(:hover)"));
    EXPECT_TRUE(zui::css::supports_matches("selector(:focus-visible)"));
    EXPECT_TRUE(zui::css::supports_matches("selector(:has(img))"));
    EXPECT_TRUE(zui::css::supports_matches("selector(::before)"));
    EXPECT_TRUE(zui::css::supports_matches("(selector(#id))"));
    EXPECT_TRUE(zui::css::supports_matches("selector(.a) and (display: flex)"));
    EXPECT_TRUE(zui::css::supports_matches("not selector(:not-a-real)"));
    EXPECT_TRUE(zui::css::supports_matches("selector(:not-a-real) or (display: flex)"));
    EXPECT_FALSE(zui::css::supports_matches("selector(:not-a-real)"));
    EXPECT_FALSE(zui::css::supports_matches("selector(::not-a-real)"));
    EXPECT_FALSE(zui::css::supports_matches("selector(>)"));
    EXPECT_FALSE(zui::css::supports_matches("selector()"));

    constexpr const char* kCss =
        "div{width:40px}"
        "@supports (display: flex){div{width:120px}}"
        "@supports (nope: 1){div{height:99px}}";
    zui::html::Document doc;
    auto div = std::make_unique<zui::html::Node>();
    div->tag = "div";
    div->id = "box";
    auto body = std::make_unique<zui::html::Node>();
    body->tag = "body";
    div->parent = body.get();
    doc.ids[div->id] = div.get();
    body->children.push_back(std::move(div));
    auto html = std::make_unique<zui::html::Node>();
    html->tag = "html";
    body->parent = html.get();
    html->children.push_back(std::move(body));
    doc.root = std::move(html);

    zui::css::Engine css;
    std::string error;
    ASSERT_TRUE(css.add_sheet_text(kCss, error)) << error;
    css.cascade(doc, 800.f, 600.f);
    const zui::html::Node* box = zui::html::find(doc, "box");
    ASSERT_NE(box, nullptr);
    EXPECT_TRUE(eq(box, "width", "120px"));
    EXPECT_EQ(prop(box, "height"), nullptr);
}

TEST(CssSpec, PointerAndHoverMedia) {
    zui::css::MediaState fine;
    fine.pointer = zui::PointerAccuracy::Fine;
    fine.any_hover = true;
    EXPECT_TRUE(zui::css::media_matches("(pointer: fine)", fine));
    EXPECT_FALSE(zui::css::media_matches("(pointer: coarse)", fine));
    zui::css::MediaState coarse;
    coarse.pointer = zui::PointerAccuracy::Coarse;
    coarse.any_hover = false;
    EXPECT_TRUE(zui::css::media_matches("(pointer: coarse)", coarse));
    EXPECT_TRUE(zui::css::media_matches("(any-hover: none)", coarse));

    constexpr const char* kCss =
        "div{width:40px}"
        "@media (pointer: fine){div{width:120px}}"
        "@media (pointer: coarse){div{height:99px}}"
        "@media (any-hover: none){div{margin-top:99px}}";

    auto make_doc = []() {
        zui::html::Document doc;
        auto div = std::make_unique<zui::html::Node>();
        div->tag = "div";
        div->id = "box";
        auto body = std::make_unique<zui::html::Node>();
        body->tag = "body";
        div->parent = body.get();
        doc.ids[div->id] = div.get();
        body->children.push_back(std::move(div));
        auto html = std::make_unique<zui::html::Node>();
        html->tag = "html";
        body->parent = html.get();
        html->children.push_back(std::move(body));
        doc.root = std::move(html);
        return doc;
    };

    {
        zui::html::Document doc = make_doc();
        zui::css::Engine css;
        std::string error;
        ASSERT_TRUE(css.add_sheet_text(kCss, error)) << error;
        zui::HostMediaFacts host;
        host.pointer = zui::PointerAccuracy::Fine;
        host.any_hover = true;
        css.cascade(doc, 800.f, 600.f, host);
        const zui::html::Node* box = zui::html::find(doc, "box");
        ASSERT_NE(box, nullptr);
        EXPECT_TRUE(eq(box, "width", "120px"));
        EXPECT_EQ(prop(box, "height"), nullptr);
        EXPECT_EQ(prop(box, "margin-top"), nullptr);
    }
    {
        zui::html::Document doc = make_doc();
        zui::css::Engine css;
        std::string error;
        ASSERT_TRUE(css.add_sheet_text(kCss, error)) << error;
        zui::HostMediaFacts host;
        host.pointer = zui::PointerAccuracy::Coarse;
        host.any_hover = false;
        css.cascade(doc, 800.f, 600.f, host);
        const zui::html::Node* box = zui::html::find(doc, "box");
        ASSERT_NE(box, nullptr);
        EXPECT_TRUE(eq(box, "width", "40px"));
        EXPECT_TRUE(eq(box, "height", "99px"));
        EXPECT_TRUE(eq(box, "margin-top", "99px"));
    }
}

TEST(CssSpec, OrientationMedia) {
    zui::css::MediaState land;
    land.width = 800.f;
    land.height = 600.f;
    EXPECT_TRUE(zui::css::media_matches("(orientation: landscape)", land));
    EXPECT_FALSE(zui::css::media_matches("(orientation: portrait)", land));
    EXPECT_TRUE(zui::css::media_matches("(orientation)", land));
    EXPECT_TRUE(zui::css::media_matches("not (orientation: portrait)", land));

    zui::css::MediaState port;
    port.width = 400.f;
    port.height = 700.f;
    EXPECT_TRUE(zui::css::media_matches("(orientation: portrait)", port));
    EXPECT_FALSE(zui::css::media_matches("(orientation: landscape)", port));

    // A square is portrait: height is not less than width.
    zui::css::MediaState square;
    square.width = 500.f;
    square.height = 500.f;
    EXPECT_TRUE(zui::css::media_matches("(orientation: portrait)", square));
    EXPECT_FALSE(zui::css::media_matches("(orientation: landscape)", square));

    EXPECT_TRUE(zui::css::container_condition_matches("(orientation: landscape)", 800.f, 600.f));
    EXPECT_FALSE(zui::css::container_condition_matches("(orientation: landscape)", 400.f, 700.f));
}

TEST(CssSpec, MediaRange) {
    zui::css::MediaState view;
    view.width = 800.f;
    view.height = 600.f;
    EXPECT_TRUE(zui::css::media_matches("(width >= 700px)", view));
    EXPECT_TRUE(zui::css::media_matches("(width>=700px)", view));
    EXPECT_TRUE(zui::css::media_matches("(min-width: 700px)", view));
    EXPECT_FALSE(zui::css::media_matches("(width > 800px)", view));
    EXPECT_TRUE(zui::css::media_matches("(width >= 800px)", view));
    EXPECT_TRUE(zui::css::media_matches("(width = 800px)", view));
    EXPECT_FALSE(zui::css::media_matches("(width < 400px)", view));
    EXPECT_TRUE(zui::css::media_matches("(400px <= width <= 900px)", view));
    EXPECT_FALSE(zui::css::media_matches("(900px <= width)", view));
    EXPECT_TRUE(zui::css::media_matches("(width >= 40em)", view));
    EXPECT_TRUE(zui::css::media_matches("(height <= 700px)", view));
    EXPECT_FALSE(zui::css::media_matches("(height < 600px)", view));
    EXPECT_FALSE(zui::css::media_matches("(100px < width > 900px)", view));
    EXPECT_FALSE(zui::css::media_matches("not (100px < width > 900px)", view));
    EXPECT_TRUE(zui::css::media_matches("(width >= 700px) and (orientation: landscape)", view));
    EXPECT_TRUE(zui::css::container_condition_matches("(width >= 150px)", 200.f, 40.f));
    EXPECT_FALSE(zui::css::container_condition_matches("(width >= 150px)", 100.f, 40.f));
}

TEST(CssSpec, AspectRatioMedia) {
    zui::css::MediaState view;
    view.width = 800.f;
    view.height = 600.f;
    EXPECT_TRUE(zui::css::media_matches("(aspect-ratio)", view));
    EXPECT_TRUE(zui::css::media_matches("(aspect-ratio: 4/3)", view));
    EXPECT_TRUE(zui::css::media_matches("(aspect-ratio: 8/6)", view));
    EXPECT_TRUE(zui::css::media_matches("(aspect-ratio: 4 / 3)", view));
    EXPECT_FALSE(zui::css::media_matches("(aspect-ratio: 16/9)", view));
    EXPECT_TRUE(zui::css::media_matches("(min-aspect-ratio: 1/1)", view));
    EXPECT_TRUE(zui::css::media_matches("(min-aspect-ratio: 1)", view));
    EXPECT_FALSE(zui::css::media_matches("(max-aspect-ratio: 1/1)", view));
    EXPECT_TRUE(zui::css::media_matches("(aspect-ratio >= 4/3)", view));
    EXPECT_TRUE(zui::css::media_matches("(aspect-ratio > 1/1)", view));
    EXPECT_FALSE(zui::css::media_matches("(aspect-ratio > 4/3)", view));
    EXPECT_FALSE(zui::css::media_matches("(aspect-ratio >= 16/9)", view));
    EXPECT_TRUE(zui::css::media_matches("(1/1 <= aspect-ratio <= 16/9)", view));
    EXPECT_FALSE(zui::css::media_matches("(16/9 <= aspect-ratio)", view));
    EXPECT_TRUE(zui::css::media_matches("(aspect-ratio >= 1)", view));
    EXPECT_FALSE(zui::css::media_matches("(16/9 <= width)", view));
    EXPECT_FALSE(zui::css::media_matches("not (16/9 <= width)", view));
    EXPECT_TRUE(zui::css::media_matches("(width >= 700px)", view));

    zui::css::MediaState square;
    square.width = 500.f;
    square.height = 500.f;
    EXPECT_TRUE(zui::css::media_matches("(aspect-ratio: 1/1)", square));
    EXPECT_FALSE(zui::css::media_matches("(aspect-ratio > 1)", square));

    zui::css::MediaState flat;
    flat.width = 800.f;
    flat.height = 0.f;
    EXPECT_FALSE(zui::css::media_matches("(aspect-ratio: 1/1)", flat));
    EXPECT_TRUE(zui::css::media_matches("not (aspect-ratio: 1/1)", flat));
    EXPECT_FALSE(zui::css::media_matches("(aspect-ratio)", flat));

    EXPECT_TRUE(zui::css::container_condition_matches("(aspect-ratio >= 16/9)", 200.f, 100.f));
    EXPECT_FALSE(zui::css::container_condition_matches("(min-aspect-ratio: 3/1)", 200.f, 100.f));
    EXPECT_TRUE(zui::css::container_condition_matches("(aspect-ratio: 2/1)", 200.f, 100.f));
}

TEST(CssSpec, ResolutionMedia) {
    zui::css::MediaState view;
    view.width = 800.f;
    view.height = 600.f;
    EXPECT_TRUE(zui::css::media_matches("(resolution)", view));
    EXPECT_TRUE(zui::css::media_matches("(resolution: 1dppx)", view));
    EXPECT_TRUE(zui::css::media_matches("(resolution: 1x)", view));
    EXPECT_TRUE(zui::css::media_matches("(resolution: 96dpi)", view));
    EXPECT_FALSE(zui::css::media_matches("(min-resolution: 2dppx)", view));
    EXPECT_TRUE(zui::css::media_matches("(max-resolution: 2dppx)", view));
    EXPECT_TRUE(zui::css::media_matches("(max-resolution: 192dpi)", view));
    EXPECT_FALSE(zui::css::media_matches("(resolution >= 2dppx)", view));
    EXPECT_FALSE(zui::css::media_matches("(resolution > 1x)", view));
    EXPECT_TRUE(zui::css::media_matches("(resolution >= 96dpi)", view));
    EXPECT_TRUE(zui::css::media_matches("(1dppx <= resolution <= 2x)", view));
    EXPECT_FALSE(zui::css::media_matches("(192dpi <= resolution)", view));
    EXPECT_TRUE(zui::css::media_matches("(resolution >= 1dpcm)", view));
    EXPECT_FALSE(zui::css::media_matches("(max-resolution: 1dpcm)", view));
    EXPECT_FALSE(zui::css::media_matches("(2dppx <= width)", view));
    EXPECT_FALSE(zui::css::media_matches("not (2dppx <= width)", view));
    EXPECT_TRUE(zui::css::media_matches("(width >= 700px)", view));
    EXPECT_TRUE(zui::css::media_matches("(aspect-ratio: 4/3)", view));

    view.resolution_dppx = 2.f;
    EXPECT_TRUE(zui::css::media_matches("(min-resolution: 2dppx)", view));
    EXPECT_TRUE(zui::css::media_matches("(resolution >= 192dpi)", view));
    EXPECT_FALSE(zui::css::media_matches("(resolution: 1x)", view));

    EXPECT_FALSE(zui::css::container_condition_matches("(min-resolution: 1dppx)", 200.f, 100.f));
    EXPECT_FALSE(zui::css::container_condition_matches("(resolution >= 1dppx)", 200.f, 100.f));
    EXPECT_TRUE(zui::css::container_condition_matches("not (min-resolution: 2dppx)", 200.f, 100.f));
    EXPECT_TRUE(zui::css::container_condition_matches("(width >= 150px)", 200.f, 100.f));
}

TEST(CssSpec, MediaColorScripting) {
    zui::css::MediaState view;
    view.width = 800.f;
    view.height = 600.f;
    EXPECT_TRUE(zui::css::media_matches("(color)", view));
    EXPECT_TRUE(zui::css::media_matches("(color: 8)", view));
    EXPECT_TRUE(zui::css::media_matches("(min-color: 4)", view));
    EXPECT_FALSE(zui::css::media_matches("(max-color: 4)", view));
    EXPECT_TRUE(zui::css::media_matches("(color >= 8)", view));
    EXPECT_FALSE(zui::css::media_matches("(color > 8)", view));
    EXPECT_TRUE(zui::css::media_matches("(4 <= color <= 8)", view));
    EXPECT_FALSE(zui::css::media_matches("(color: 0)", view));
    EXPECT_FALSE(zui::css::media_matches("(color: 8px)", view));
    EXPECT_FALSE(zui::css::media_matches("(monochrome)", view));
    EXPECT_TRUE(zui::css::media_matches("(monochrome: 0)", view));
    EXPECT_TRUE(zui::css::media_matches("not (monochrome)", view));
    EXPECT_FALSE(zui::css::media_matches("(min-monochrome: 1)", view));
    EXPECT_TRUE(zui::css::media_matches("(monochrome <= 0)", view));
    EXPECT_TRUE(zui::css::media_matches("(scripting)", view));
    EXPECT_TRUE(zui::css::media_matches("(scripting: enabled)", view));
    EXPECT_FALSE(zui::css::media_matches("(scripting: none)", view));
    EXPECT_FALSE(zui::css::media_matches("(scripting: initial-only)", view));
    EXPECT_TRUE(zui::css::media_matches("(color) and (scripting: enabled)", view));
    EXPECT_FALSE(zui::css::container_condition_matches("(color)", 200.f, 100.f));
    EXPECT_FALSE(zui::css::container_condition_matches("(scripting: enabled)", 200.f, 100.f));
    EXPECT_FALSE(zui::css::container_condition_matches("(monochrome: 0)", 200.f, 100.f));
    EXPECT_TRUE(zui::css::container_condition_matches("(width >= 150px)", 200.f, 100.f));
}

TEST(CssSpec, MediaDisplayUpdate) {
    zui::css::MediaState view;
    view.width = 800.f;
    view.height = 600.f;
    // A normal browser window: browser display, fast updates, scrollable overflow, bitmap.
    EXPECT_TRUE(zui::css::media_matches("(display-mode)", view));
    EXPECT_TRUE(zui::css::media_matches("(display-mode: browser)", view));
    EXPECT_FALSE(zui::css::media_matches("(display-mode: standalone)", view));
    EXPECT_FALSE(zui::css::media_matches("(display-mode: fullscreen)", view));
    EXPECT_FALSE(zui::css::media_matches("(display-mode: minimal-ui)", view));
    EXPECT_FALSE(zui::css::media_matches("(display-mode: picture-in-picture)", view));
    EXPECT_FALSE(zui::css::media_matches("(display-mode: window-controls-overlay)", view));
    EXPECT_TRUE(zui::css::media_matches("(update)", view));
    EXPECT_TRUE(zui::css::media_matches("(update: fast)", view));
    EXPECT_FALSE(zui::css::media_matches("(update: slow)", view));
    EXPECT_FALSE(zui::css::media_matches("(update: none)", view));
    EXPECT_TRUE(zui::css::media_matches("not (update: none)", view));
    EXPECT_TRUE(zui::css::media_matches("(overflow-block)", view));
    EXPECT_TRUE(zui::css::media_matches("(overflow-block: scroll)", view));
    EXPECT_FALSE(zui::css::media_matches("(overflow-block: none)", view));
    EXPECT_FALSE(zui::css::media_matches("(overflow-block: paged)", view));
    EXPECT_TRUE(zui::css::media_matches("(overflow-inline)", view));
    EXPECT_TRUE(zui::css::media_matches("(overflow-inline: scroll)", view));
    EXPECT_FALSE(zui::css::media_matches("(overflow-inline: none)", view));
    EXPECT_FALSE(zui::css::media_matches("(grid)", view));
    EXPECT_TRUE(zui::css::media_matches("(grid: 0)", view));
    EXPECT_TRUE(zui::css::media_matches("not (grid)", view));
    EXPECT_FALSE(zui::css::media_matches("(grid: 1)", view));
    EXPECT_FALSE(zui::css::media_matches("(grid: 2)", view));
    EXPECT_TRUE(zui::css::media_matches("not (grid: 2)", view));
    EXPECT_FALSE(zui::css::media_matches("(min-grid: 0)", view));
    EXPECT_TRUE(zui::css::media_matches("not (min-update: slow)", view));
    EXPECT_FALSE(zui::css::media_matches("(update >= fast)", view));
    EXPECT_FALSE(zui::css::media_matches("not (update >= fast)", view));
    EXPECT_FALSE(zui::css::media_matches("(grid >= 0)", view));
    EXPECT_FALSE(zui::css::media_matches("not (grid >= 0)", view));
    EXPECT_TRUE(zui::css::media_matches("(display-mode: browser) and (update: fast)", view));
    EXPECT_FALSE(zui::css::container_condition_matches("(display-mode: browser)", 200.f, 100.f));
    EXPECT_FALSE(zui::css::container_condition_matches("(update)", 200.f, 100.f));
    EXPECT_FALSE(zui::css::container_condition_matches("(overflow-block: scroll)", 200.f, 100.f));
    EXPECT_FALSE(zui::css::container_condition_matches("(overflow-inline: scroll)", 200.f, 100.f));
    EXPECT_FALSE(zui::css::container_condition_matches("(grid: 0)", 200.f, 100.f));
    EXPECT_TRUE(zui::css::container_condition_matches("(width >= 150px)", 200.f, 100.f));
}

TEST(CssSpec, ContainerInlineSize) {
    zui::css::MediaState view;
    view.width = 800.f;
    view.height = 600.f;
    EXPECT_FALSE(zui::css::media_matches("(inline-size >= 100px)", view));
    EXPECT_FALSE(zui::css::media_matches("(min-inline-size: 100px)", view));
    EXPECT_FALSE(zui::css::media_matches("(block-size < 900px)", view));
    EXPECT_TRUE(zui::css::media_matches("not (inline-size >= 100px)", view));
    EXPECT_TRUE(zui::css::media_matches("(width >= 700px)", view));

    EXPECT_TRUE(zui::css::container_condition_matches("(min-inline-size: 300px)", 320.f, 220.f));
    EXPECT_TRUE(zui::css::container_condition_matches("(inline-size >= 300px)", 320.f, 220.f));
    EXPECT_FALSE(zui::css::container_condition_matches("(inline-size > 400px)", 320.f, 220.f));
    EXPECT_TRUE(zui::css::container_condition_matches("(200px <= inline-size <= 400px)", 320.f, 220.f));
    EXPECT_TRUE(zui::css::container_condition_matches("(max-block-size: 240px)", 320.f, 220.f));
    EXPECT_FALSE(zui::css::container_condition_matches("(block-size > 400px)", 320.f, 220.f));
    EXPECT_TRUE(zui::css::container_condition_matches("(inline-size)", 320.f, 220.f));
    EXPECT_FALSE(zui::css::container_condition_matches("(16/9 <= inline-size)", 320.f, 220.f));
    EXPECT_FALSE(zui::css::container_condition_matches("not (16/9 <= inline-size)", 320.f, 220.f));
    EXPECT_TRUE(zui::css::container_condition_matches("(width >= 300px)", 320.f, 220.f));
    EXPECT_FALSE(zui::css::container_condition_matches("(inline-size >= 300px)", 200.f, 220.f));
}

TEST(CssSpec, ImgSrcset) {
    using zui::layout::box_tree_detail::select_img_url;
    zui::html::Node img;
    img.tag = "img";
    img.attrs["src"] = "blue.png";
    img.attrs["srcset"] = "green.png 1x, blue.png 2x";
    EXPECT_EQ(select_img_url(&img, 800.f, 600.f, 1.f), "green.png");
    EXPECT_EQ(select_img_url(&img, 800.f, 600.f, 2.f), "blue.png");

    img.attrs["srcset"] = "blue.png 2x";
    EXPECT_EQ(select_img_url(&img, 800.f, 600.f, 1.f), "blue.png");
    img.attrs["src"] = "green.png";
    EXPECT_EQ(select_img_url(&img, 800.f, 600.f, 1.f), "green.png");
    EXPECT_EQ(select_img_url(&img, 800.f, 600.f, 2.f), "blue.png");

    // Equidistant 0.5x and 1.5x, with no 1x `src` candidate: the higher density wins the tie.
    img.attrs.erase("src");
    img.attrs["srcset"] = "low.png 0.5x, high.png 1.5x";
    EXPECT_EQ(select_img_url(&img, 800.f, 600.f, 1.f), "high.png");

    // Later duplicate density is dropped. The first 1x is the one that stays.
    img.attrs["srcset"] = "first.png 1x, second.png 1x, other.png 2x";
    EXPECT_EQ(select_img_url(&img, 800.f, 600.f, 1.f), "first.png");

    img.attrs["src"] = "red.png";
    img.attrs["srcset"] = "green.png, blue.png 2x";
    EXPECT_EQ(select_img_url(&img, 800.f, 600.f, 1.f), "green.png");

    // `w` descriptors ignore `src`. 800w / 100vw at an 800px viewport is 1x.
    img.attrs["src"] = "red.png";
    img.attrs["sizes"] = "100vw";
    img.attrs["srcset"] = "blue.png 400w, green.png 800w";
    EXPECT_EQ(select_img_url(&img, 800.f, 600.f, 1.f), "green.png");
    EXPECT_EQ(select_img_url(&img, 400.f, 600.f, 1.f), "blue.png");

    img.attrs["sizes"] = "(min-width: 700px) 200px, 100vw";
    img.attrs["srcset"] = "green.png 200w, blue.png 800w";
    EXPECT_EQ(select_img_url(&img, 800.f, 600.f, 1.f), "green.png");
    EXPECT_EQ(select_img_url(&img, 400.f, 600.f, 1.f), "green.png");

    img.attrs["sizes"] = "calc(100vw - 600px)";
    img.attrs["srcset"] = "green.png 200w, blue.png 800w";
    EXPECT_EQ(select_img_url(&img, 800.f, 600.f, 1.f), "green.png");

    img.attrs["sizes"] = "20em";
    img.attrs["srcset"] = "green.png 320w, blue.png 800w";
    EXPECT_EQ(select_img_url(&img, 800.f, 600.f, 1.f), "green.png");

    img.attrs.clear();
    img.attrs["src"] = "only.png";
    EXPECT_EQ(select_img_url(&img, 800.f, 600.f, 1.f), "only.png");

    zui::html::Node picture;
    picture.tag = "picture";
    auto narrow = std::make_unique<zui::html::Node>();
    narrow->tag = "source";
    narrow->parent = &picture;
    narrow->attrs["media"] = "(max-width: 400px)";
    narrow->attrs["srcset"] = "blue.png";
    auto wide = std::make_unique<zui::html::Node>();
    wide->tag = "source";
    wide->parent = &picture;
    wide->attrs["media"] = "(min-width: 700px)";
    wide->attrs["srcset"] = "green.png";
    auto typed = std::make_unique<zui::html::Node>();
    typed->tag = "source";
    typed->parent = &picture;
    typed->attrs["type"] = "image/x-zui-nope";
    typed->attrs["srcset"] = "nope.png";
    auto child = std::make_unique<zui::html::Node>();
    child->tag = "img";
    child->parent = &picture;
    child->attrs["src"] = "blue.png";
    zui::html::Node* child_ptr = child.get();
    picture.children.push_back(std::move(typed));
    picture.children.push_back(std::move(narrow));
    picture.children.push_back(std::move(wide));
    picture.children.push_back(std::move(child));
    EXPECT_EQ(select_img_url(child_ptr, 800.f, 600.f, 1.f), "green.png");
    EXPECT_EQ(select_img_url(child_ptr, 400.f, 600.f, 1.f), "blue.png");
    EXPECT_EQ(select_img_url(child_ptr, 500.f, 600.f, 1.f), "blue.png");

    zui::html::Document doc;
    std::string error;
    ASSERT_TRUE(zui::html::load_file(
        ZUI_SOURCE_DIR "/tests/difftest/cases/img-srcset/page.html", doc, error))
        << error;
    zui::layout::FreeTypeTextMetrics metrics;
    ASSERT_TRUE(metrics.ready());
    zui::layout::LayoutEngine layout(metrics);
    auto chosen = [&](const char* id) {
        zui::layout::Box* box = layout.principal_box(zui::html::find(doc, id));
        if (box == nullptr) {
            return std::string();
        }
        const std::string& path = box->replaced_src;
        const size_t slash = path.find_last_of("/\\");
        return slash == std::string::npos ? path : path.substr(slash + 1);
    };
    layout.layout(doc, zui::layout::Viewport{800.f, 600.f});
    EXPECT_EQ(chosen("density"), "green64.png");
    EXPECT_EQ(chosen("widths"), "green64.png");
    EXPECT_EQ(chosen("sizes"), "green64.png");
    EXPECT_EQ(chosen("picture"), "green64.png");
    EXPECT_EQ(chosen("type"), "green64.png");
    EXPECT_EQ(chosen("fallback"), "green64.png");

    layout.layout(doc, zui::layout::Viewport{400.f, 600.f});
    EXPECT_EQ(chosen("density"), "green64.png");
    EXPECT_EQ(chosen("widths"), "blue64.png");
    EXPECT_EQ(chosen("sizes"), "green64.png");
    EXPECT_EQ(chosen("picture"), "blue64.png");
    EXPECT_EQ(chosen("type"), "green64.png");
    EXPECT_EQ(chosen("fallback"), "green64.png");
}

TEST(CssSpec, ImageSet) {
    using zui::css::select_image_set;
    EXPECT_EQ(select_image_set("image-set(url(\"green.png\") 1x, url(\"blue.png\") 2x)", 1.f),
              "url(\"green.png\")");
    EXPECT_EQ(select_image_set("image-set(url(\"green.png\") 1x, url(\"blue.png\") 2x)", 2.f),
              "url(\"blue.png\")");
    EXPECT_EQ(select_image_set("image-set(url(blue.png) 2x, url(green.png) 1x)", 1.f),
              "url(green.png)");
    EXPECT_EQ(select_image_set("image-set(\"green.png\" 1x, \"blue.png\" 2x)", 1.f),
              "url(\"green.png\")");
    EXPECT_EQ(select_image_set("image-set(url(green.png), url(blue.png) 2x)", 1.f), "url(green.png)");
    EXPECT_EQ(select_image_set("image-set(url(blue.png) 192dpi, url(green.png) 96dpi)", 1.f),
              "url(green.png)");
    EXPECT_EQ(select_image_set("image-set(url(blue.png) 2dppx, url(green.png) 1x)", 1.f),
              "url(green.png)");
    EXPECT_EQ(select_image_set(
                  "image-set(url(nope.png) type(\"image/x-zui-nope\"), url(green.png) type(\"image/png\"))",
                  1.f),
              "url(green.png)");
    EXPECT_EQ(select_image_set("image-set(url(blue.png) 2x)", 1.f), "url(blue.png)");
    EXPECT_EQ(select_image_set("image-set(url(first.png) 1x, url(second.png) 1x)", 1.f),
              "url(first.png)");
    // Equal distance keeps the higher resolution, the same rule as srcset.
    EXPECT_EQ(select_image_set("image-set(url(low.png) 0.5x, url(high.png) 1.5x)", 1.f),
              "url(high.png)");
    EXPECT_EQ(select_image_set("image-set(linear-gradient(red, red) 1x, url(blue.png) 2x)", 1.f),
              "linear-gradient(red, red)");
    EXPECT_TRUE(select_image_set("image-set(url(a.png) type(\"image/webp\"))", 1.f).empty());
    EXPECT_TRUE(select_image_set("url(a.png)", 1.f).empty());
    EXPECT_TRUE(select_image_set("-webkit-image-set(url(a.png) 1x)", 1.f).empty());
    EXPECT_FALSE(zui::css::is_image_set("-webkit-image-set(url(a.png) 1x)"));
    EXPECT_TRUE(zui::css::is_image_set(" image-set( url(a.png) 1x ) "));
}

// Batch 0 of `布局可见性与规范修补方案.md`: the probes moved out of the ignored `build/scratch`
// tree into `tests/fixtures`, so the document's evidence and this gate are one set of files. Only
// behaviour that is already right is asserted here - the length-percentage gap itself (M1-M4) is
// batch 1, and freezing today's wrong numbers would just mean flipping them there.
// `many_*.html` are the 2000-button cascade pages; they belong to batch 2's M5 and are far too slow
// for this test.
TEST(CssSpec, ReaderProbeFixtures) {
    {
        Probe probe;
        if (!probe.run("flow_column.html", 1100.0f, 720.0f)) {
            FAIL() << probe.error << "\n";
        }
        const zui::layout::Box* flow = probe.box("flow_m");
        const zui::layout::Box* large = probe.box("flow_l");
        if (flow == nullptr || large == nullptr) {
            FAIL() << "flow_column.html lost its .flow boxes\n";
        }
        // Section 3.1 cleared `.flow` itself: `padding: ... max(28px, env(...)) ...` has no
        // percentage, so it interns, and `env()` is already 0 by computed-value time.
        if (std::fabs(flow->geo.padding.left - 28.0f) > 0.5f ||
            std::fabs(flow->geo.padding.right - 28.0f) > 0.5f) {
            FAIL() << ".flow padding " << flow->geo.padding.left << "/" << flow->geo.padding.right
                   << "\n";
        }
        // `width: min(50rem, 100%)` at a 16px root, so 800px, centred by `margin: 0 auto`.
        if (std::fabs(large->geo.width - 800.0f) > 1.0f) {
            FAIL() << ".flow.large width " << large->geo.width << "\n";
        }
        if (std::fabs(large->geo.content_width - 744.0f) > 1.0f) {
            FAIL() << ".flow.large content width " << large->geo.content_width << "\n";
        }
    }
    {
        // A window narrower than 50rem (the large measure). `min(50rem, 100%)` has to be the
        // viewport, not 800px hanging out the right side, and the 28px padding stays inside it.
        Probe narrow;
        if (!narrow.run("flow_column.html", 640.0f, 800.0f)) {
            FAIL() << narrow.error << "\n";
        }
        const zui::layout::Box* large = narrow.box("flow_l");
        const zui::layout::Box* text = narrow.box("p_l");
        if (large == nullptr || text == nullptr) {
            FAIL() << "flow_column.html lost its narrow .flow\n";
        }
        if (large->geo.width > 640.5f) {
            FAIL() << "narrow .flow.large wider than the window " << large->geo.width << "\n";
        }
        if (std::fabs(large->geo.content_width - (large->geo.width - 56.0f)) > 1.0f) {
            FAIL() << "narrow .flow.large content width " << large->geo.content_width
                   << " box " << large->geo.width << "\n";
        }
        const float flow_right = large->geo.world_x + large->geo.width;
        const float text_right = text->geo.world_x + text->geo.width;
        if (large->geo.world_x < -0.5f || flow_right > 640.5f) {
            FAIL() << "narrow .flow.large escapes the window x=" << large->geo.world_x
                   << " w=" << large->geo.width << "\n";
        }
        if (text_right > flow_right - 27.0f) {
            FAIL() << "paragraph eats the right padding right=" << text_right
                   << " flow_right=" << flow_right << "\n";
        }
    }
    {
        // The control row of section 3.2: `width: min(400px, 100%)` already resolves on a flex
        // item and on a grid item. E1 widens the used-value path and has to keep this.
        Probe probe;
        if (!probe.run("math.html", 1000.0f, 600.0f)) {
            FAIL() << probe.error << "\n";
        }
        const zui::layout::Box* flex = probe.box("flex_math");
        const zui::layout::Box* grid = probe.box("grid_math");
        if (flex == nullptr || grid == nullptr) {
            FAIL() << "math.html lost its min() controls\n";
        }
        if (std::fabs(flex->geo.width - 400.0f) > 1.0f ||
            std::fabs(grid->geo.width - 400.0f) > 1.0f) {
            FAIL() << "min() widths " << flex->geo.width << "/" << grid->geo.width << "\n";
        }
        if (probe.box("pad_math") == nullptr || probe.box("min_math") == nullptr ||
            probe.box("max_math") == nullptr) {
            FAIL() << "math.html lost its length-percentage probes\n";
        }
    }
    {
        // The two pages whose geometry batch 1 fixes: `page.html` measured the lost right margin in
        // page mode (`.wrap` has the `max(20px, calc((100% - 41rem) / 2))` inline padding), and
        // `math2.html` measured the inset and gap path. Only the wiring is asserted here.
        Probe page;
        if (!page.run("page.html", 1100.0f, 720.0f)) {
            FAIL() << page.error << "\n";
        }
        if (page.box("wrap_m") == nullptr || page.box("flow_m") == nullptr ||
            page.box("wrap_l") == nullptr) {
            FAIL() << "page.html lost its boxes\n";
        }
        Probe math2;
        if (!math2.run("math2.html", 1000.0f, 600.0f)) {
            FAIL() << math2.error << "\n";
        }
        if (math2.box("abs_math") == nullptr || math2.box("gap_math") == nullptr ||
            math2.box("gap_b") == nullptr) {
            FAIL() << "math2.html lost its boxes\n";
        }
    }
}

// `布局可见性与规范修补方案.md` section 6, M1-M4: the used value of a `<length-percentage>` whose
// math mixes `%` with lengths (CSS Values section 10). The fixture is the one the section 7
// evidence used, so the gate and the probe are the same file. Batch 1, E1.

// Switching the reader to the large size on a window that still fits the medium column
// (656px) but not the large one (800px) must grow the column only up to the window, and
// the paragraph must keep the 28px right padding. A stale line break from the medium
// size would paint 23px glyphs through that padding and out of the window.
TEST(CssSpec, ReaderLargeSizeStaysInsideNarrowWindow) {
    Probe probe;
    if (!probe.run("reader_resize.html", 900.0f, 700.0f)) {
        FAIL() << probe.error << "\n";
    }
    const zui::layout::Box* medium = probe.box("flow");
    if (medium == nullptr) {
        FAIL() << "missing flow\n";
    }
    if (std::fabs(medium->geo.width - 656.0f) > 1.5f) {
        FAIL() << "medium column " << medium->geo.width << "\n";
    }
    zui::html::Node* small = zui::html::find(probe.doc, "size_m");
    zui::html::Node* large_input = zui::html::find(probe.doc, "size_l");
    if (small == nullptr || large_input == nullptr) {
        FAIL() << "missing size radios\n";
    }
    small->checked = false;
    large_input->checked = true;
    const zui::css::Engine::CascadeResult cr = probe.css.cascade(probe.doc, 900.0f, 700.0f);
    bool flow_marked = false;
    for (zui::html::Node* n : cr.layout) {
        if (n != nullptr && n->id == "flow") {
            flow_marked = true;
        }
        probe.layout->mark_needs_layout(n);
    }
    if (!flow_marked) {
        FAIL() << "flow was not in the layout invalidation set (" << cr.layout.size() << ")\n";
    }
    for (zui::html::Node* n : cr.reattach) {
        probe.layout->mark_needs_layout(n);
    }
    const zui::layout::Box* refreshed = probe.box("flow");
    const float refreshed_font =
        refreshed != nullptr && refreshed->style_ptr != nullptr ? refreshed->style().font_size : -1.f;
    probe.layout->layout(probe.doc, zui::layout::Viewport{900.0f, 700.0f});
    if (refreshed_font > 0.f && std::fabs(refreshed_font - 23.f) > 0.1f) {
        FAIL() << "refresh kept font " << refreshed_font << "\n";
    }
    const zui::layout::Box* flow = probe.box("flow");
    const zui::layout::Box* para = probe.box("p1");
    if (flow == nullptr || para == nullptr) {
        FAIL() << "flow disappeared after the size change\n";
    }
    const zui::html::Node* flow_node = zui::html::find(probe.doc, "flow");
    const std::string* fs = flow_node != nullptr ? flow_node->style("font-size") : nullptr;
    const std::string* w = flow_node != nullptr ? flow_node->style("width") : nullptr;
    if (std::fabs(flow->geo.width - 800.0f) > 1.5f || std::fabs(flow->geo.world_x - 50.0f) > 1.5f) {
        FAIL() << "large column " << flow->geo.width
               << " x=" << flow->geo.world_x
               << " font=" << (fs != nullptr ? *fs : std::string("<none>"))
               << " width=" << (w != nullptr ? *w : std::string("<none>")) << "\n";
    }
    const float flow_right = flow->geo.world_x + flow->geo.width;
    const float para_right = para->geo.world_x + para->geo.width;
    if (flow_right > 900.5f) {
        FAIL() << "large column leaves the window right=" << flow_right << "\n";
    }
    if (para_right > flow_right - 27.0f) {
        FAIL() << "paragraph eats the right padding para_right=" << para_right
               << " flow_right=" << flow_right << "\n";
    }

    // The window the reader was actually on: narrower than both measures, so `min(50rem, 100%)`
    // stays the viewport. Switching from the medium size must rebreak at 23px inside the padding
    // instead of painting the medium line breaks in the larger face out through the right edge.
    Probe narrow;
    if (!narrow.run("reader_resize.html", 640.0f, 800.0f)) {
        FAIL() << narrow.error << "\n";
    }
    zui::html::Node* narrow_m = zui::html::find(narrow.doc, "size_m");
    zui::html::Node* narrow_l = zui::html::find(narrow.doc, "size_l");
    if (narrow_m == nullptr || narrow_l == nullptr) {
        FAIL() << "narrow fixture lost its size radios\n";
    }
    narrow_m->checked = false;
    narrow_l->checked = true;
    const zui::css::Engine::CascadeResult narrow_cr = narrow.css.cascade(narrow.doc, 640.0f, 800.0f);
    for (zui::html::Node* n : narrow_cr.layout) {
        narrow.layout->mark_needs_layout(n);
    }
    for (zui::html::Node* n : narrow_cr.reattach) {
        narrow.layout->mark_needs_layout(n);
    }
    narrow.layout->layout(narrow.doc, zui::layout::Viewport{640.0f, 800.0f});
    const zui::layout::Box* narrow_flow = narrow.box("flow");
    const zui::layout::Box* narrow_para = narrow.box("p1");
    if (narrow_flow == nullptr || narrow_para == nullptr) {
        FAIL() << "narrow flow disappeared after the size change\n";
    }
    const float narrow_right = narrow_flow->geo.world_x + narrow_flow->geo.width;
    const float narrow_para_right = narrow_para->geo.world_x + narrow_para->geo.width;
    if (narrow_flow->geo.world_x < -0.5f || narrow_right > 640.5f) {
        FAIL() << "narrow large column leaves the window x=" << narrow_flow->geo.world_x
               << " w=" << narrow_flow->geo.width << "\n";
    }
    if (narrow_para_right > narrow_right - 27.0f) {
        FAIL() << "narrow paragraph eats the right padding para_right=" << narrow_para_right
               << " flow_right=" << narrow_right << "\n";
    }
}

TEST(CssSpec, LengthPercentageUsedValues) {
    Probe probe;
    if (!probe.run("math.html", 1000.0f, 600.0f)) {
        FAIL() << probe.error << "\n";
    }
    auto padding_left = [&](const char* id) {
        const zui::layout::Box* box = probe.box(id);
        if (box == nullptr) {
            ADD_FAILURE() << "missing #" << id;
            return 0.0f;
        }
        return box->geo.padding.left;
    };
    // M1: `max(20px, calc((100% - 400px) / 2))` against the 1000px containing block is 300.
    if (std::fabs(padding_left("pad_math") - 300.0f) > 0.5f) {
        FAIL() << "M1 padding-left " << padding_left("pad_math") << "\n";
    }
    // The same shape without the `max` floor: `calc(100% - 900px)` is 100.
    if (std::fabs(padding_left("pad_calc") - 100.0f) > 0.5f) {
        FAIL() << "calc(100% - 900px) padding-left " << padding_left("pad_calc") << "\n";
    }
    // M1b / M1c: the reader's own page margin, `max(16px, calc((100% - 50rem) / 2))` (50rem = 800px
    // at the 16px root), on a containing block wider than the column and on one narrower than it.
    if (std::fabs(padding_left("pad_large") - 200.0f) > 0.5f) {
        FAIL() << "M1b padding-left " << padding_left("pad_large") << "\n";
    }
    if (std::fabs(padding_left("pad_narrow") - 16.0f) > 0.5f) {
        FAIL() << "M1c padding-left " << padding_left("pad_narrow") << "\n";
    }
    // A plain percentage keeps its own lane: 10% of the 1000px containing block.
    if (std::fabs(padding_left("pad_plain") - 100.0f) > 0.5f) {
        FAIL() << "10% padding-left " << padding_left("pad_plain") << "\n";
    }
    // A margin is the same family, and the percentage of an edge always uses the inline basis.
    const zui::layout::Box* margin = probe.box("mar_math");
    const zui::layout::Box* min_fit = probe.box("min_fit");
    const zui::layout::Box* max_math = probe.box("max_math");
    if (margin == nullptr || min_fit == nullptr || max_math == nullptr) {
        FAIL() << "math.html lost its margin / min-width / max-width probes\n";
    }
    if (std::fabs(margin->geo.margin.left - 100.0f) > 0.5f) {
        FAIL() << "margin-left " << margin->geo.margin.left << "\n";
    }
    // M4: `min-width: min(300px, 100%)` only shows in a box that would be narrower without it, and
    // `max-width: min(400px, 100%)` clamps a box that would stretch past it.
    if (std::fabs(min_fit->geo.width - 300.0f) > 0.5f) {
        FAIL() << "M4 min-width " << min_fit->geo.width << "\n";
    }
    if (std::fabs(max_math->geo.width - 400.0f) > 0.5f) {
        FAIL() << "M4 max-width " << max_math->geo.width << "\n";
    }
    Probe inset;
    if (!inset.run("math2.html", 1000.0f, 600.0f)) {
        FAIL() << inset.error << "\n";
    }
    const zui::layout::Box* absolute = inset.box("abs_math");
    const zui::layout::Box* gap_host = inset.box("gap_math");
    const zui::layout::Box* gap_a = inset.box("gap_a");
    const zui::layout::Box* gap_b = inset.box("gap_b");
    if (absolute == nullptr || gap_host == nullptr || gap_a == nullptr || gap_b == nullptr) {
        FAIL() << "math2.html lost its inset / gap probes\n";
    }
    // M2: `left: calc(50% - 100px)` and `top: calc(50% - 50px)` of the 1000x300 containing block.
    if (std::fabs(absolute->geo.x - 400.0f) > 0.5f ||
        std::fabs(absolute->geo.y - 100.0f) > 0.5f) {
        FAIL() << "M2 inset " << absolute->geo.x << "," << absolute->geo.y << "\n";
    }
    // M3: `gap: calc(10px + 2%)` resolves against the flex container's own content box, which is
    // the viewport minus the scrollbar gutter - so the expectation comes from that box, not from a
    // hard-coded 1000.
    const float expected_gap = 10.0f + 0.02f * gap_host->geo.content_width;
    const float actual_gap = gap_b->geo.x - (gap_a->geo.x + gap_a->geo.width);
    if (std::fabs(actual_gap - expected_gap) > 0.5f) {
        FAIL() << "M3 gap " << actual_gap << " want " << expected_gap << "\n";
    }
}

// A row flex item stretched by a column, with `max-width: min(72%, 520px)`. The percentage
// resolves against that row's definite inner size (CSS Sizing 5.2). A short line stays one line
// wide; a line longer than the cap stops at min(72% of the row, 520px). Resolving the percentage
// against the bubble's own max-content makes the short line 72% of itself and stacks the glyphs.
bool flex_max_width_at(float viewport, float* short_width, float* row_width) {
    Probe probe;
    if (!probe.run("flex_max_width_percent.html", viewport, 600.0f)) {
        ADD_FAILURE() << probe.error;
        return false;
    }
    const zui::layout::Box* row = probe.box("row");
    const zui::layout::Box* short_box = probe.box("short");
    const zui::layout::Box* medium = probe.box("medium");
    const zui::layout::Box* long_box = probe.box("long");
    const zui::layout::Box* ref_short = probe.box("ref_short");
    const zui::layout::Box* ref_medium = probe.box("ref_medium");
    if (row == nullptr || short_box == nullptr || medium == nullptr || long_box == nullptr ||
        ref_short == nullptr || ref_medium == nullptr) {
        ADD_FAILURE() << "flex_max_width_percent.html lost a probe box";
        return false;
    }
    if (row->geo.width < viewport * 0.9f) {
        ADD_FAILURE() << "message row did not stretch: " << row->geo.width << " viewport "
                      << viewport;
        return false;
    }
    if (std::fabs(short_box->geo.width - ref_short->geo.width) > 1.0f ||
        std::fabs(short_box->geo.height - ref_short->geo.height) > 1.0f) {
        ADD_FAILURE() << "short bubble " << short_box->geo.width << "x" << short_box->geo.height
                      << " ref " << ref_short->geo.width << "x" << ref_short->geo.height;
        return false;
    }
    if (std::fabs(medium->geo.width - ref_medium->geo.width) > 1.0f ||
        std::fabs(medium->geo.height - ref_medium->geo.height) > 1.0f) {
        ADD_FAILURE() << "medium bubble " << medium->geo.width << "x" << medium->geo.height
                      << " ref " << ref_medium->geo.width << "x" << ref_medium->geo.height;
        return false;
    }
    if (medium->geo.width <= short_box->geo.width + 1.0f) {
        ADD_FAILURE() << "medium text measured no wider than the short one: " << medium->geo.width;
        return false;
    }
    const float cap = std::min(520.0f, 0.72f * row->geo.width);
    if (std::fabs(long_box->geo.width - cap) > 1.0f) {
        ADD_FAILURE() << "long bubble " << long_box->geo.width << " cap " << cap << " row "
                      << row->geo.width;
        return false;
    }
    *short_width = short_box->geo.width;
    *row_width = row->geo.width;
    return true;
}

TEST(CssSpec, FlexItemPercentageMaxWidthUsesTheRow) {
    float short_narrow = 0.0f;
    float row_narrow = 0.0f;
    float short_wide = 0.0f;
    float row_wide = 0.0f;
    if (!flex_max_width_at(1000.0f, &short_narrow, &row_narrow) ||
        !flex_max_width_at(1800.0f, &short_wide, &row_wide)) {
        return;
    }
    if (std::fabs(short_narrow - short_wide) > 1.0f) {
        FAIL() << "short bubble grew with the window: " << short_narrow << " -> " << short_wide
               << "\n";
    }
    if (row_wide < row_narrow + 400.0f) {
        FAIL() << "message row did not grow: " << row_narrow << " -> " << row_wide << "\n";
    }
}

// `布局可见性与规范修补方案.md` M5 (the governance notes' section 9.5): matching must not cost the
// square of the sibling count. Counted in `:has()` evaluations rather than in seconds - the same
// number on every machine, where a clock makes the test flaky.
TEST(CssSpec, HasCostDoesNotGrowWithTheSquareOfSiblings) {
    ManyButtons small;
    ManyButtons large;
    if (!small.run(200) || !large.run(2000)) {
        FAIL() << "could not load the :has() fixture\n";
    }
    ZUI_FILE_INFO("many_has :has() calls: 200={} 2000={}", small.has_calls(), large.has_calls());
    if (small.has_calls() == 0) {
        FAIL() << "the fixture's :has() rules never ran\n";
    }
    // Ten times the siblings: quadratic growth is ~100x and linear is 10x, but with the id out of
    // the share key the count does not grow at all (every `.ch` but the first and the last answers
    // from the cache). The gate only has to catch the square, so two of them are allowed for slack.
    if (large.has_calls() > small.has_calls() * 2) {
        FAIL() << ":has() evaluations grew with the sibling count: " << small.has_calls() << " -> "
               << large.has_calls() << "\n";
    }
    // Sharing a key must not change what gets computed: the first, a middle and the last button all
    // carry the fixture's declarations.
    const zui::html::Node* first = zui::html::find(large.doc, "ch1");
    const zui::html::Node* middle = zui::html::find(large.doc, "ch1000");
    const zui::html::Node* last = zui::html::find(large.doc, "ch2000");
    if (first == nullptr || middle == nullptr || last == nullptr) {
        FAIL() << "the generated page lost its buttons\n";
    }
    for (const zui::html::Node* node : {first, middle, last}) {
        if (!eq(node, "display", "block") || !eq(node, "text-align", "left")) {
            FAIL() << "a .ch button lost its computed style\n";
        }
    }
}

// `布局可见性与规范修补方案.md` M6: `contain-intrinsic-size` follows its grammar
// `auto? [ none | <length> ]{1,2}` (CSS Sizing section 5.3). Those boxes are skipped
// unconditionally and shrink to fit, so the placeholder *is* the size the layout reports.
TEST(CssSpec, ContainIntrinsicSizeSyntax) {
    Probe probe;
    if (!probe.run("contain_intrinsic.html", 800.0f, 600.0f)) {
        FAIL() << probe.error << "\n";
    }
    auto expect_size = [&](const char* id, float want_w, float want_h) {
        const zui::layout::Box* box = probe.box(id);
        if (box == nullptr) {
            ADD_FAILURE() << "missing #" << id;
            return;
        }
        if (std::fabs(box->geo.width - want_w) > 0.5f ||
            std::fabs(box->geo.height - want_h) > 0.5f) {
            FAIL() << "#" << id << " placeholder " << box->geo.width << "x" << box->geo.height
                   << " want " << want_w << "x" << want_h << "\n";
        }
    };
    // M6: `auto 38px` keeps the 38px fallback - the zero placeholder was the defect.
    expect_size("auto_len", 38.0f, 38.0f);
    // `auto none` has no fallback, and this engine remembers no size, so there is none.
    expect_size("auto_none", 0.0f, 0.0f);
    expect_size("plain", 24.0f, 24.0f);
    expect_size("pair", 30.0f, 46.0f);
    expect_size("longhand", 20.0f, 12.0f);
}

// `布局可见性与规范修补方案.md` M8, as the engine's side of it: `content-visibility: auto` plus
// `contain-intrinsic-size` make the layout skip the list items the previous frame left outside the
// viewport, and the placeholder keeps the list's scroll extent so its last item stays reachable.
// The **first** frame lays everything out - the relevance test reads the previous frame's world
// rectangles and a brand-new box has none (section 4.5) - and the second pays for flipping the
// skip flags. From the third frame the list costs nothing: `laid` stops following the item count.
// This is a fixture for those two standard CSS features, not a change to the reader: `reader.css`
// renders correctly in a browser and is left alone.
TEST(CssSpec, ContentVisibilitySkipsOffscreenListItems) {
    const std::size_t chapters = 400;
    zui::html::Document doc = make_long_list(chapters);
    zui::css::Engine css;
    std::string error;
    if (!css.load_sheets(doc, error)) {
        FAIL() << error << "\n";
    }
    css.cascade(doc, 800.0f, 400.0f);
    zui::layout::FreeTypeTextMetrics metrics;
    if (!metrics.ready()) {
        FAIL() << "no font\n";
    }
    zui::layout::LayoutEngine layout(metrics);
    const zui::layout::Viewport viewport{800.0f, 400.0f};
    layout.layout(doc, viewport);
    const std::size_t cold = layout.frame_stats().laid_out;
    layout.layout(doc, viewport);
    const std::size_t settling = layout.frame_stats().laid_out;
    layout.layout(doc, viewport);
    const std::size_t settled = layout.frame_stats().laid_out;
    ZUI_FILE_INFO("long_list_auto laid: cold={} settle={} settled={} items={}", cold, settling,
                  settled, chapters);
    // The first frame is the documented gap: the relevance test reads the previous frame's world
    // rectangles and a brand-new box has none, so everything is laid out once.
    if (cold <= chapters) {
        FAIL() << "the first frame was expected to lay every item, laid " << cold << "\n";
    }
    // The second frame is the one that flips the skip flags, so it still pays per item - but less
    // than the first, because a skipped box takes the placeholder path instead of its content.
    if (settling >= cold) {
        FAIL() << "content-visibility never took effect: " << cold << " -> " << settling << "\n";
    }
    // From the third frame the list costs nothing at all: `laid` stops following the item count
    // (measured 0 at 100, 400 and 1600 items). That is M8's "decoupled from the chapter count".
    if (settled > 16) {
        FAIL() << "a settled frame laid " << settled << " boxes for " << chapters << " items\n";
    }
    const zui::layout::Box* scroller = layout.principal_box(zui::html::find(doc, "scroller"));
    zui::html::Node* scroller_node = zui::html::find(doc, "scroller");
    if (scroller == nullptr || scroller_node == nullptr) {
        FAIL() << "missing #scroller\n";
    }
    // ...and the placeholder keeps the list's scroll extent, so its end is reachable. Both bounds
    // matter: `contain-intrinsic-size` is a *content* size (CSS Sizing 4 section 5.2), so a fixture
    // that feeds it the row's border-box height makes every skipped row taller than a laid-out one
    // and inflates the extent - which is exactly what the upper bound catches.
    const float want_extent = static_cast<float>(chapters) * 38.3f;
    if (scroller->geo.scroll_height < 0.98f * want_extent ||
        scroller->geo.scroll_height > 1.02f * want_extent) {
        FAIL() << "scroll height " << scroller->geo.scroll_height << " want about " << want_extent
               << " - a placeholder that misses the row height by the padding shows up here\n";
    }
    // Ask the pane for the end the way a drag does - set the offset past the end and let the engine
    // clamp it - and give it a few frames: the placeholders of the items that just became relevant
    // settle on the frame after the scroll, which moves the end.
    const zui::layout::Box* last = nullptr;
    // The id follows `chapters`: the sweep over 100 / 400 / 1600 rows is done by editing that one
    // constant, and a hard-coded "ch400" would quietly measure nothing at the other two.
    const std::string last_id = "ch" + std::to_string(chapters);
    for (int attempt = 0; attempt < 3; ++attempt) {
        scroller_node->scroll_y = std::max(0.0f, scroller->geo.scroll_height - scroller->geo.height);
        layout.sync_world();
        layout.layout(doc, viewport);
        scroller = layout.principal_box(zui::html::find(doc, "scroller"));
        last = layout.principal_box(zui::html::find(doc, last_id));
        if (scroller == nullptr || last == nullptr) {
            break;
        }
    }
    if (last == nullptr) {
        FAIL() << "the list lost its last item\n";
    }
    // "Reachable" is measured against the pane's own box, not the viewport: the fixture's scroller
    // starts 8px down the page, so its last 8px fall outside the 400px viewport even though
    // scrolling does bring the last item to the pane's bottom edge.
    const float pane_top = scroller->geo.world_y;
    const float pane_bottom = pane_top + scroller->geo.height;
    if (last->geo.world_y < pane_top - 1.0f ||
        last->geo.world_y + last->geo.height > pane_bottom + 1.0f) {
        FAIL() << "the last item spans " << last->geo.world_y << ".."
               << last->geo.world_y + last->geo.height << " - outside the pane " << pane_top << ".."
               << pane_bottom << "\n";
    }
}

// `布局可见性与规范修补方案.md` M7 / M7b: `content-visibility: auto` changes the *used* value of
// `contain` (CSS Containment section 4) without changing the computed one, and `contain: layout`
// on its own already establishes the containing block that section 3.3 effect 4 and the stacking
// context that section 3.4 promise. The engine has to reach both through either clause: the probe
// boxes are `position: static` and carry no transform / filter / perspective / will-change, so
// nothing else can hand them a containing block, and their out-of-flow child measures it - at the
// containment box's padding edge (x = its 40px margin) when the parent is the containing block,
// at the page origin when it falls through to the initial one.
// `contain: style` is deliberately not wired: this engine scopes no counters or quotes.
TEST(CssSpec, ContentVisibilityImpliesTheContainmentItCanLand) {
    Probe probe;
    if (!probe.run("containment.html", 800.0f, 600.0f)) {
        FAIL() << probe.error << "\n";
    }
    auto expect_contained = [&](const char* id, bool want) {
        const zui::layout::Box* box = probe.box(id);
        if (box == nullptr) {
            ADD_FAILURE() << "missing #" << id;
            return;
        }
        const zui::layout::LayoutStyle& style = box->style();
        if (style.cb_for_absolute != want || style.cb_for_fixed != want) {
            FAIL() << "#" << id << " is abs=" << style.cb_for_absolute
                   << " fixed=" << style.cb_for_fixed << " containing block, want " << want
                   << "\n";
        }
        if (zui::layout::stacking_info(*box).context != want) {
            FAIL() << "#" << id << " stacking context mismatch, want " << want << "\n";
        }
    };
    // M7b: `contain: layout` alone - no `contain: paint` to lean on - still delivers both.
    expect_contained("lay", true);
    // M7: `content-visibility: auto` reaches the same used value through its own clause.
    expect_contained("viz", true);
    // The negative control: without either clause the box is neither, which is what makes the
    // probe able to tell the two answers apart.
    expect_contained("plain", false);
    // ...and the containing block is real, not just a stored flag: each child sits on its parent's
    // padding edge, and the one whose parent is not a containing block lands at the page origin.
    auto expect_place = [&](const char* id, float want_x, float want_y) {
        const zui::layout::Box* box = probe.box(id);
        if (box == nullptr) {
            ADD_FAILURE() << "missing #" << id;
            return;
        }
        if (std::fabs(box->geo.world_x - want_x) > 0.5f ||
            std::fabs(box->geo.world_y - want_y) > 0.5f) {
            FAIL() << "#" << id << " sits at " << box->geo.world_x << "," << box->geo.world_y
                   << " want " << want_x << "," << want_y << "\n";
        }
    };
    expect_place("lay_abs", 40.0f, 0.0f);
    expect_place("viz_abs", 40.0f, 60.0f);
    expect_place("plain_abs", 0.0f, 0.0f);
}


// Viewport units are baked when a style is interned. A second layout with a new viewport has to
// re-intern those boxes: percentages already follow the constraint space, `100dvh` did not, which
// left the reader shell at the height of the first window.
TEST(CssSpec, ViewportUnitsFollowResize) {
    Probe probe;
    if (!probe.run("viewport_resize.html", 1100.0f, 720.0f)) {
        FAIL() << probe.error << "\n";
    }
    const zui::layout::Box* app = probe.box("app");
    const zui::layout::Box* side = probe.box("side");
    const zui::layout::Box* bar = probe.box("bar");
    const zui::layout::Box* fixed = probe.box("fixed");
    if (app == nullptr || side == nullptr || bar == nullptr || fixed == nullptr) {
        FAIL() << "missing box\n";
    }
    if (std::fabs(app->geo.height - 720.0f) > 1.0f) {
        FAIL() << "app height " << app->geo.height << "\n";
    }
    for (int i = 0; i < 3; ++i) {
        probe.layout->layout(probe.doc, zui::layout::Viewport{1100.0f, 720.0f});
    }
    if (std::fabs(app->geo.height - 720.0f) > 1.0f) {
        FAIL() << "settled app height " << app->geo.height << "\n";
    }
    if (std::fabs(side->geo.width - 550.0f) > 1.0f || std::fabs(side->geo.height - 72.0f) > 1.0f) {
        FAIL() << "side " << side->geo.width << "x" << side->geo.height << "\n";
    }
    if (std::fabs(bar->geo.height - 72.0f) > 1.0f || std::fabs(fixed->geo.height - 200.0f) > 1.0f) {
        FAIL() << "bar " << bar->geo.height << " fixed " << fixed->geo.height << "\n";
    }
    probe.layout->layout(probe.doc, zui::layout::Viewport{1100.0f, 1400.0f});
    if (std::fabs(app->geo.height - 1400.0f) > 1.0f) {
        FAIL() << "resized app height " << app->geo.height << "\n";
    }
    if (std::fabs(side->geo.width - 550.0f) > 1.0f || std::fabs(side->geo.height - 140.0f) > 1.0f) {
        FAIL() << "resized side " << side->geo.width << "x" << side->geo.height << "\n";
    }
    if (std::fabs(bar->geo.height - 140.0f) > 1.0f || std::fabs(fixed->geo.height - 200.0f) > 1.0f) {
        FAIL() << "resized bar " << bar->geo.height << " fixed " << fixed->geo.height << "\n";
    }
    probe.layout->layout(probe.doc, zui::layout::Viewport{800.0f, 1400.0f});
    if (std::fabs(side->geo.width - 400.0f) > 1.0f || std::fabs(app->geo.height - 1400.0f) > 1.0f) {
        FAIL() << "narrow side " << side->geo.width << " app " << app->geo.height << "\n";
    }
}

// CSS Values 4: min() and max() take one or more arguments. A two-argument-only parser
// drops `min(100px, 50px, 25px)` and the block stretches to the containing block.
TEST(CssSpec, MinMaxTakesOneOrMoreArguments) {
    Probe probe;
    if (!probe.run("min_arity.html", 1000.0f, 400.0f)) {
        FAIL() << probe.error << "\n";
    }
    const zui::layout::Box* three = probe.box("three");
    const zui::layout::Box* one = probe.box("one");
    const zui::layout::Box* nested = probe.box("nested");
    const zui::layout::Box* quarter = probe.box("q");
    if (three == nullptr || one == nullptr || nested == nullptr || quarter == nullptr) {
        FAIL() << "min_arity.html lost a probe box\n";
    }
    if (std::fabs(three->geo.width - 25.0f) > 0.5f) {
        FAIL() << "min() of three lengths " << three->geo.width << "\n";
    }
    if (std::fabs(one->geo.width - 40.0f) > 0.5f) {
        FAIL() << "min() of one length " << one->geo.width << "\n";
    }
    if (std::fabs(nested->geo.width - 15.0f) > 0.5f) {
        FAIL() << "calc(min(...) + 5px) " << nested->geo.width << "\n";
    }
    // 4Q = 1mm = 96/25.4 CSS px.
    const float one_mm = 96.0f / 25.4f;
    if (std::fabs(quarter->geo.width - one_mm) > 0.05f) {
        FAIL() << "4Q " << quarter->geo.width << " want " << one_mm << "\n";
    }
}

// CSS Fonts absolute-size keywords are used lengths, not an inherit fallback.
// `font-size: medium` inside a 32px parent is the initial 16px.
TEST(CssSpec, FontSizeKeywordsAreUsedLengths) {
    Probe probe;
    if (!probe.run("font_keywords.html", 800.0f, 400.0f)) {
        FAIL() << probe.error << "\n";
    }
    const zui::layout::Box* medium = probe.box("medium");
    const zui::layout::Box* small = probe.box("small");
    const zui::layout::Box* large = probe.box("large");
    const zui::layout::Box* smaller = probe.box("smaller");
    const zui::layout::Box* larger = probe.box("larger");
    const zui::layout::Box* root_large = probe.box("root-large");
    if (medium == nullptr || small == nullptr || large == nullptr || smaller == nullptr ||
        larger == nullptr || root_large == nullptr) {
        FAIL() << "font_keywords.html lost a probe box\n";
    }
    if (std::fabs(medium->style().font_size - 16.0f) > 0.05f) {
        FAIL() << "medium " << medium->style().font_size << "\n";
    }
    if (std::fabs(small->style().font_size - 13.0f) > 0.05f) {
        FAIL() << "small " << small->style().font_size << "\n";
    }
    if (std::fabs(large->style().font_size - 18.0f) > 0.05f) {
        FAIL() << "large " << large->style().font_size << "\n";
    }
    if (std::fabs(smaller->style().font_size - (32.0f / 1.2f)) > 0.05f) {
        FAIL() << "smaller " << smaller->style().font_size << "\n";
    }
    if (std::fabs(larger->style().font_size - (32.0f * 1.2f)) > 0.05f) {
        FAIL() << "larger " << larger->style().font_size << "\n";
    }
    if (std::fabs(root_large->style().font_size - 18.0f) > 0.05f) {
        FAIL() << "root large " << root_large->style().font_size << "\n";
    }
    // CSS 2.1 §15.7 steps the absolute-size table from medium (and from the
    // initial medium when font-size is dropped). A 32px length still scales by 1.2.
    const zui::layout::Box* med_larger = probe.box("med-larger");
    const zui::layout::Box* med_smaller = probe.box("med-smaller");
    const zui::layout::Box* init_larger = probe.box("init-larger");
    const zui::layout::Box* init_smaller = probe.box("init-smaller");
    if (med_larger == nullptr || med_smaller == nullptr || init_larger == nullptr ||
        init_smaller == nullptr) {
        FAIL() << "font_keywords.html lost a keyword-step box\n";
    }
    if (std::fabs(med_larger->style().font_size - 18.0f) > 0.05f) {
        FAIL() << "medium+larger " << med_larger->style().font_size << "\n";
    }
    if (std::fabs(med_smaller->style().font_size - 13.0f) > 0.05f) {
        FAIL() << "medium+smaller " << med_smaller->style().font_size << "\n";
    }
    if (std::fabs(init_larger->style().font_size - 18.0f) > 0.05f) {
        FAIL() << "initial+larger " << init_larger->style().font_size << "\n";
    }
    if (std::fabs(init_smaller->style().font_size - 13.0f) > 0.05f) {
        FAIL() << "initial+smaller " << init_smaller->style().font_size << "\n";
    }
}
