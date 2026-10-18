#include <gtest/gtest.h>

#include "css/engine/engine.h"
#include "css/values.h"
#include "html/document.h"
#include "layout/cache/box_cache.h"
#include "layout/cache/layout_result.h"
#include "layout/engine.h"
#include "paint/display_list.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

TEST(PaintRound, ClipAndStroke) {
    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/round_clip.html", doc, error)) {
        FAIL() << error << "\n";
    }
    zui::css::Engine css;
    if (!css.load_sheets(doc, error)) {
        FAIL() << error << "\n";
    }
    css.cascade(doc);
    zui::layout::FreeTypeTextMetrics metrics;
    zui::layout::LayoutEngine layout(metrics);
    layout.layout(doc, zui::layout::Viewport{400.0f, 300.0f});

    zui::html::Node* box_n = zui::html::find(doc, "box");
    zui::html::Node* kid_n = zui::html::find(doc, "kid");
    zui::layout::Box* box = layout.principal_box(box_n);
    zui::layout::Box* kid = layout.principal_box(kid_n);
    if (box == nullptr || kid == nullptr) {
        FAIL() << "missing boxes\n";
    }

    zui::layout::Box* corner = layout.hit_test(box->geo.world_x + 2.0f, box->geo.world_y + 2.0f);
    if (corner == kid || corner == box) {
        FAIL() << "rounded corner still hits\n";
    }
    zui::layout::Box* mid = layout.hit_test(box->geo.world_x + 50.0f, box->geo.world_y + 50.0f);
    if (mid != box && mid != kid) {
        FAIL() << "center missed the rounded box\n";
    }

    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.paint(*layout.root(), list);
    bool round_clip = false;
    bool round_stroke = false;
    for (const auto& it : list.items()) {
        if (it.op == zui::paint::DisplayOp::PushClip && it.radius > 20.0f) {
            round_clip = true;
        }
        if (it.op == zui::paint::DisplayOp::StrokeRect && it.radius > 20.0f &&
            it.stroke_width > 2.0f) {
            round_stroke = true;
            if (it.color.r != 0x7c || it.color.g != 0xb7 || it.color.b != 0xff) {
                FAIL() << "stroke color " << static_cast<int>(it.color.r) << ","
                       << static_cast<int>(it.color.g) << "," << static_cast<int>(it.color.b)
                       << "\n";
            }
        }
    }
    if (!round_clip) {
        FAIL() << "overflow clip has no radius\n";
    }
    if (!round_stroke) {
        FAIL() << "border stroke has no radius\n";
    }

    // `#kid` is position:absolute under a position:relative overflow box that is not itself a
    // stacking context, so the root paints it after `#box` has popped its own clip. The clip
    // active on that fill has to keep the border radius; a square scrollport leaves the corner.
    float kid_clip_radius = -1.f;
    std::vector<float> clip_stack;
    for (const auto& it : list.items()) {
        if (it.op == zui::paint::DisplayOp::PushClip) {
            clip_stack.push_back(it.radius);
        } else if (it.op == zui::paint::DisplayOp::PopClip) {
            if (!clip_stack.empty()) {
                clip_stack.pop_back();
            }
        } else if (it.op == zui::paint::DisplayOp::FillRect && it.color.r == 0xcc &&
                   it.color.g == 0x33 && it.color.b == 0x44) {
            kid_clip_radius = 0.f;
            for (float radius : clip_stack) {
                kid_clip_radius = std::max(kid_clip_radius, radius);
            }
        }
    }
    if (kid_clip_radius < 20.f) {
        FAIL() << "lifted overflow child is not under a rounded clip (" << kid_clip_radius
               << ")\n";
    }
}

namespace {

std::string stops_dump(const zui::paint::Gradient& g) {
    std::string out = "line=(" + std::to_string(g.x) + "," + std::to_string(g.y) + ")+(" +
                      std::to_string(g.dx) + "," + std::to_string(g.dy) + ")*" +
                      std::to_string(g.len) + " stops=";
    for (const zui::paint::GradientStop& s : g.stops) {
        out += std::to_string(s.offset) + ":rgb(" + std::to_string(s.color.r) + "," +
               std::to_string(s.color.g) + "," + std::to_string(s.color.b) + "/" +
               std::to_string(s.color.a) + ") ";
    }
    return out;
}

}  // namespace

