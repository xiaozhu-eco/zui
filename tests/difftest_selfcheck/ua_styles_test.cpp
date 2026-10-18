// User-agent defaults for blocks, dialog, ruby, lists, and cold tags.
// L6 architecture self-checks (TESTPLAN section 12).
// Shared scene harness: selfcheck_scene.h.

#include "selfcheck_scene.h"

using namespace difftest_selfcheck;

// Chromium html.css subset: common block/inline UA defaults (footer/blockquote/hr/pre/…).
TEST(DifftestSelfCheck, UaCommonBlocksAndPhrase) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "ua-blocks.html")) << error;

    auto style_of = [&](const char* id)
        -> decltype(static_cast<zui::html::Node*>(nullptr)->computed.get()) {
        zui::html::Node* n = zui::html::find(scene.doc, id);
        EXPECT_NE(n, nullptr) << id;
        if (n == nullptr) {
            return nullptr;
        }
        EXPECT_NE(n->computed, nullptr) << id;
        return n->computed.get();
    };
    auto prop = [](const auto* m,
                   const char* key) -> std::string {
        if (m == nullptr) {
            return "<missing-map>";
        }
        const auto it = m->find(key);
        return it == m->end() ? std::string("<absent>") : std::string(it->second);
    };

    const auto* foot = style_of("foot");
    ASSERT_NE(foot, nullptr);
    EXPECT_EQ(prop(foot, "display"), "block");

    const auto* side = style_of("side");
    ASSERT_NE(side, nullptr);
    EXPECT_EQ(prop(side, "display"), "block");

    const auto* bq = style_of("bq");
    ASSERT_NE(bq, nullptr);
    EXPECT_EQ(prop(bq, "display"), "block");
    EXPECT_EQ(prop(bq, "margin-block"), "1em");
    EXPECT_EQ(prop(bq, "margin-inline"), "40px");
    EXPECT_EQ(prop(bq, "margin-left"), "40px") << "LTR maps margin-inline → left/right";
    EXPECT_EQ(prop(bq, "margin-right"), "40px");

    const auto* fig = style_of("fig");
    ASSERT_NE(fig, nullptr);
    EXPECT_EQ(prop(fig, "display"), "block");
    EXPECT_EQ(prop(fig, "margin-inline"), "40px");

    const auto* cap = style_of("cap");
    ASSERT_NE(cap, nullptr);
    EXPECT_EQ(prop(cap, "display"), "block");

    const auto* addr = style_of("addr");
    ASSERT_NE(addr, nullptr);
    EXPECT_EQ(prop(addr, "display"), "block");
    EXPECT_EQ(prop(addr, "font-style"), "italic");

    const auto* ctr = style_of("ctr");
    ASSERT_NE(ctr, nullptr);
    EXPECT_EQ(prop(ctr, "display"), "block");
    EXPECT_EQ(prop(ctr, "text-align"), "center");

    const auto* pre = style_of("pre");
    ASSERT_NE(pre, nullptr);
    EXPECT_EQ(prop(pre, "display"), "block");
    EXPECT_EQ(prop(pre, "white-space"), "pre");
    EXPECT_EQ(prop(pre, "font-family"), "monospace");
    EXPECT_EQ(prop(pre, "margin-block"), "1em");

    const auto* rule = style_of("rule");
    ASSERT_NE(rule, nullptr);
    EXPECT_EQ(prop(rule, "display"), "block");
    EXPECT_EQ(prop(rule, "border-top-style"), "inset");
    EXPECT_EQ(prop(rule, "border-top-width"), "1px");
    EXPECT_EQ(prop(rule, "margin-block"), "0.5em");
    EXPECT_EQ(prop(rule, "margin-inline"), "auto");

    zui::layout::Box* hr_box = scene.box("rule");
    ASSERT_NE(hr_box, nullptr);
    EXPECT_NEAR(hr_box->geo.border.top, 1.f, 0.01f);
    EXPECT_NEAR(hr_box->geo.border.bottom, 1.f, 0.01f);

    const auto* code = style_of("code");
    ASSERT_NE(code, nullptr);
    EXPECT_EQ(prop(code, "font-family"), "monospace");

    const auto* strong = style_of("strong");
    ASSERT_NE(strong, nullptr);
    EXPECT_EQ(prop(strong, "font-weight"), "bolder");

    const auto* em = style_of("em");
    ASSERT_NE(em, nullptr);
    EXPECT_EQ(prop(em, "font-style"), "italic");

    const auto* h1 = style_of("h1");
    ASSERT_NE(h1, nullptr);
    EXPECT_EQ(prop(h1, "font-weight"), "bold");
    EXPECT_EQ(prop(h1, "font-size"), "2em");

    const auto* frm = style_of("frm");
    ASSERT_NE(frm, nullptr);
    EXPECT_EQ(prop(frm, "display"), "block");
}

