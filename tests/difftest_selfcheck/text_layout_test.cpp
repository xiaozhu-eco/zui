// Text wrapping, ellipsis, decorations, shadows, shaping, and writing modes.
// L6 architecture self-checks (TESTPLAN section 12).
// Shared scene harness: selfcheck_scene.h.

#include "selfcheck_scene.h"

using namespace difftest_selfcheck;

// B2: break-spaces wraps preserved trailing spaces; pre-wrap hangs them.
TEST(DifftestSelfCheck, BreakSpacesWrapsTrailing) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "break-spaces.html")) << error;

    zui::layout::Box* pre = scene.box("prewrap");
    zui::layout::Box* brk = scene.box("break");
    ASSERT_NE(pre, nullptr);
    ASSERT_NE(brk, nullptr);
    EXPECT_EQ(pre->style().white_space, zui::layout::WhiteSpace::PreWrap);
    EXPECT_EQ(brk->style().white_space, zui::layout::WhiteSpace::BreakSpaces);
    zui::html::Node* pre_n = zui::html::find(scene.doc, "prewrap");
    zui::html::Node* brk_n = zui::html::find(scene.doc, "break");
    ASSERT_NE(pre_n, nullptr);
    ASSERT_NE(brk_n, nullptr);
    std::size_t pre_chars = pre_n->text.size();
    std::size_t brk_chars = brk_n->text.size();
    if (!pre_n->children.empty() && pre_n->children[0]) {
        pre_chars = pre_n->children[0]->text.size();
    }
    if (!brk_n->children.empty() && brk_n->children[0]) {
        brk_chars = brk_n->children[0]->text.size();
    }
    EXPECT_GT(pre_chars, std::size_t{5}) << "fixture must keep trailing spaces in the DOM";
    EXPECT_GT(brk_chars, std::size_t{5}) << "fixture must keep trailing spaces in the DOM";
    EXPECT_GT(brk->geo.height, pre->geo.height + 0.5f)
        << "break-spaces should wrap trailing spaces onto extra lines; pre_h=" << pre->geo.height
        << " brk_h=" << brk->geo.height << " pre_chars=" << pre_chars << " brk_chars=" << brk_chars
        << " pre_sw=" << pre->geo.scroll_width << " brk_sw=" << brk->geo.scroll_width;
    // Hanging pre-wrap spaces must not inflate scrollWidth past the border box.
    EXPECT_LE(pre->geo.scroll_width, pre->geo.width + 0.5f)
        << "pre-wrap hang must not grow scrollWidth; sw=" << pre->geo.scroll_width
        << " w=" << pre->geo.width;
}

// B2: text-overflow:ellipsis emits U+2026 after a truncated run.
TEST(DifftestSelfCheck, TextOverflowEllipsis) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "text-overflow.html")) << error;

    EXPECT_EQ(scene.box("ellip")->style().text_overflow, zui::layout::TextOverflow::Ellipsis);
    EXPECT_EQ(scene.box("clip")->style().text_overflow, zui::layout::TextOverflow::Clip);

    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.set_retained(false);
    painter.paint(*scene.layout->root(), list, 800, 600);

    bool saw_ellipsis = false;
    for (const auto& it : list.items()) {
        if (it.op != zui::paint::DisplayOp::DrawText) {
            continue;
        }
        if (it.text.find("\xE2\x80\xA6") != std::string::npos) {
            saw_ellipsis = true;
            break;
        }
    }
    EXPECT_TRUE(saw_ellipsis) << "ellipsis box should paint U+2026";
}