// The line a gradient resolves to decides every pixel of it, so the geometry is pinned here
// rather than only through the differential raster. Numbers are the CSS Images 3 section 3.4.2
// construction: the line runs through the centre, its length is |w sin| + |h cos|, and a corner
// keyword angles it at atan2 of the box sides.
TEST(PaintRound, ChecksLiftedCornerClip) {
    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/example/tests/checks/checks.html", doc, error)) {
        FAIL() << error << "\n";
    }
    zui::css::Engine css;
    if (!css.load_sheets(doc, error)) {
        FAIL() << error << "\n";
    }
    css.cascade(doc);
    zui::layout::FreeTypeTextMetrics metrics;
    zui::layout::LayoutEngine layout(metrics);
    // The swatch sits below the 600px fold. A taller viewport keeps the fill in the list
    // without scrolling, which is enough to see which clip is active on it.
    layout.layout(doc, zui::layout::Viewport{800.0f, 1200.0f});
    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.paint(*layout.root(), list);
    std::vector<float> clip_stack;
    int rounded_red = 0;
    int square_red = 0;
    for (const auto& it : list.items()) {
        if (it.op == zui::paint::DisplayOp::PushClip) {
            clip_stack.push_back(it.radius);
        } else if (it.op == zui::paint::DisplayOp::PopClip) {
            if (!clip_stack.empty()) {
                clip_stack.pop_back();
            }
        } else if (it.op == zui::paint::DisplayOp::FillRect && it.color.r == 0xe8 &&
                   it.color.g == 0x5d && it.color.b == 0x4c && it.rect.x < 450.f) {
            float rad = 0.f;
            for (float radius : clip_stack) {
                rad = std::max(rad, radius);
            }
            if (rad > 20.f) {
                ++rounded_red;
            } else {
                ++square_red;
            }
        }
    }
    EXPECT_GE(rounded_red, 1) << "clipped swatch red fills " << rounded_red
                              << " square " << square_red;
}

TEST(PaintRound, LinearGradientParsesAngleAndStops) {
    const zui::paint::Rect box{0, 0, 120, 90};
    zui::paint::Gradient g;

    ASSERT_TRUE(
        zui::paint::linear_gradient_for_box("linear-gradient(165deg, #2f6d50, #173728)", box, g))
        << "angle form rejected";
    ASSERT_EQ(g.stops.size(), std::size_t{2}) << stops_dump(g);
    EXPECT_NEAR(g.stops[0].offset, 0.0f, 0.001f) << stops_dump(g);
    EXPECT_NEAR(g.stops[1].offset, 1.0f, 0.001f) << stops_dump(g);
    EXPECT_EQ(static_cast<int>(g.stops[0].color.r), 0x2f) << stops_dump(g);
    EXPECT_EQ(static_cast<int>(g.stops[1].color.a), 0xff) << stops_dump(g);
    const float len_165 = std::fabs(120.0f * std::sin(165.0f * 3.14159265f / 180.0f)) +
                          std::fabs(90.0f * std::cos(165.0f * 3.14159265f / 180.0f));
    EXPECT_NEAR(g.len, len_165, 0.01f) << stops_dump(g);

    // No direction at all defaults to `to bottom`, and a stated middle position stays where it is.
    ASSERT_TRUE(zui::paint::linear_gradient_for_box(
        "linear-gradient(#ff0000, #00ff00 50%, #0000ff)", box, g))
        << "default-direction form rejected";
    ASSERT_EQ(g.stops.size(), std::size_t{3}) << stops_dump(g);
    EXPECT_NEAR(g.dx, 0.0f, 0.001f) << stops_dump(g);
    EXPECT_NEAR(g.dy, 1.0f, 0.001f) << stops_dump(g);
    EXPECT_NEAR(g.len, 90.0f, 0.01f) << stops_dump(g);
    EXPECT_NEAR(g.stops[0].offset, 0.0f, 0.001f) << stops_dump(g);
    EXPECT_NEAR(g.stops[1].offset, 0.5f, 0.001f) << stops_dump(g);
    EXPECT_NEAR(g.stops[2].offset, 1.0f, 0.001f) << stops_dump(g);
    EXPECT_EQ(static_cast<int>(g.stops[2].color.b), 0xff) << stops_dump(g);

    // The sampler runs along the line, hits the pure stop colours at both ends, and
    // interpolates in premultiplied space in between.
    const zui::paint::Color top = zui::paint::gradient_color_at(g, 60.0f, 0.0f);
    const zui::paint::Color mid = zui::paint::gradient_color_at(g, 60.0f, 45.0f);
    const zui::paint::Color bottom = zui::paint::gradient_color_at(g, 60.0f, 90.0f);
    EXPECT_EQ(static_cast<int>(top.r), 0xff) << stops_dump(g);
    EXPECT_NEAR(static_cast<int>(mid.g), 0xff, 2) << stops_dump(g);
    EXPECT_EQ(static_cast<int>(bottom.b), 0xff) << stops_dump(g);
}

