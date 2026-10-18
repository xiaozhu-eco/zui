#include <gtest/gtest.h>

#include "css/engine/engine.h"
#include "css/transition/transition.h"
#include "html/document.h"

#include <cmath>
#include <iostream>
#include <string>

TEST(CssTransition, Sample) {
    zui::css::Xform pct = zui::css::parse_transform("translateY(110%)");
    if (!pct.ty_pct || std::fabs(pct.ty - 110.0f) > 0.1f) {
        FAIL() << "translateY(110%) parsed as " << pct.ty << " pct=" << pct.ty_pct << "\n";
    }
    if (std::fabs(pct.used_ty(800.0f) - 880.0f) > 0.5f) {
        FAIL() << "110% of 800=" << pct.used_ty(800.0f) << "\n";
    }

    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/transition.html", doc, error)) {
        FAIL() << error << "\n";
    }
    zui::css::Engine css;
    if (!css.load_sheets(doc, error)) {
        FAIL() << error << "\n";
    }
    zui::css::Transitions motion;
    {
        zui::css::Engine::CascadeResult cr0 = css.cascade(doc);
        motion.sync(cr0, 0);
        motion.sample(0);
    }
    if (motion.active()) {
        FAIL() << "first cascade spawned transitions\n";
    }

    zui::html::Node* btn = zui::html::find(doc, "btn");
    zui::html::Node* snap = zui::html::find(doc, "snap");
    if (btn == nullptr || snap == nullptr) {
        FAIL() << "missing nodes\n";
    }

    if (!zui::html::add_class(doc, "btn", "on") || !zui::html::add_class(doc, "snap", "on")) {
        FAIL() << "add_class failed\n";
    }
    zui::css::Engine::CascadeResult cr = css.cascade(doc);
    if (cr.paint.empty()) {
        FAIL() << "no paint diffs\n";
    }
    motion.sync(cr, 0);
    if (!motion.active()) {
        FAIL() << "expected running transitions\n";
    }
    motion.sample(0);
    if ((btn->used.flags & zui::html::VisualUsed::kBg) == 0) {
        FAIL() << "background overlay missing at t=0\n";
    }
    if (btn->used.bg_r > 8) {
        FAIL() << "t=0 background not at from: " << static_cast<int>(btn->used.bg_r) << "\n";
    }
    if (std::fabs(btn->used.opacity - 1.0f) > 0.02f) {
        FAIL() << "t=0 opacity " << btn->used.opacity << "\n";
    }

    motion.sample(0.5);
    if (btn->used.bg_r < 100 || btn->used.bg_r > 155) {
        FAIL() << "t=0.5 background " << static_cast<int>(btn->used.bg_r) << "\n";
    }
    if (std::fabs(btn->used.opacity - 0.75f) > 0.03f) {
        FAIL() << "t=0.5 opacity " << btn->used.opacity << "\n";
    }
    if ((btn->used.flags & zui::html::VisualUsed::kTransform) == 0 || std::fabs(btn->used.ty - 5.0f) > 0.3f) {
        FAIL() << "t=0.5 translateY " << btn->used.ty << "\n";
    }
    if (snap->used.flags & zui::html::VisualUsed::kBg) {
        FAIL() << "duration 0 spawned a used overlay\n";
    }

    motion.sample(1.0);
    if (motion.active()) {
        FAIL() << "records still active after duration\n";
    }
    if (btn->used.flags & zui::html::VisualUsed::kBg) {
        FAIL() << "overlay not cleared at end\n";
    }

    zui::html::remove_class(doc, "btn", "on");
    cr = css.cascade(doc);
    motion.sync(cr, 2.0);
    motion.sample(2.0);
    motion.sample(2.5);
    if (btn->used.bg_r < 100 || btn->used.bg_r > 155) {
        FAIL() << "reverse t=0.5 background " << static_cast<int>(btn->used.bg_r) << "\n";
    }
}