// Chromium html.css residual: dialog open/closed, ruby/rt/rp, nested h1 shrink, u/s/del/ins.
TEST(DifftestSelfCheck, UaDialogRubyNestedH1) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "ua-dialog-ruby.html")) << error;

    auto style_of = [&](const char* id)
        -> decltype(static_cast<zui::html::Node*>(nullptr)->computed.get()) {
        zui::html::Node* n = zui::html::find(scene.doc, id);
        EXPECT_NE(n, nullptr) << id;
        if (n == nullptr) {
            return nullptr;
        }
        EXPECT_NE(n->computed, nullptr) << id;
        return n->computed.get();
    };
    auto prop = [](const auto* m,
                   const char* key) -> std::string {
        if (m == nullptr) {
            return "<missing-map>";
        }
        const auto it = m->find(key);
        return it == m->end() ? std::string("<absent>") : std::string(it->second);
    };

    const auto* closed = style_of("dlg-closed");
    ASSERT_NE(closed, nullptr);
    EXPECT_EQ(prop(closed, "display"), "none");
    EXPECT_EQ(prop(closed, "position"), "absolute");

    const auto* open = style_of("dlg-open");
    ASSERT_NE(open, nullptr);
    EXPECT_EQ(prop(open, "display"), "block");
    // A declarative `open` shows the dialog *non-modally*: HTML puts the top layer, `:modal` and
    // `::backdrop` behind `showModal()`. The pinned browser reports `position: absolute` for it and
    // paints no backdrop, so the plain `dialog` UA rule applies and `dialog:modal` stays dormant.
    EXPECT_EQ(prop(open, "position"), "absolute");
    EXPECT_EQ(prop(open, "left"), "0");
    EXPECT_EQ(prop(open, "right"), "0");
    EXPECT_EQ(prop(open, "width"), "fit-content");
    EXPECT_EQ(prop(open, "margin-left"), "auto");
    EXPECT_EQ(prop(open, "margin-right"), "auto");
    EXPECT_EQ(prop(open, "padding-top"), "1em");
    EXPECT_EQ(prop(open, "border-top-style"), "solid");
    EXPECT_NE(scene.box("dlg-open"), nullptr);
    EXPECT_EQ(scene.box("dlg-closed"), nullptr) << "closed dialog is display:none → no box";
    zui::html::Node* open_n = zui::html::find(scene.doc, "dlg-open");
    ASSERT_NE(open_n, nullptr);
    EXPECT_FALSE(open_n->modal);
    EXPECT_EQ(open_n->backdrop, nullptr) << "only a modal dialog cascades ::backdrop";

    const auto* ruby = style_of("rb");
    ASSERT_NE(ruby, nullptr);
    EXPECT_EQ(prop(ruby, "display"), "ruby");
    const auto* rt = style_of("rt");
    ASSERT_NE(rt, nullptr);
    EXPECT_EQ(prop(rt, "display"), "ruby-text");
    EXPECT_EQ(prop(rt, "font-size"), "50%");
    const auto* rp = style_of("rp");
    ASSERT_NE(rp, nullptr);
    EXPECT_EQ(prop(rp, "display"), "none");
    EXPECT_EQ(scene.box("rp"), nullptr);
    zui::layout::Box* ruby_box = scene.box("rb");
    ASSERT_NE(ruby_box, nullptr);
    EXPECT_EQ(ruby_box->style().outer, zui::layout::DisplayOuter::Inline);
    EXPECT_TRUE(ruby_box->atomic_inline) << "display:ruby is an atomic inline for layout_ruby";

    EXPECT_EQ(prop(style_of("u"), "text-decoration"), "underline");
    EXPECT_EQ(prop(style_of("ins"), "text-decoration"), "underline");
    EXPECT_EQ(prop(style_of("s"), "text-decoration"), "line-through");
    EXPECT_EQ(prop(style_of("strike"), "text-decoration"), "line-through");
    EXPECT_EQ(prop(style_of("del"), "text-decoration"), "line-through");

    EXPECT_EQ(prop(style_of("h1-root"), "font-size"), "2em");
    EXPECT_EQ(prop(style_of("h1-d1"), "font-size"), "1.5em");
    EXPECT_EQ(prop(style_of("h1-d2"), "font-size"), "1.17em");
    EXPECT_EQ(prop(style_of("h1-d3"), "font-size"), "1em");
    EXPECT_EQ(prop(style_of("h1-d4"), "font-size"), "0.83em");
    EXPECT_EQ(prop(style_of("h1-d5"), "font-size"), "0.67em");
}