TEST(PaintRound, LinearGradientTwoPositionStops) {
    const zui::paint::Rect box{0, 0, 200, 100};
    zui::paint::Gradient g;
    // CSS Images 3 §3.4.1: two positions on one color are two stops. A shared offset is a hard edge.
    ASSERT_TRUE(zui::paint::linear_gradient_for_box(
        "linear-gradient(to bottom, #e23b3b 0% 50%, #2f6fed 50% 100%)", box, g));
    ASSERT_EQ(g.stops.size(), std::size_t{4}) << stops_dump(g);
    EXPECT_NEAR(g.stops[0].offset, 0.0f, 0.001f) << stops_dump(g);
    EXPECT_NEAR(g.stops[1].offset, 0.5f, 0.001f) << stops_dump(g);
    EXPECT_NEAR(g.stops[2].offset, 0.5f, 0.001f) << stops_dump(g);
    EXPECT_NEAR(g.stops[3].offset, 1.0f, 0.001f) << stops_dump(g);
    EXPECT_EQ(static_cast<int>(g.stops[0].color.r), 0xe2) << stops_dump(g);
    EXPECT_EQ(static_cast<int>(g.stops[1].color.r), 0xe2) << stops_dump(g);
    EXPECT_EQ(static_cast<int>(g.stops[2].color.b), 0xed) << stops_dump(g);
    EXPECT_EQ(static_cast<int>(g.stops[3].color.b), 0xed) << stops_dump(g);
    const zui::paint::Color above = zui::paint::gradient_color_at(g, 100.0f, 49.0f);
    const zui::paint::Color below = zui::paint::gradient_color_at(g, 100.0f, 51.0f);
    EXPECT_EQ(static_cast<int>(above.r), 0xe2);
    EXPECT_EQ(static_cast<int>(above.b), 0x3b);
    EXPECT_EQ(static_cast<int>(below.r), 0x2f);
    EXPECT_EQ(static_cast<int>(below.b), 0xed);

    // Flat run, then a blend, then a flat run. The second position is not dropped.
    ASSERT_TRUE(zui::paint::linear_gradient_for_box(
        "linear-gradient(to bottom, #e23b3b 0% 25%, #2f6fed 75% 100%)", box, g));
    ASSERT_EQ(g.stops.size(), std::size_t{4}) << stops_dump(g);
    EXPECT_NEAR(g.stops[1].offset, 0.25f, 0.001f) << stops_dump(g);
    EXPECT_NEAR(g.stops[2].offset, 0.75f, 0.001f) << stops_dump(g);
    const zui::paint::Color flat = zui::paint::gradient_color_at(g, 100.0f, 20.0f);
    const zui::paint::Color solid = zui::paint::gradient_color_at(g, 100.0f, 90.0f);
    EXPECT_EQ(static_cast<int>(flat.r), 0xe2);
    EXPECT_EQ(static_cast<int>(flat.b), 0x3b);
    EXPECT_EQ(static_cast<int>(solid.r), 0x2f);
    EXPECT_EQ(static_cast<int>(solid.b), 0xed);

    const zui::paint::Rect wide{0, 0, 160, 40};
    ASSERT_TRUE(zui::paint::repeating_linear_gradient_for_box(
        "repeating-linear-gradient(to right, #e23b3b 0 16px, #2f6fed 16px 32px)", wide, g));
    ASSERT_EQ(g.stops.size(), std::size_t{4}) << stops_dump(g);
    EXPECT_TRUE(g.repeating);
    EXPECT_NEAR(g.stops[1].offset, 0.1f, 0.001f) << stops_dump(g);
    EXPECT_NEAR(g.stops[2].offset, 0.1f, 0.001f) << stops_dump(g);
    EXPECT_NEAR(g.stops[3].offset, 0.2f, 0.001f) << stops_dump(g);

    // A lone percentage is a hint. Blink expands it to nine stops; the stop on the hint
    // is the premultiplied midpoint, not 20% of the way from the first colour.
    ASSERT_TRUE(zui::paint::linear_gradient_for_box(
        "linear-gradient(to bottom, #e23a3a, 20%, #2e6eee)", box, g));
    ASSERT_EQ(g.stops.size(), std::size_t{11}) << stops_dump(g);
    EXPECT_NEAR(g.stops[0].offset, 0.0f, 0.001f) << stops_dump(g);
    EXPECT_NEAR(g.stops[1].offset, 0.2f / 3.0f, 0.001f) << stops_dump(g);
    EXPECT_NEAR(g.stops[3].offset, 0.2f, 0.001f) << stops_dump(g);
    EXPECT_NEAR(g.stops[10].offset, 1.0f, 0.001f) << stops_dump(g);
    EXPECT_EQ(static_cast<int>(g.stops[3].color.r), 136) << stops_dump(g);
    EXPECT_EQ(static_cast<int>(g.stops[3].color.g), 84) << stops_dump(g);
    EXPECT_EQ(static_cast<int>(g.stops[3].color.b), 148) << stops_dump(g);
    const zui::paint::Color at_hint = zui::paint::gradient_color_at(g, 100.0f, 20.0f);
    EXPECT_EQ(static_cast<int>(at_hint.r), 136);
    EXPECT_EQ(static_cast<int>(at_hint.g), 84);
    EXPECT_EQ(static_cast<int>(at_hint.b), 148);

    // CSS Images 4: `in oklch` samples that space. The stop at 50% is color-mix, not the
    // premultiplied sRGB midpoint (136, 84, 148 for these two colours).
    ASSERT_TRUE(zui::paint::linear_gradient_for_box(
        "linear-gradient(to right in oklch, #e23a3a, #2e6eee)", box, g));
    ASSERT_GT(g.stops.size(), std::size_t{2}) << stops_dump(g);
    const std::optional<zui::css::CssRgba> oklch_mid = zui::css::parse_css_color(
        "color-mix(in oklch, #2e6eee 50%, #e23a3a)");
    ASSERT_TRUE(oklch_mid.has_value());
    const zui::paint::GradientStop* at_half = nullptr;
    for (const zui::paint::GradientStop& stop : g.stops) {
        if (std::fabs(stop.offset - 0.5f) < 0.0001f) {
            at_half = &stop;
            break;
        }
    }
    ASSERT_NE(at_half, nullptr) << stops_dump(g);
    EXPECT_EQ(static_cast<int>(at_half->color.r), static_cast<int>(oklch_mid->r)) << stops_dump(g);
    EXPECT_EQ(static_cast<int>(at_half->color.g), static_cast<int>(oklch_mid->g)) << stops_dump(g);
    EXPECT_EQ(static_cast<int>(at_half->color.b), static_cast<int>(oklch_mid->b)) << stops_dump(g);
    EXPECT_NE(static_cast<int>(at_half->color.r), 136);

    const std::optional<zui::css::CssRgba> longer_mid = zui::css::parse_css_color(
        "color-mix(in hsl longer hue, #2e6eee 50%, #e23a3a)");
    ASSERT_TRUE(longer_mid.has_value());
    ASSERT_TRUE(zui::paint::linear_gradient_for_box(
        "linear-gradient(in hsl longer hue, #e23a3a, #2e6eee)", box, g));
    at_half = nullptr;
    for (const zui::paint::GradientStop& stop : g.stops) {
        if (std::fabs(stop.offset - 0.5f) < 0.0001f) {
            at_half = &stop;
            break;
        }
    }
    ASSERT_NE(at_half, nullptr) << stops_dump(g);
    EXPECT_EQ(static_cast<int>(at_half->color.r), static_cast<int>(longer_mid->r)) << stops_dump(g);
    EXPECT_EQ(static_cast<int>(at_half->color.g), static_cast<int>(longer_mid->g)) << stops_dump(g);
    EXPECT_EQ(static_cast<int>(at_half->color.b), static_cast<int>(longer_mid->b)) << stops_dump(g);

    ASSERT_TRUE(zui::paint::conic_gradient_for_box(
        "conic-gradient(in hsl longer hue, #e23a3a, #2e6eee)", box, g));
    EXPECT_EQ(g.kind, zui::paint::GradientKind::Conic);
    ASSERT_GT(g.stops.size(), std::size_t{2});

    // A conic hint uses the same nine-stop premultiplied expansion as a linear hint.
    ASSERT_TRUE(zui::paint::conic_gradient_for_box(
        "conic-gradient(#e23a3a, 20%, #2e6eee)", box, g));
    ASSERT_EQ(g.stops.size(), std::size_t{11}) << stops_dump(g);
    EXPECT_NEAR(g.stops[3].offset, 0.2f, 0.001f) << stops_dump(g);
    EXPECT_EQ(static_cast<int>(g.stops[3].color.r), 136) << stops_dump(g);
    EXPECT_EQ(static_cast<int>(g.stops[3].color.g), 84) << stops_dump(g);
    EXPECT_EQ(static_cast<int>(g.stops[3].color.b), 148) << stops_dump(g);

    const zui::paint::Rect square{0, 0, 120, 120};
    ASSERT_TRUE(zui::paint::radial_gradient_for_box(
        "radial-gradient(circle 50px in oklch, #e23a3a, #2e6eee)", square, g));
    EXPECT_NEAR(g.rx, 50.0f, 0.01f);
    EXPECT_EQ(g.kind, zui::paint::GradientKind::Radial);
    ASSERT_GT(g.stops.size(), std::size_t{2});

    ASSERT_TRUE(zui::paint::radial_gradient_for_box(
        "radial-gradient(circle 50px, #e23b3b 0% 50%, #2f6fed 50% 100%)", square, g));
    EXPECT_NEAR(g.rx, 50.0f, 0.01f) << stops_dump(g);
    EXPECT_NEAR(g.ry, 50.0f, 0.01f) << stops_dump(g);
    ASSERT_EQ(g.stops.size(), std::size_t{4}) << stops_dump(g);
    EXPECT_NEAR(g.stops[1].offset, 0.5f, 0.001f) << stops_dump(g);
    EXPECT_EQ(static_cast<int>(g.stops[0].color.r), 0xe2) << stops_dump(g);
    const zui::paint::Color centre = zui::paint::gradient_color_at(g, 60.0f, 60.0f);
    const zui::paint::Color ring = zui::paint::gradient_color_at(g, 60.0f, 100.0f);
    EXPECT_EQ(static_cast<int>(centre.r), 0xe2);
    EXPECT_EQ(static_cast<int>(ring.b), 0xed);
    EXPECT_EQ(static_cast<int>(ring.r), 0x2f);
}