TEST(CssTransition, ColorFormatsHex8AndHsl) {
    zui::html::Document doc;
    std::string error;
    ASSERT_TRUE(zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/transition-color.html", doc,
                                     error))
        << error;
    zui::css::Engine css;
    ASSERT_TRUE(css.load_sheets(doc, error)) << error;
    zui::css::Transitions motion;
    {
        zui::css::Engine::CascadeResult cr0 = css.cascade(doc);
        motion.sync(cr0, 0);
        motion.sample(0);
    }
    ASSERT_TRUE(zui::html::add_class(doc, "hex8", "on"));
    ASSERT_TRUE(zui::html::add_class(doc, "hsl", "on"));
    zui::css::Engine::CascadeResult cr = css.cascade(doc);
    motion.sync(cr, 0);
    ASSERT_TRUE(motion.active());

    zui::html::Node* hex8 = zui::html::find(doc, "hex8");
    zui::html::Node* hsl = zui::html::find(doc, "hsl");
    ASSERT_NE(hex8, nullptr);
    ASSERT_NE(hsl, nullptr);

    motion.sample(0.1);  // 100ms of 200ms
    ASSERT_TRUE(hex8->used.flags & zui::html::VisualUsed::kBg);
    EXPECT_NEAR(static_cast<int>(hex8->used.bg_r), 128, 8);
    EXPECT_NEAR(static_cast<int>(hex8->used.bg_a), 128, 8)
        << "#rrggbbaa alpha must interpolate (was forced to 255)";
    ASSERT_TRUE(hsl->used.flags & zui::html::VisualUsed::kBg);
    EXPECT_NEAR(static_cast<int>(hsl->used.bg_r), 128, 20)
        << "hsl(0,0%,0%) → hsl(0,100%,50%) mid should be dark red";
    EXPECT_LT(static_cast<int>(hsl->used.bg_g), 40);
    EXPECT_LT(static_cast<int>(hsl->used.bg_b), 40);

    motion.sample(0.2);
    EXPECT_NEAR(static_cast<int>(hex8->used.bg_r), 255, 2);
    EXPECT_NEAR(static_cast<int>(hex8->used.bg_a), 255, 2);
    EXPECT_NEAR(static_cast<int>(hsl->used.bg_r), 255, 2);
    EXPECT_NEAR(static_cast<int>(hsl->used.bg_g), 0, 2);
}