// B2: word-spacing widens/narrows max-content bars by the space count.
TEST(DifftestSelfCheck, WordSpacingAdvances) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "word-spacing.html")) << error;

    zui::layout::Box* normal = scene.box("normal");
    zui::layout::Box* wide = scene.box("wide");
    zui::layout::Box* tight = scene.box("tight");
    ASSERT_NE(normal, nullptr);
    ASSERT_NE(wide, nullptr);
    ASSERT_NE(tight, nullptr);
    EXPECT_NEAR(wide->style().word_spacing, 12.f, 0.01f);
    EXPECT_NEAR(tight->style().word_spacing, -4.f, 0.01f);
    EXPECT_NEAR(normal->style().word_spacing, 0.f, 0.01f);
    // "AA BB" has one inter-word space → wide is +12px, tight is -4px vs normal.
    EXPECT_NEAR(wide->geo.width, normal->geo.width + 12.f, 0.75f);
    EXPECT_NEAR(tight->geo.width, normal->geo.width - 4.f, 0.75f);
}

// B3: text-decoration paints underline/overline/line-through with style/thickness/color.
TEST(DifftestSelfCheck, TextDecorationLines) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "text-decoration.html")) << error;

    auto style_of = [&](const char* id)
        -> decltype(static_cast<zui::html::Node*>(nullptr)->computed.get()) {
        zui::html::Node* n = zui::html::find(scene.doc, id);
        EXPECT_NE(n, nullptr) << id;
        return n == nullptr ? nullptr : n->computed.get();
    };
    auto prop = [](const auto* m,
                   const char* key) -> std::string {
        if (m == nullptr) {
            return "<missing-map>";
        }
        const auto it = m->find(key);
        return it == m->end() ? std::string("<absent>") : std::string(it->second);
    };

    // Prefer longhands when lexbor expands; otherwise the shorthand string.
    auto decor_blob = [&](const char* id) {
        const auto* m = style_of(id);
        std::string s = prop(m, "text-decoration");
        s += "|" + prop(m, "text-decoration-line");
        s += "|" + prop(m, "text-decoration-style");
        s += "|" + prop(m, "text-decoration-color");
        s += "|" + prop(m, "text-decoration-thickness");
        return s;
    };
    EXPECT_NE(decor_blob("u").find("underline"), std::string::npos) << decor_blob("u");
    EXPECT_NE(decor_blob("wavy").find("wavy"), std::string::npos) << decor_blob("wavy");
    EXPECT_NE(decor_blob("thick").find("3px"), std::string::npos) << decor_blob("thick");

    // Decoration must not change the content box vs the undecorated control.
    zui::layout::Box* plain = scene.box("plain");
    zui::layout::Box* u = scene.box("u");
    ASSERT_NE(plain, nullptr);
    ASSERT_NE(u, nullptr);
    EXPECT_NEAR(u->geo.height, plain->geo.height, 0.5f);

    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.set_retained(false);
    painter.paint(*scene.layout->root(), list, 800, 600);

    int blue_rects = 0;
    int red_rects = 0;
    int light_rects = 0;
    int dashed_pieces = 0;
    for (const auto& it : list.items()) {
        if (it.op != zui::paint::DisplayOp::FillRect) {
            continue;
        }
        // Skip large background fills.
        if (it.rect.w > 200.f || it.rect.h > 8.f) {
            continue;
        }
        if (it.color.r > 100 && it.color.g < 100 && it.color.b < 100) {
            ++red_rects;
        }
        if (it.color.b > 200 && it.color.r < 150) {
            ++blue_rects;
            if (it.rect.w < 20.f) {
                ++dashed_pieces;
            }
        }
        if (it.color.r > 200 && it.color.g > 200 && it.color.b > 200 && it.rect.h <= 3.f) {
            ++light_rects;
        }
    }
    EXPECT_GE(light_rects, 2) << "default-color underline/overline/line-through strokes";
    EXPECT_GE(red_rects, 3) << "wavy underline should emit several red segments";
    EXPECT_GE(blue_rects, 1) << "thick/dashed blue underlines";
    EXPECT_GE(dashed_pieces, 2) << "dashed style should emit multiple short segments";
}