TEST(PaintRound, LinearGradientParsesKeywordDirections) {
    const zui::paint::Rect box{0, 0, 120, 90};
    zui::paint::Gradient g;

    ASSERT_TRUE(
        zui::paint::linear_gradient_for_box("linear-gradient(to right, #000000, #ffffff)", box, g))
        << "side keyword rejected";
    EXPECT_NEAR(g.dx, 1.0f, 0.001f) << stops_dump(g);
    EXPECT_NEAR(g.dy, 0.0f, 0.001f) << stops_dump(g);
    EXPECT_NEAR(g.len, 120.0f, 0.01f) << stops_dump(g);

    // A corner keyword depends on the box shape: the line is perpendicular to the line joining the
    // two neighbouring corners, so for 120x90 it runs at (0.6, 0.8) and measures 72 + 72.
    ASSERT_TRUE(zui::paint::linear_gradient_for_box(
        "linear-gradient(to bottom right, #e0a050, #8c4030, #2c1a10)", box, g))
        << "corner keyword rejected";
    EXPECT_NEAR(g.len, 144.0f, 0.01f) << stops_dump(g);
    EXPECT_NEAR(g.dx, 0.6f, 0.001f) << stops_dump(g);
    EXPECT_NEAR(g.dy, 0.8f, 0.001f) << stops_dump(g);
    EXPECT_NEAR(g.stops[1].offset, 0.5f, 0.001f) << stops_dump(g);
}