// B1 true ruby: annotation (rt) sits above the base on the block axis; container inline size is
// at least as wide as either run (CSS Ruby simple ruby / ruby-position:over).
TEST(DifftestSelfCheck, RubyAnnotationAboveBase) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "ruby-layout.html")) << error;

    zui::layout::Box* ruby = scene.box("rb");
    zui::layout::Box* rt = scene.box("rt");
    ASSERT_NE(ruby, nullptr);
    ASSERT_NE(rt, nullptr);
    EXPECT_TRUE(ruby->atomic_inline);
    EXPECT_EQ(ruby->style().outer, zui::layout::DisplayOuter::Inline);

    // Annotation overhangs above the base (Chrome keeps the line on the base strut).
    EXPECT_LT(rt->geo.y + rt->geo.height, ruby->geo.y + ruby->geo.height + 0.5f);
    EXPECT_LE(rt->geo.y + rt->geo.height, ruby->geo.y + 0.5f)
        << "rt bottom should meet or sit above the ruby/base top";
    // Container advances by max(base, annotation).
    EXPECT_GE(ruby->geo.width + 0.5f, rt->geo.width);
}

// B1: declarative `open` is the *non-modal* dialog; `:modal` / `::backdrop` stay dormant until a
// host `showModal()` exists (paint of the non-modal dialog is covered by `ua-dialog-modal`).
TEST(DifftestSelfCheck, UaDialogModalBackdrop) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "ua-dialog-modal.html")) << error;

    auto prop = [](const zui::html::Node* n, const char* key) -> std::string {
        if (n == nullptr || n->computed == nullptr) {
            return "<missing>";
        }
        const auto it = n->computed->find(key);
        return it == n->computed->end() ? std::string("<absent>") : std::string(it->second);
    };

    zui::html::Node* modal = zui::html::find(scene.doc, "dlg-modal");
    zui::html::Node* plain = zui::html::find(scene.doc, "dlg-plain");
    ASSERT_NE(modal, nullptr);
    ASSERT_NE(plain, nullptr);
    EXPECT_FALSE(modal->modal);
    EXPECT_FALSE(plain->modal);
    EXPECT_EQ(prop(modal, "display"), "block");
    EXPECT_EQ(prop(modal, "position"), "absolute");
    // `dialog:modal`'s `overflow: auto` is not applied, and the engine's computed map only holds
    // properties a stylesheet set, so `overflow` stays absent here (the trace fills it from
    // props.json's `initial`).
    EXPECT_EQ(modal->backdrop, nullptr);
    EXPECT_EQ(plain->backdrop, nullptr);
    EXPECT_EQ(prop(plain, "display"), "none");
    EXPECT_NE(scene.box("dlg-modal"), nullptr);
    EXPECT_EQ(scene.box("dlg-plain"), nullptr);
}