// B3: text-shadow emits offset/blurred DrawText layers under the fill (multi-layer OK).
TEST(DifftestSelfCheck, TextShadowLayers) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "text-shadow.html")) << error;

    auto style_of = [&](const char* id)
        -> decltype(static_cast<zui::html::Node*>(nullptr)->computed.get()) {
        zui::html::Node* n = zui::html::find(scene.doc, id);
        EXPECT_NE(n, nullptr) << id;
        return n == nullptr ? nullptr : n->computed.get();
    };
    auto prop = [](const auto* m,
                   const char* key) -> std::string {
        if (m == nullptr) {
            return "<missing-map>";
        }
        const auto it = m->find(key);
        return it == m->end() ? std::string("<absent>") : std::string(it->second);
    };

    const std::string multi = prop(style_of("multi"), "text-shadow");
    EXPECT_NE(multi.find(','), std::string::npos) << multi;
    EXPECT_NE(multi.find("e85d4c"), std::string::npos) << multi;

    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.set_retained(false);
    painter.paint(*scene.layout->root(), list, 800, 600);

    int hard_shadows = 0;
    int soft_shadows = 0;
    int fill_texts = 0;
    for (const auto& it : list.items()) {
        if (it.op != zui::paint::DisplayOp::DrawText) {
            continue;
        }
        if (it.blur > 0.5f) {
            ++soft_shadows;
            continue;
        }
        // Shadow colour is not the near-black fill.
        if (it.color.r > 40 || it.color.g > 40 || it.color.b > 80) {
            ++hard_shadows;
        } else {
            ++fill_texts;
        }
    }
    EXPECT_GE(hard_shadows, 2) << "hard + first multi layer";
    EXPECT_GE(soft_shadows, 2) << "soft + second multi layer";
    EXPECT_GE(fill_texts, 3) << "each paragraph paints its fill text";

    // Multi: last listed layer paints first; first listed (red hard) ends above the soft layer.
    // Find a red hard DrawText that is followed later by near-black fill for "Multi".
    bool saw_red_before_fill = false;
    bool saw_red = false;
    for (const auto& it : list.items()) {
        if (it.op != zui::paint::DisplayOp::DrawText) {
            continue;
        }
        if (it.text.find("Multi") == std::string::npos && it.text.find("M") == std::string::npos) {
            // Runs may be whole string "Multi".
        }
        const bool is_red = it.color.r > 180 && it.color.g < 120 && it.blur < 0.5f;
        const bool is_fill =
            it.color.r < 40 && it.color.g < 40 && it.color.b < 60 && it.blur < 0.5f;
        if (is_red) {
            saw_red = true;
        }
        if (saw_red && is_fill && it.text.find("Multi") != std::string::npos) {
            saw_red_before_fill = true;
            break;
        }
    }
    EXPECT_TRUE(saw_red_before_fill) << "multi-layer: coloured hard shadow before fill text";
}

// B2: HarfBuzz shapes Latin spans. `liga` is on, but AV is not a ligature, so the span stays
// 1:1. Pair kerning comes from GPOS via `FontChain::kerning`, the same value paint and
// `FreeTypeTextMetrics::measure` use. HarfBuzz's own advance is not used: hb-ft answers in
// 1/64px, so its kern delta is a rounded difference that drifts the run width.
TEST(DifftestSelfCheck, HarfBuzzShapesKerning) {
#if !defined(ZUI_WITH_HARFBUZZ)
    GTEST_SKIP() << "ZUI_WITH_HARFBUZZ is off";
#else
    const std::string font_path =
        std::string(ZUI_SOURCE_DIR) + "/tests/difftest/fonts/NotoSans-Regular.ttf";
    zui::text::register_embedded_font("Zui Test", font_path);
    zui::text::FontChain chain;
    ASSERT_TRUE(chain.init("Zui Test", 400, false));
    const zui::text::LoadedGlyph glyph_a = chain.load(static_cast<std::uint32_t>('A'), 16.f, false);
    const zui::text::LoadedGlyph glyph_v = chain.load(static_cast<std::uint32_t>('V'), 16.f, false);
    ASSERT_TRUE(glyph_a.ok && glyph_v.ok);
    const float gpos = chain.kerning(glyph_a.face_key, glyph_a.glyph_index, glyph_v.glyph_index);
    ASSERT_LT(gpos, -0.01f) << "Noto Sans Regular must expose a tightening AV pair";

    const std::uint32_t cps[2] = {static_cast<std::uint32_t>('A'), static_cast<std::uint32_t>('V')};
    float shaped[2] = {};
    std::uint32_t gids[2] = {};
    ASSERT_TRUE(chain.shape_advances(glyph_a.face_key, 16.f, cps, 2, shaped, gids));
    EXPECT_EQ(gids[0], glyph_a.glyph_index);
    EXPECT_EQ(gids[1], glyph_v.glyph_index);
    // The shaped advance is the FreeType base advance; the pair kern is the caller's to add.
    EXPECT_NEAR(shaped[0], glyph_a.advance, 0.015625f);
    EXPECT_NEAR(shaped[1], glyph_v.advance, 0.015625f);
    EXPECT_NEAR(shaped[0] + shaped[1] + gpos, glyph_a.advance + glyph_v.advance + gpos, 0.015625f);
#endif
}