TEST(PaintRound, LinearGradientRejectsOtherImages) {
    const zui::paint::Rect box{0, 0, 120, 90};
    zui::paint::Gradient g;
    EXPECT_FALSE(zui::paint::linear_gradient_for_box("none", box, g));
    EXPECT_FALSE(zui::paint::linear_gradient_for_box("url(cover.png)", box, g));
    EXPECT_FALSE(
        zui::paint::linear_gradient_for_box("radial-gradient(circle, #000000, #ffffff)", box, g));
    // One stop is not a gradient, so the caller falls back to the background colour.
    EXPECT_FALSE(zui::paint::linear_gradient_for_box("linear-gradient(#ffffff)", box, g));
    // A comma-joined multi-layer string still "parses" here by inventing false stops — painter
    // must refuse top-level commas before calling this (B2 list-value guard / `.mat` fix).
    EXPECT_TRUE(zui::paint::linear_gradient_for_box(
        "linear-gradient(45deg, #e4ddd0 25%, transparent 25%), "
        "linear-gradient(-45deg, #e4ddd0 25%, transparent 25%)",
        box, g));
}

TEST(PaintRound, RepeatingLinearAndRadialGradients) {
    const zui::paint::Rect box{0, 0, 100, 80};
    zui::paint::Gradient g;
    ASSERT_TRUE(zui::paint::repeating_linear_gradient_for_box(
        "repeating-linear-gradient(90deg, #ff0000 0, #0000ff 20px)", box, g));
    EXPECT_TRUE(g.repeating);
    EXPECT_EQ(g.kind, zui::paint::GradientKind::Linear);
    EXPECT_GE(g.stops.size(), 2u);

    ASSERT_TRUE(zui::paint::radial_gradient_for_box("radial-gradient(#ff0000, #0000ff)", box, g));
    EXPECT_EQ(g.kind, zui::paint::GradientKind::Radial);
    EXPECT_FALSE(g.repeating);
    EXPECT_NEAR(g.x, 50.f, 0.01f);
    EXPECT_NEAR(g.y, 40.f, 0.01f);
    // farthest-corner ellipse at centre: rx=w/√2, ry=h/√2
    EXPECT_NEAR(g.rx, 50.f * std::sqrt(2.f), 0.05f);
    EXPECT_NEAR(g.ry, 40.f * std::sqrt(2.f), 0.05f);

    ASSERT_TRUE(zui::paint::radial_gradient_for_box(
        "radial-gradient(circle closest-side at 20px 20px, #ffffff, #000000)", box, g));
    EXPECT_EQ(g.kind, zui::paint::GradientKind::Radial);
    EXPECT_NEAR(g.x, 20.f, 0.01f);
    EXPECT_NEAR(g.y, 20.f, 0.01f);
    EXPECT_NEAR(g.rx, 20.f, 0.01f);
    EXPECT_NEAR(g.ry, 20.f, 0.01f);

    // Centre sample of a default ellipse is the first stop; a far corner is past 1 → last stop.
    ASSERT_TRUE(zui::paint::radial_gradient_for_box("radial-gradient(#ff0000, #0000ff)", box, g));
    const zui::paint::Color mid = zui::paint::gradient_color_at(g, 50.f, 40.f);
    EXPECT_EQ(mid.r, 255);
    EXPECT_EQ(mid.b, 0);
    const zui::paint::Color edge = zui::paint::gradient_color_at(g, 100.f, 80.f);
    EXPECT_EQ(edge.r, 0);
    EXPECT_EQ(edge.b, 255);

    ASSERT_TRUE(zui::paint::gradient_image_for_box(
        "repeating-linear-gradient(to right, #000000, #ffffff 25%)", box, g));
    EXPECT_TRUE(g.repeating);
}