// B1 UA cold tags / form residual (Chromium html.css long tail beyond dialog/ruby).
TEST(DifftestSelfCheck, UaColdTagsFormResidual) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "ua-cold-tags.html")) << error;

    auto style_of = [&](const char* id)
        -> decltype(static_cast<zui::html::Node*>(nullptr)->computed.get()) {
        zui::html::Node* n = zui::html::find(scene.doc, id);
        EXPECT_NE(n, nullptr) << id;
        if (n == nullptr) {
            return nullptr;
        }
        EXPECT_NE(n->computed, nullptr) << id;
        return n->computed.get();
    };
    auto prop = [](const auto* m,
                   const char* key) -> std::string {
        if (m == nullptr) {
            return "<missing-map>";
        }
        const auto it = m->find(key);
        return it == m->end() ? std::string("<absent>") : std::string(it->second);
    };

    const auto* fs = style_of("fs");
    ASSERT_NE(fs, nullptr);
    EXPECT_EQ(prop(fs, "display"), "block");
    EXPECT_EQ(prop(fs, "border-top-style"), "groove");
    EXPECT_EQ(prop(fs, "border-top-width"), "2px");
    EXPECT_EQ(prop(fs, "margin-inline"), "2px");
    EXPECT_EQ(prop(fs, "padding-inline"), "0.75em");
    EXPECT_NE(scene.box("fs"), nullptr);

    const auto* leg = style_of("leg");
    ASSERT_NE(leg, nullptr);
    EXPECT_EQ(prop(leg, "display"), "block");
    EXPECT_EQ(prop(leg, "padding-inline"), "2px");

    const auto* meter = style_of("m");
    ASSERT_NE(meter, nullptr);
    EXPECT_EQ(prop(meter, "display"), "inline-block");
    EXPECT_EQ(prop(meter, "width"), "5em");
    EXPECT_EQ(prop(meter, "height"), "1em");
    EXPECT_EQ(prop(meter, "vertical-align"), "-0.2em");
    zui::layout::Box* meter_box = scene.box("m");
    ASSERT_NE(meter_box, nullptr);
    EXPECT_NEAR(meter_box->geo.width, 80.f, 0.5f) << "5em at 16px";
    EXPECT_NEAR(meter_box->geo.height, 16.f, 0.5f);

    EXPECT_EQ(prop(style_of("out"), "display"), "inline");
    EXPECT_EQ(prop(style_of("out"), "unicode-bidi"), "isolate");

    const auto* mq = style_of("mq");
    ASSERT_NE(mq, nullptr);
    EXPECT_EQ(prop(mq, "display"), "inline-block");
    EXPECT_EQ(prop(mq, "width"), "100%");

    const auto* fr = style_of("fr");
    ASSERT_NE(fr, nullptr);
    EXPECT_EQ(prop(fr, "border-top-style"), "inset");
    EXPECT_EQ(prop(fr, "border-top-width"), "2px");
    EXPECT_TRUE(prop(fr, "overflow") == "clip" || prop(fr, "overflow-x") == "clip")
        << "overflow=" << prop(fr, "overflow") << " overflow-x=" << prop(fr, "overflow-x");

    EXPECT_EQ(prop(style_of("vid"), "object-fit"), "contain");
    EXPECT_TRUE(prop(style_of("vid"), "overflow") == "clip" ||
                prop(style_of("vid"), "overflow-x") == "clip");
    EXPECT_TRUE(prop(style_of("cv"), "overflow") == "clip" ||
                prop(style_of("cv"), "overflow-x") == "clip");

    const auto* aud = style_of("aud");
    ASSERT_NE(aud, nullptr);
    EXPECT_EQ(prop(aud, "width"), "300px");
    EXPECT_EQ(prop(aud, "height"), "54px");
    EXPECT_NE(scene.box("aud"), nullptr);
    EXPECT_EQ(prop(style_of("aud-hide"), "display"), "none");
    EXPECT_EQ(scene.box("aud-hide"), nullptr);

    EXPECT_EQ(prop(style_of("bdi"), "unicode-bidi"), "isolate");
    EXPECT_EQ(prop(style_of("bdo"), "unicode-bidi"), "isolate-override");
    // Cascade keeps longhand order as underline then style (Chromium UA writes "dotted underline").
    EXPECT_EQ(prop(style_of("ab"), "text-decoration"), "underline dotted");
    EXPECT_EQ(prop(style_of("mk"), "background-color"), "#ffff00");
    EXPECT_EQ(prop(style_of("sm"), "font-size"), "0.833333em");
    EXPECT_EQ(prop(style_of("sub"), "vertical-align"), "sub");
    EXPECT_EQ(prop(style_of("sub"), "font-size"), "0.833333em");
    EXPECT_EQ(prop(style_of("sup"), "vertical-align"), "super");
    EXPECT_EQ(prop(style_of("sup"), "font-size"), "0.833333em");
    EXPECT_EQ(prop(style_of("samp"), "font-family"), "monospace");
    EXPECT_EQ(prop(style_of("kbd"), "font-family"), "monospace");
    EXPECT_EQ(prop(style_of("vr"), "font-style"), "italic");

    EXPECT_EQ(prop(style_of("dl"), "display"), "none");
    EXPECT_EQ(scene.box("dl"), nullptr);
    // The datalist's own option is `display: block` in the browser and has no box here: Chrome
    // reports `has_box` from the element's own display, the list inside a `datalist` is hidden by
    // the datalist's own `display: none`.
    EXPECT_EQ(prop(style_of("dopt"), "display"), "block");
    EXPECT_EQ(prop(style_of("ar"), "display"), "none");
    EXPECT_EQ(scene.box("ar"), nullptr);
    EXPECT_EQ(prop(style_of("pm"), "display"), "none");
    EXPECT_EQ(prop(style_of("sl"), "display"), "contents");
    EXPECT_EQ(scene.box("sl"), nullptr) << "display:contents builds no box";

    EXPECT_EQ(prop(style_of("tbl"), "display"), "table");
    EXPECT_EQ(prop(style_of("tbl"), "border-collapse"), "separate");
    EXPECT_EQ(prop(style_of("tbl"), "border-spacing"), "2px");
    EXPECT_EQ(prop(style_of("cap"), "display"), "table-caption");
    EXPECT_EQ(prop(style_of("col"), "display"), "table-column");
    EXPECT_EQ(prop(style_of("cg"), "display"), "table-column-group");

    // HTML rendering section 15.5.16: an `optgroup` is `display: block` - even inside a closed
    // `<select>`. What keeps the list off the page is the shadow tree Chrome swaps in; the engine
    // has none, so the stand-in skips a closed select's subtree in the box tree (the popup test
    // above covers the open state, which builds the option boxes).
    EXPECT_EQ(prop(style_of("og"), "display"), "block");
    EXPECT_EQ(prop(style_of("og"), "font-weight"), "bolder");
    EXPECT_EQ(scene.box("og"), nullptr) << "a closed select's list builds no box";
    EXPECT_NE(scene.box("sel"), nullptr) << "the control itself keeps its box";
}