// A6/B2: vertical / sideways writing-mode records Latin glyphs as Sideways* DrawText.
TEST(DifftestSelfCheck, WritingModeSidewaysGlyphs) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "writing-mode-sideways.html")) << error;

    EXPECT_EQ(scene.box("vrl")->style().writing_mode, zui::layout::WritingMode::VerticalRl);
    EXPECT_EQ(scene.box("srl")->style().writing_mode, zui::layout::WritingMode::SidewaysRl);
    EXPECT_EQ(scene.box("vlr")->style().writing_mode, zui::layout::WritingMode::VerticalLr);

    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.set_retained(false);
    painter.paint(*scene.layout->root(), list, 800, 600);

    int sideways_rl = 0;
    int sideways_lr = 0;
    for (const auto& it : list.items()) {
        if (it.op != zui::paint::DisplayOp::DrawText) {
            continue;
        }
        if (it.text_orient == zui::paint::TextGlyphOrient::SidewaysRl) {
            ++sideways_rl;
        } else if (it.text_orient == zui::paint::TextGlyphOrient::SidewaysLr) {
            ++sideways_lr;
        }
    }
    EXPECT_GE(sideways_rl, 2) << "vertical-rl and sideways-rl should emit SidewaysRl DrawText";
    EXPECT_GE(sideways_lr, 1) << "vertical-lr should emit SidewaysLr DrawText";
}

// B2: vertical-rl overflow maps physical scrollLeft onto the block axis.
TEST(DifftestSelfCheck, WritingModeVerticalScroll) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "writing-mode-vertical-scroll.html")) << error;

    zui::layout::Box* scroller = scene.box("scroller");
    zui::layout::Box* start = scene.box("start");
    ASSERT_NE(scroller, nullptr);
    ASSERT_NE(start, nullptr);
    EXPECT_EQ(scroller->style().writing_mode, zui::layout::WritingMode::VerticalRl);
    EXPECT_GT(scroller->geo.scroll_width, scroller->geo.width + 1.f)
        << "block-axis content must overflow physically in width";

    const float start_x0 = start->geo.world_x;
    const float port_right = scroller->geo.world_x + scroller->geo.width;
    EXPECT_LT(start_x0 + 1.f, port_right)
        << "at scrollLeft 0 the block-start mark should sit inside the port";
    EXPECT_TRUE(zui::input::set_scroll(*scroller, 60.f, 0.f));
    scene.layout->sync_world();
    start = scene.box("start");
    ASSERT_NE(start, nullptr);
    // vertical-rl: increasing scrollLeft advances toward block-end (left), so content shifts
    // right in physical coords and the block-start mark leaves past the port's right edge.
    EXPECT_GT(start->geo.world_x, start_x0 + 40.f)
        << "scrollLeft under vertical-rl should shift block-axis content rightward; before="
        << start_x0 << " after=" << start->geo.world_x;
    EXPECT_GT(start->geo.world_x, port_right - 1.f)
        << "block-start mark should leave the scrollport after scrollLeft=60";
}