TEST(PaintRound, ConicAndRepeatingRadialGradients) {
    const zui::paint::Rect box{0, 0, 100, 80};
    zui::paint::Gradient g;
    ASSERT_TRUE(zui::paint::conic_gradient_for_box("conic-gradient(#ff0000, #0000ff)", box, g));
    EXPECT_EQ(g.kind, zui::paint::GradientKind::Conic);
    EXPECT_FALSE(g.repeating);
    EXPECT_NEAR(g.x, 50.f, 0.01f);
    EXPECT_NEAR(g.y, 40.f, 0.01f);
    EXPECT_NEAR(g.angle, 0.f, 0.001f);
    // 0deg is up, so the point above the centre is the first stop.
    const zui::paint::Color up = zui::paint::gradient_color_at(g, 50.f, 10.f);
    EXPECT_EQ(up.r, 255);
    EXPECT_EQ(up.b, 0);
    // A quarter turn clockwise (to the right) is 75% of the first stop and 25% of the last.
    const zui::paint::Color right = zui::paint::gradient_color_at(g, 90.f, 40.f);
    EXPECT_NEAR(static_cast<int>(right.r), 191, 2);
    EXPECT_NEAR(static_cast<int>(right.b), 64, 2);
    const zui::paint::Color down = zui::paint::gradient_color_at(g, 50.f, 70.f);
    EXPECT_NEAR(static_cast<int>(down.r), 128, 2);
    EXPECT_NEAR(static_cast<int>(down.b), 128, 2);

    ASSERT_TRUE(zui::paint::conic_gradient_for_box(
        "conic-gradient(from 90deg at 30px 20px, #ffffff, #000000)", box, g));
    EXPECT_NEAR(g.x, 30.f, 0.01f);
    EXPECT_NEAR(g.y, 20.f, 0.01f);
    EXPECT_NEAR(g.angle, 3.14159265f / 2.f, 0.001f);
    // `from 90deg` puts the first stop to the right of the centre.
    const zui::paint::Color from_right = zui::paint::gradient_color_at(g, 80.f, 20.f);
    EXPECT_EQ(from_right.r, 255);
    EXPECT_EQ(from_right.b, 255);

    ASSERT_TRUE(zui::paint::conic_gradient_for_box(
        "conic-gradient(#ff0000 0% 25%, #0000ff 25% 100%)", box, g));
    // 45deg clockwise from up is inside the first quarter.
    const zui::paint::Color wedge = zui::paint::gradient_color_at(g, 70.f, 20.f);
    EXPECT_EQ(wedge.r, 255);
    EXPECT_EQ(wedge.b, 0);
    const zui::paint::Color opposite = zui::paint::gradient_color_at(g, 50.f, 70.f);
    EXPECT_EQ(opposite.r, 0);
    EXPECT_EQ(opposite.b, 255);

    ASSERT_TRUE(zui::paint::repeating_radial_gradient_for_box(
        "repeating-radial-gradient(circle, #ff0000 0, #0000ff 20px)", box, g));
    EXPECT_TRUE(g.repeating);
    EXPECT_EQ(g.kind, zui::paint::GradientKind::Radial);

    ASSERT_TRUE(zui::paint::gradient_image_for_box(
        "repeating-conic-gradient(#000000 0% 10%, #ffffff 10% 20%)", box, g));
    EXPECT_TRUE(g.repeating);
    EXPECT_EQ(g.kind, zui::paint::GradientKind::Conic);
    const zui::paint::Color spoke = zui::paint::gradient_color_at(g, 50.f, 10.f);
    EXPECT_EQ(spoke.r, 0);
    // 54deg is 0.15 turn, inside the white half of the 0.20 period.
    const zui::paint::Color band = zui::paint::gradient_color_at(g, 74.f, 22.f);
    EXPECT_GE(band.r, 250);
}