// CSS Transitions: border-*-color longhands and border-color shorthand animate per side.
TEST(CssTransition, BorderColorsPerSide) {
    zui::html::Document doc;
    std::string error;
    ASSERT_TRUE(
        zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/transition-border.html", doc, error))
        << error;
    zui::css::Engine css;
    ASSERT_TRUE(css.load_sheets(doc, error)) << error;
    zui::css::Transitions motion;
    {
        zui::css::Engine::CascadeResult cr0 = css.cascade(doc);
        motion.sync(cr0, 0);
        motion.sample(0);
    }
    ASSERT_TRUE(zui::html::add_class(doc, "sides", "on"));
    ASSERT_TRUE(zui::html::add_class(doc, "shorthand", "on"));
    ASSERT_TRUE(zui::html::add_class(doc, "only-top", "on"));
    zui::css::Engine::CascadeResult cr = css.cascade(doc);
    motion.sync(cr, 0);
    ASSERT_TRUE(motion.active());

    zui::html::Node* sides = zui::html::find(doc, "sides");
    zui::html::Node* shorthand = zui::html::find(doc, "shorthand");
    zui::html::Node* only_top = zui::html::find(doc, "only-top");
    ASSERT_NE(sides, nullptr);
    ASSERT_NE(shorthand, nullptr);
    ASSERT_NE(only_top, nullptr);

    motion.sample(0.1);  // halfway through 200ms
    ASSERT_EQ(sides->used.flags & zui::html::VisualUsed::kBorder, zui::html::VisualUsed::kBorder);
    EXPECT_NEAR(static_cast<int>(sides->used.border_top_r), 128, 8);
    EXPECT_NEAR(static_cast<int>(sides->used.border_top_g), 0, 8);
    EXPECT_NEAR(static_cast<int>(sides->used.border_right_r), 0, 8);
    EXPECT_NEAR(static_cast<int>(sides->used.border_right_g), 128, 8);
    EXPECT_NEAR(static_cast<int>(sides->used.border_bottom_b), 128, 8);
    EXPECT_NEAR(static_cast<int>(sides->used.border_left_r), 128, 8);
    EXPECT_NEAR(static_cast<int>(sides->used.border_left_g), 128, 8);
    EXPECT_NEAR(static_cast<int>(sides->used.border_left_b), 128, 8);

    ASSERT_EQ(shorthand->used.flags & zui::html::VisualUsed::kBorder,
              zui::html::VisualUsed::kBorder)
        << "border-color shorthand must drive all four longhands";
    EXPECT_NEAR(static_cast<int>(shorthand->used.border_top_r), 128, 8);
    EXPECT_NEAR(static_cast<int>(shorthand->used.border_right_g), 128, 8);

    ASSERT_TRUE(only_top->used.flags & zui::html::VisualUsed::kBorderTop);
    EXPECT_FALSE(only_top->used.flags & zui::html::VisualUsed::kBorderRight)
        << "right color change without transition-property must not overlay";
    EXPECT_NEAR(static_cast<int>(only_top->used.border_top_r), 128, 8);

    motion.sample(0.2);
    EXPECT_FALSE(motion.active());
    EXPECT_EQ(sides->used.flags & zui::html::VisualUsed::kBorder, 0u);
}

// CSS Easing 1 §2.3: steps() / step-start / step-end.
TEST(CssTransition, StepsTimingFunction) {
    zui::html::Document doc;
    std::string error;
    ASSERT_TRUE(
        zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/transition-steps.html", doc, error))
        << error;
    zui::css::Engine css;
    ASSERT_TRUE(css.load_sheets(doc, error)) << error;
    zui::css::Transitions motion;
    {
        zui::css::Engine::CascadeResult cr0 = css.cascade(doc);
        motion.sync(cr0, 0);
        motion.sample(0);
    }
    ASSERT_TRUE(zui::html::add_class(doc, "end", "on"));
    ASSERT_TRUE(zui::html::add_class(doc, "start", "on"));
    ASSERT_TRUE(zui::html::add_class(doc, "step-end-kw", "on"));
    ASSERT_TRUE(zui::html::add_class(doc, "jump-none", "on"));
    zui::css::Engine::CascadeResult cr = css.cascade(doc);
    motion.sync(cr, 0);
    ASSERT_TRUE(motion.active());

    zui::html::Node* end = zui::html::find(doc, "end");
    zui::html::Node* start = zui::html::find(doc, "start");
    zui::html::Node* step_end_kw = zui::html::find(doc, "step-end-kw");
    zui::html::Node* jump_none = zui::html::find(doc, "jump-none");
    ASSERT_NE(end, nullptr);
    ASSERT_NE(start, nullptr);
    ASSERT_NE(step_end_kw, nullptr);
    ASSERT_NE(jump_none, nullptr);

    // steps(2, end): holds 0 until t=0.5, then 0.5 until t=1.
    motion.sample(0.25);
    ASSERT_TRUE(end->used.flags & zui::html::VisualUsed::kBg);
    EXPECT_NEAR(static_cast<int>(end->used.bg_r), 0, 2);
    // steps(2, start): jumps to 0.5 at t=0.
    ASSERT_TRUE(start->used.flags & zui::html::VisualUsed::kBg);
    EXPECT_NEAR(static_cast<int>(start->used.bg_r), 128, 4);
    // step-end ≡ steps(1, end): stays at 0 until the end.
    ASSERT_TRUE(step_end_kw->used.flags & zui::html::VisualUsed::kOpacity);
    EXPECT_NEAR(step_end_kw->used.opacity, 0.f, 0.02f);
    // steps(2, jump-none): one internal jump at 0.5 → output 0 then 1 (jumps = 1).
    ASSERT_TRUE(jump_none->used.flags & zui::html::VisualUsed::kOpacity);
    EXPECT_NEAR(jump_none->used.opacity, 0.f, 0.02f);

    motion.sample(0.5);
    EXPECT_NEAR(static_cast<int>(end->used.bg_r), 128, 4);
    EXPECT_NEAR(static_cast<int>(start->used.bg_r), 255, 4);
    EXPECT_NEAR(step_end_kw->used.opacity, 0.f, 0.02f);
    EXPECT_NEAR(jump_none->used.opacity, 1.f, 0.02f);

    motion.sample(0.75);
    EXPECT_NEAR(static_cast<int>(end->used.bg_r), 128, 4);
    EXPECT_NEAR(static_cast<int>(start->used.bg_r), 255, 4);
    EXPECT_NEAR(step_end_kw->used.opacity, 0.f, 0.02f);

    motion.sample(1.0);
    EXPECT_FALSE(motion.active());
}

// CSS Transitions: absolute + % lengths; auto with interpolate-size; em ok.
TEST(CssTransition, LengthProps) {
    zui::html::Document doc;
    std::string error;
    ASSERT_TRUE(
        zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/transition-length.html", doc, error))
        << error;
    zui::css::Engine css;
    ASSERT_TRUE(css.load_sheets(doc, error)) << error;
    zui::css::Transitions motion;
    motion.set_length_basis([](const zui::html::Node*) {
        zui::css::LengthBasis b;
        b.inline_px = 200.f;
        b.block_px = 200.f;
        b.has_inline = true;
        b.has_block = true;
        b.used_width = 40.f;
        b.used_height = 16.f;
        b.has_used_width = true;
        b.has_used_height = true;
        return b;
    });
    {
        zui::css::Engine::CascadeResult cr0 = css.cascade(doc);
        motion.sync(cr0, 0);
        motion.sample(0);
    }
    ASSERT_TRUE(zui::html::add_class(doc, "box", "on"));
    ASSERT_TRUE(zui::html::add_class(doc, "pad", "on"));
    ASSERT_TRUE(zui::html::add_class(doc, "mg", "on"));
    ASSERT_TRUE(zui::html::add_class(doc, "shorthand", "on"));
    ASSERT_TRUE(zui::html::add_class(doc, "pct", "on"));
    ASSERT_TRUE(zui::html::add_class(doc, "em", "on"));
    ASSERT_TRUE(zui::html::add_class(doc, "auto-h", "on"));
    zui::css::Engine::CascadeResult cr = css.cascade(doc);
    motion.sync(cr, 0);
    motion.resolve_pending();
    ASSERT_TRUE(motion.active());

    zui::html::Node* box = zui::html::find(doc, "box");
    zui::html::Node* pad = zui::html::find(doc, "pad");
    zui::html::Node* mg = zui::html::find(doc, "mg");
    zui::html::Node* shorthand = zui::html::find(doc, "shorthand");
    zui::html::Node* pct = zui::html::find(doc, "pct");
    zui::html::Node* em = zui::html::find(doc, "em");
    zui::html::Node* auto_h = zui::html::find(doc, "auto-h");
    ASSERT_NE(box, nullptr);
    ASSERT_NE(pad, nullptr);
    ASSERT_NE(mg, nullptr);
    ASSERT_NE(shorthand, nullptr);
    ASSERT_NE(pct, nullptr);
    ASSERT_NE(em, nullptr);
    ASSERT_NE(auto_h, nullptr);

    motion.sample(0.1);  // halfway through 200ms
    ASSERT_TRUE(motion.needs_layout());
    ASSERT_FALSE(motion.layout_nodes().empty());

    ASSERT_TRUE(box->used.flags & zui::html::VisualUsed::kWidth);
    ASSERT_TRUE(box->used.flags & zui::html::VisualUsed::kHeight);
    EXPECT_NEAR(box->used.width, 60.f, 0.6f);
    EXPECT_NEAR(box->used.height, 40.f, 0.6f);

    ASSERT_TRUE(pad->used.flags & zui::html::VisualUsed::kPaddingLeft);
    EXPECT_NEAR(pad->used.padding_left, 14.f, 0.6f);
    EXPECT_FALSE(pad->used.flags & zui::html::VisualUsed::kPaddingTop)
        << "unlisted padding sides must not overlay";

    ASSERT_TRUE(mg->used.flags & zui::html::VisualUsed::kMarginTop);
    EXPECT_NEAR(mg->used.margin_top, 10.f, 0.6f);

    ASSERT_EQ(shorthand->used.flags & zui::html::VisualUsed::kMargin,
              zui::html::VisualUsed::kMargin)
        << "margin shorthand must drive all four longhands";
    EXPECT_NEAR(shorthand->used.margin_top, 5.f, 0.6f);
    EXPECT_NEAR(shorthand->used.margin_right, 10.f, 0.6f);

    ASSERT_TRUE(pct->used.flags & zui::html::VisualUsed::kWidth)
        << "percent width must interpolate once the CB basis is known";
    // 40px → 50% of 200px = 100px; mid = 70
    EXPECT_NEAR(pct->used.width, 70.f, 0.6f);

    ASSERT_TRUE(em->used.flags & zui::html::VisualUsed::kWidth);
    // 2em→4em at font-size 10px → 20px→40px; mid = 30px
    EXPECT_NEAR(em->used.width, 30.f, 0.6f);

    ASSERT_TRUE(auto_h->used.flags & zui::html::VisualUsed::kHeight)
        << "height:auto with interpolate-size:allow-keywords must interpolate";
    // 0 → used_height 16; mid = 8
    EXPECT_NEAR(auto_h->used.height, 8.f, 0.6f);

    motion.sample(0.2);
    EXPECT_FALSE(motion.active());
    EXPECT_EQ(box->used.flags & zui::html::VisualUsed::kLayoutLengths, 0u);
    // Clearing overlays still dirties layout so the host can settle on specified geometry.
    EXPECT_TRUE(motion.needs_layout());
}