// HTML Rendering lists UA: logical padding/margin mapping, nested disc→circle→square,
// nested margin-block zeroing, and dl/dd.
TEST(DifftestSelfCheck, UaListsLogicalAndNested) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "ua-lists.html")) << error;

    auto style_of = [&](const char* id)
        -> decltype(static_cast<zui::html::Node*>(nullptr)->computed.get()) {
        zui::html::Node* n = zui::html::find(scene.doc, id);
        EXPECT_NE(n, nullptr) << id;
        if (n == nullptr) {
            return nullptr;
        }
        EXPECT_NE(n->computed, nullptr) << id;
        return n->computed.get();
    };
    auto prop = [](const auto* m,
                   const char* key) -> std::string {
        if (m == nullptr) {
            return "<missing-map>";
        }
        const auto it = m->find(key);
        return it == m->end() ? std::string("<absent>") : std::string(it->second);
    };

    const auto* outer = style_of("outer");
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(prop(outer, "display"), "block");
    EXPECT_EQ(prop(outer, "list-style-type"), "disc");
    EXPECT_EQ(prop(outer, "padding-inline-start"), "40px");
    EXPECT_EQ(prop(outer, "padding-left"), "40px") << "LTR maps inline-start → left";
    EXPECT_EQ(prop(outer, "margin-block"), "1em");
    EXPECT_EQ(prop(outer, "margin-top"), "1em");
    EXPECT_EQ(prop(outer, "margin-bottom"), "1em");

    const auto* mid = style_of("mid");
    ASSERT_NE(mid, nullptr);
    EXPECT_EQ(prop(mid, "list-style-type"), "circle");
    EXPECT_EQ(prop(mid, "margin-block"), "0");
    EXPECT_EQ(prop(mid, "margin-top"), "0");
    EXPECT_EQ(prop(mid, "margin-bottom"), "0");
    EXPECT_EQ(prop(mid, "padding-left"), "40px");

    const auto* inner = style_of("inner");
    ASSERT_NE(inner, nullptr);
    EXPECT_EQ(prop(inner, "list-style-type"), "square");
    EXPECT_EQ(prop(inner, "margin-block"), "0");

    const auto* rtl = style_of("rtl");
    ASSERT_NE(rtl, nullptr);
    EXPECT_EQ(prop(rtl, "direction"), "rtl");
    EXPECT_EQ(prop(rtl, "padding-inline-start"), "40px");
    EXPECT_EQ(prop(rtl, "padding-right"), "40px") << "RTL maps inline-start → right";
    EXPECT_EQ(prop(rtl, "padding-left"), "<absent>");

    zui::layout::Box* rtl_box = scene.box("rtl");
    ASSERT_NE(rtl_box, nullptr);
    EXPECT_NEAR(rtl_box->geo.padding.right, 40.f, 0.01f)
        << "RTL list gutter is physical padding-right";
    EXPECT_NEAR(rtl_box->geo.padding.left, 0.f, 0.01f);

    const auto* def = style_of("def");
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(prop(def, "display"), "block");
    EXPECT_EQ(prop(def, "margin-block"), "1em");
    EXPECT_EQ(prop(def, "padding-inline-start"), "<absent>") << "dl has no list gutter";
    EXPECT_EQ(prop(def, "padding-left"), "<absent>");

    const auto* dd = style_of("dd1");
    ASSERT_NE(dd, nullptr);
    EXPECT_EQ(prop(dd, "display"), "block");
    EXPECT_EQ(prop(dd, "margin-inline-start"), "40px");
    EXPECT_EQ(prop(dd, "margin-left"), "40px");

    const auto* author = style_of("author-pad");
    ASSERT_NE(author, nullptr);
    EXPECT_EQ(prop(author, "padding-inline-start"), "24px");
    EXPECT_EQ(prop(author, "padding-left"), "24px");

    zui::layout::Box* author_box = scene.box("author-pad");
    ASSERT_NE(author_box, nullptr);
    EXPECT_NEAR(author_box->geo.padding.left, 24.f, 0.01f);
}