// P1-5 B: `contain: paint` writes `Overflow::ink`, clips the descendants to the padding box
// (CSS Containment section 2.4) and stops hit testing at the same edge. The control below has no
// containment, so the identical child stays reachable past its parent's box.
TEST(PaintRound, ContainPaintClipsPaintAndHitTesting) {
    zui::html::Document doc;
    std::string error;
    ASSERT_TRUE(
        zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/paint_contain.html", doc, error))
        << error;
    zui::css::Engine css;
    ASSERT_TRUE(css.load_sheets(doc, error)) << error;
    css.cascade(doc);
    zui::layout::FreeTypeTextMetrics metrics;
    zui::layout::LayoutEngine layout(metrics);
    layout.layout(doc, zui::layout::Viewport{400.0f, 300.0f});

    zui::layout::Box* clipper = layout.principal_box(zui::html::find(doc, "clipper"));
    zui::layout::Box* spill = layout.principal_box(zui::html::find(doc, "spill"));
    zui::layout::Box* plain = layout.principal_box(zui::html::find(doc, "plain"));
    zui::layout::Box* spill2 = layout.principal_box(zui::html::find(doc, "spill2"));
    ASSERT_NE(clipper, nullptr);
    ASSERT_NE(spill, nullptr);
    ASSERT_NE(plain, nullptr);
    ASSERT_NE(spill2, nullptr);

    const zui::layout::LayoutResult* clipped = clipper->cache->pinned_final();
    const zui::layout::LayoutResult* open = plain->cache->pinned_final();
    ASSERT_NE(clipped, nullptr);
    ASSERT_NE(open, nullptr);
    EXPECT_TRUE(clipped->overflow.clipped_by_paint_contain);
    EXPECT_FALSE(open->overflow.clipped_by_paint_contain);
    // The ink rectangle is the union of the box with whatever the layout put inside it: the child
    // starts at 16px (6px border + 10px padding) and is 200x120, so the rectangle is 216x136 and
    // reaches well past the 120x80 padding box. The containment flag is what differs between the
    // two boxes; the rectangles themselves are the same.
    EXPECT_GT(clipped->overflow.ink.w, clipped->geometry.border_box_inline);
    EXPECT_GT(clipped->overflow.ink.h, clipped->geometry.border_box_block);
    EXPECT_FLOAT_EQ(clipped->overflow.ink.w, 216.f);
    EXPECT_FLOAT_EQ(clipped->overflow.ink.h, 136.f);
    EXPECT_FLOAT_EQ(open->overflow.ink.w, clipped->overflow.ink.w);
    EXPECT_FLOAT_EQ(open->overflow.ink.h, clipped->overflow.ink.h);

    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.paint(*layout.root(), list);
    // The padding box: the border box inset by the 6px border on each side.
    const float padding_x = clipper->geo.world_x + 6.f;
    const float padding_y = clipper->geo.world_y + 6.f;
    bool clipped_at_padding_box = false;
    for (const zui::paint::DisplayItem& item : list.items()) {
        if (item.op != zui::paint::DisplayOp::PushClip) {
            continue;
        }
        if (std::abs(item.rect.x - padding_x) < 0.01f &&
            std::abs(item.rect.y - padding_y) < 0.01f && std::abs(item.rect.w - 120.f) < 0.01f &&
            std::abs(item.rect.h - 80.f) < 0.01f) {
            clipped_at_padding_box = true;
        }
    }
    EXPECT_TRUE(clipped_at_padding_box) << "no clip at the contained box's padding box";

    // Hit testing shares the painter's edge: a point inside the padding box still reaches the
    // child, and the 6px border ring belongs to the container alone.
    EXPECT_EQ(layout.hit_test(padding_x + 15.f, padding_y + 15.f), spill);
    EXPECT_EQ(layout.hit_test(clipper->geo.world_x + 2.f, clipper->geo.world_y + 2.f), clipper);
    // The control has no containment, so the same child is reachable outside the container's box.
    EXPECT_EQ(layout.hit_test(plain->geo.world_x + 150.f, plain->geo.world_y + 20.f), spill2);
}
