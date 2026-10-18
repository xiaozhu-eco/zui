// Borders, blend/filter/clip, gradients, opacity, radii, shadows, and backgrounds.
// L6 architecture self-checks (TESTPLAN section 12).
// Shared scene harness: selfcheck_scene.h.

#include "selfcheck_scene.h"

#include <cmath>

using namespace difftest_selfcheck;

// B3: non-solid border-style emits FillRect strips (dash/dot/double/3D), not one stroke.
TEST(DifftestSelfCheck, BorderStylesNonSolid) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "border-styles.html")) << error;

    auto style_of = [&](const char* id) -> const zui::layout::LayoutStyle* {
        zui::layout::Box* b = scene.box(id);
        EXPECT_NE(b, nullptr) << id;
        return b == nullptr ? nullptr : &b->style();
    };
    using BS = zui::layout::BorderLineStyle;
    EXPECT_EQ(style_of("dashed")->border_top_style, BS::Dashed);
    EXPECT_EQ(style_of("dotted")->border_top_style, BS::Dotted);
    EXPECT_EQ(style_of("double")->border_top_style, BS::Double);
    EXPECT_EQ(style_of("groove")->border_top_style, BS::Groove);
    EXPECT_EQ(style_of("ridge")->border_top_style, BS::Ridge);
    EXPECT_EQ(style_of("inset")->border_top_style, BS::Inset);
    EXPECT_EQ(style_of("outset")->border_top_style, BS::Outset);
    EXPECT_EQ(style_of("solid")->border_top_style, BS::Solid);

    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.set_retained(false);
    painter.paint(*scene.layout->root(), list, 800, 600);

    auto count_fills_in = [&](zui::layout::Box* box, float* out_min_lum, float* out_max_lum) {
        int n = 0;
        float min_lum = 1e9f;
        float max_lum = -1e9f;
        if (box == nullptr) {
            return 0;
        }
        const float x0 = box->geo.world_x - 0.5f;
        const float y0 = box->geo.world_y - 0.5f;
        const float x1 = box->geo.world_x + box->geo.width + 0.5f;
        const float y1 = box->geo.world_y + box->geo.height + 0.5f;
        for (const auto& it : list.items()) {
            if (it.op != zui::paint::DisplayOp::FillRect) {
                continue;
            }
            // Skip the padding-box background fill (near full content size).
            if (it.rect.w > box->geo.width - 4.f && it.rect.h > box->geo.height - 4.f) {
                continue;
            }
            const float cx = it.rect.x + it.rect.w * 0.5f;
            const float cy = it.rect.y + it.rect.h * 0.5f;
            if (cx < x0 || cy < y0 || cx > x1 || cy > y1) {
                continue;
            }
            ++n;
            const float lum = 0.2126f * it.color.r + 0.7152f * it.color.g + 0.0722f * it.color.b;
            min_lum = std::min(min_lum, lum);
            max_lum = std::max(max_lum, lum);
        }
        if (out_min_lum != nullptr) {
            *out_min_lum = min_lum;
        }
        if (out_max_lum != nullptr) {
            *out_max_lum = max_lum;
        }
        return n;
    };

    zui::layout::Box* dashed = scene.box("dashed");
    zui::layout::Box* dotted = scene.box("dotted");
    zui::layout::Box* dbl = scene.box("double");
    ASSERT_NE(dashed, nullptr);
    ASSERT_NE(dotted, nullptr);
    ASSERT_NE(dbl, nullptr);
    EXPECT_GE(count_fills_in(dashed, nullptr, nullptr), 8)
        << "dashed should emit many short FillRect segments";
    EXPECT_GE(count_fills_in(dotted, nullptr, nullptr), 8)
        << "dotted should emit many FillRect dots";
    EXPECT_GE(count_fills_in(dbl, nullptr, nullptr), 6)
        << "double should emit outer+inner bands on multiple sides";

    int dotted_rounded = 0;
    {
        const float x0 = dotted->geo.world_x - 0.5f;
        const float y0 = dotted->geo.world_y - 0.5f;
        const float x1 = dotted->geo.world_x + dotted->geo.width + 0.5f;
        const float y1 = dotted->geo.world_y + dotted->geo.height + 0.5f;
        for (const auto& it : list.items()) {
            if (it.op != zui::paint::DisplayOp::FillRect) {
                continue;
            }
            const float cx = it.rect.x + it.rect.w * 0.5f;
            const float cy = it.rect.y + it.rect.h * 0.5f;
            if (cx < x0 || cy < y0 || cx > x1 || cy > y1) {
                continue;
            }
            if (it.radii.max_component() > 1.f) {
                ++dotted_rounded;
            }
        }
    }
    EXPECT_GE(dotted_rounded, 4) << "dotted borders use rounded FillRect dots";

    for (const char* id : {"groove", "ridge", "inset", "outset"}) {
        zui::layout::Box* box = scene.box(id);
        ASSERT_NE(box, nullptr) << id;
        float min_lum = 0.f;
        float max_lum = 0.f;
        const int n = count_fills_in(box, &min_lum, &max_lum);
        EXPECT_GE(n, 4) << id << " should paint four edge fills (or more for groove/ridge)";
        EXPECT_GT(max_lum - min_lum, 8.f) << id << " should use distinct light/dark tones";
    }
}

TEST(DifftestSelfCheck, MaskAndMixBlend) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "mask-blend.html")) << error;

    auto prop = [&](const char* id, const char* key) {
        zui::html::Node* n = zui::html::find(scene.doc, id);
        EXPECT_NE(n, nullptr) << id;
        if (n == nullptr || n->computed == nullptr) {
            return std::string("<missing>");
        }
        const auto it = n->computed->find(key);
        return it == n->computed->end() ? std::string("<absent>") : std::string(it->second);
    };
    EXPECT_NE(prop("mask", "mask-image").find("linear-gradient"), std::string::npos)
        << prop("mask", "mask-image");
    EXPECT_EQ(prop("mul", "mix-blend-mode"), "multiply");
    EXPECT_EQ(prop("scr", "mix-blend-mode"), "screen");

    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.set_retained(false);
    painter.set_resource_base(scene.doc.source_path);
    painter.paint(*scene.layout->root(), list, 800, 600);

    int begins = 0;
    bool saw_mask = false;
    bool saw_multiply = false;
    bool saw_screen = false;
    for (const auto& it : list.items()) {
        if (it.op != zui::paint::DisplayOp::BeginFilter) {
            continue;
        }
        ++begins;
        if (it.filter.has_mask) {
            saw_mask = true;
        }
        if (it.filter.blend == zui::paint::MixBlendMode::Multiply) {
            saw_multiply = true;
        }
        if (it.filter.blend == zui::paint::MixBlendMode::Screen) {
            saw_screen = true;
        }
    }
    EXPECT_GE(begins, 3);
    EXPECT_TRUE(saw_mask);
    EXPECT_TRUE(saw_multiply);
    EXPECT_TRUE(saw_screen);
}

// B3: filter blur/brightness/opacity emits BeginFilter/EndFilter around the box.
TEST(DifftestSelfCheck, FilterEffectsBasic) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "filter.html")) << error;

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
    EXPECT_NE(prop(style_of("bright"), "filter").find("brightness"), std::string::npos);
    EXPECT_NE(prop(style_of("blur"), "filter").find("blur"), std::string::npos);

    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.set_retained(false);
    painter.paint(*scene.layout->root(), list, 800, 600);

    int begins = 0;
    int ends = 0;
    bool saw_blur = false;
    bool saw_bright = false;
    for (const auto& it : list.items()) {
        if (it.op == zui::paint::DisplayOp::BeginFilter) {
            ++begins;
            if (it.filter.blur > 0.5f) {
                saw_blur = true;
            }
            if (it.filter.brightness > 1.01f) {
                saw_bright = true;
            }
        }
        if (it.op == zui::paint::DisplayOp::EndFilter) {
            ++ends;
        }
    }
    EXPECT_EQ(begins, ends);
    EXPECT_GE(begins, 3);
    EXPECT_TRUE(saw_blur);
    EXPECT_TRUE(saw_bright);
}

// filter: drop-shadow() keeps the offset, the CSS blur radius, and the color (currentcolor when
// the color is omitted). The display item is what the GL composite reads.
TEST(DifftestSelfCheck, FilterDropShadow) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "filter.html")) << error;

    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.set_retained(false);
    painter.paint(*scene.layout->root(), list, 800, 600);

    const zui::paint::DropShadow* drop = nullptr;
    const zui::paint::DropShadow* ink = nullptr;
    const zui::paint::DropShadow* rgba = nullptr;
    for (const auto& it : list.items()) {
        if (it.op != zui::paint::DisplayOp::BeginFilter || it.filter.shadows.empty()) {
            continue;
        }
        const zui::paint::DropShadow& s = it.filter.shadows[0];
        if (std::abs(s.dx - 10.f) < 0.1f) {
            drop = &s;
        } else if (std::abs(s.dx - 3.f) < 0.1f) {
            ink = &s;
        } else if (std::abs(s.dx - 2.f) < 0.1f) {
            rgba = &s;
        }
    }
    ASSERT_NE(drop, nullptr);
    EXPECT_NEAR(drop->dy, 12.f, 0.1f);
    EXPECT_NEAR(drop->blur, 4.f, 0.1f);
    EXPECT_EQ(drop->color.r, 16);
    EXPECT_EQ(drop->color.g, 32);
    EXPECT_EQ(drop->color.b, 64);
    EXPECT_EQ(drop->color.a, 255);
    ASSERT_NE(ink, nullptr);
    EXPECT_NEAR(ink->dy, 4.f, 0.1f);
    EXPECT_NEAR(ink->blur, 0.f, 0.1f);
    EXPECT_EQ(ink->color.r, 204);
    EXPECT_EQ(ink->color.g, 51);
    EXPECT_EQ(ink->color.b, 68);
    ASSERT_NE(rgba, nullptr);
    EXPECT_EQ(rgba->color.r, 16);
    EXPECT_EQ(rgba->color.g, 32);
    EXPECT_EQ(rgba->color.b, 64);
    EXPECT_EQ(rgba->color.a, 128);
}

// B3: clip-path inset/circle/ellipse clips paint and hit-testing.
TEST(DifftestSelfCheck, ClipPathBasicShapes) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "clip-path.html")) << error;

    zui::layout::ClipPathShape shape;
    ASSERT_TRUE(zui::layout::parse_clip_path("inset(10px)", 80.f, 60.f, shape));
    EXPECT_EQ(shape.kind, zui::layout::ClipPathShape::Kind::Inset);
    EXPECT_NEAR(shape.inset_t, 10.f, 0.01f);
    EXPECT_TRUE(zui::layout::point_in_clip_path(shape, 80.f, 60.f, 40.f, 30.f));
    EXPECT_FALSE(zui::layout::point_in_clip_path(shape, 80.f, 60.f, 5.f, 5.f));

    ASSERT_TRUE(zui::layout::parse_clip_path("circle(25px at center)", 80.f, 60.f, shape));
    EXPECT_EQ(shape.kind, zui::layout::ClipPathShape::Kind::Circle);
    EXPECT_NEAR(shape.cx, 40.f, 0.01f);
    EXPECT_NEAR(shape.cy, 30.f, 0.01f);
    EXPECT_TRUE(zui::layout::point_in_clip_path(shape, 80.f, 60.f, 40.f, 30.f));
    EXPECT_FALSE(zui::layout::point_in_clip_path(shape, 80.f, 60.f, 70.f, 30.f));

    ASSERT_TRUE(zui::layout::parse_clip_path("ellipse(35px 20px at 40px 30px)", 80.f, 60.f, shape));
    EXPECT_EQ(shape.kind, zui::layout::ClipPathShape::Kind::Ellipse);

    ASSERT_TRUE(zui::layout::parse_clip_path("polygon(50% 0, 100% 100%, 0 100%)", 80.f, 60.f, shape));
    EXPECT_EQ(shape.kind, zui::layout::ClipPathShape::Kind::Polygon);
    EXPECT_FALSE(shape.evenodd);
    ASSERT_EQ(shape.points.size(), 3u);
    EXPECT_NEAR(shape.points[0].first, 40.f, 0.01f);
    EXPECT_NEAR(shape.points[0].second, 0.f, 0.01f);
    EXPECT_TRUE(zui::layout::point_in_clip_path(shape, 80.f, 60.f, 40.f, 30.f));
    EXPECT_FALSE(zui::layout::point_in_clip_path(shape, 80.f, 60.f, 70.f, 10.f));

    ASSERT_TRUE(zui::layout::parse_clip_path(
        "polygon(evenodd, 0 0, 100% 0, 100% 100%, 0 100%, 0 0, 25% 25%, 75% 25%, 75% 75%, 25% 75%, 25% 25%)",
        80.f, 60.f, shape));
    EXPECT_TRUE(shape.evenodd);
    EXPECT_FALSE(zui::layout::point_in_clip_path(shape, 80.f, 60.f, 40.f, 30.f));
    EXPECT_TRUE(zui::layout::point_in_clip_path(shape, 80.f, 60.f, 4.f, 30.f));

    ASSERT_TRUE(zui::layout::parse_clip_path(
        "polygon(nonzero, 0 0, 100% 0, 100% 100%, 0 100%, 0 0, 25% 25%, 75% 25%, 75% 75%, 25% 75%, 25% 25%)",
        80.f, 60.f, shape));
    EXPECT_FALSE(shape.evenodd);
    EXPECT_TRUE(zui::layout::point_in_clip_path(shape, 80.f, 60.f, 40.f, 30.f));

    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.set_retained(false);
    painter.paint(*scene.layout->root(), list, 800, 600);
    int clips = 0;
    for (const auto& it : list.items()) {
        if (it.op == zui::paint::DisplayOp::PushClip) {
            ++clips;
        }
    }
    EXPECT_GE(clips, 5) << "inset/circle/ellipse/polygon/evenodd each push a clip";

    zui::layout::Box* poly = scene.box("poly");
    zui::layout::Box* hole = scene.box("hole");
    ASSERT_NE(poly, nullptr);
    ASSERT_NE(hole, nullptr);
    bool found_poly = false;
    bool found_hole = false;
    for (const auto& it : list.items()) {
        if (it.op != zui::paint::DisplayOp::PushClip || it.poly.size() < 6) {
            continue;
        }
        if (!it.poly_evenodd && it.poly.size() == 6) {
            EXPECT_NEAR(it.poly[0], poly->geo.world_x + 40.f, 0.05f);
            EXPECT_NEAR(it.poly[1], poly->geo.world_y, 0.05f);
            EXPECT_NEAR(it.poly[2], poly->geo.world_x + poly->geo.width, 0.05f);
            EXPECT_NEAR(it.poly[3], poly->geo.world_y + poly->geo.height, 0.05f);
            found_poly = true;
        }
        if (it.poly_evenodd) {
            EXPECT_GE(it.poly.size(), 16u);
            found_hole = true;
        }
    }
    EXPECT_TRUE(found_poly);
    EXPECT_TRUE(found_hole);

    const float px = poly->geo.world_x + poly->geo.width * 0.5f;
    const float py = poly->geo.world_y + poly->geo.height * 0.5f;
    EXPECT_EQ(scene.layout->hit_test(px, py), poly);
    EXPECT_NE(scene.layout->hit_test(poly->geo.world_x + 2.f, poly->geo.world_y + 2.f), poly);
    EXPECT_NE(scene.layout->hit_test(hole->geo.world_x + 40.f, hole->geo.world_y + 30.f), hole);
    EXPECT_EQ(scene.layout->hit_test(hole->geo.world_x + 4.f, hole->geo.world_y + 30.f), hole);

    zui::layout::Box* circle = scene.box("circle");
    ASSERT_NE(circle, nullptr);
    const float cx = circle->geo.world_x + circle->geo.width * 0.5f;
    const float cy = circle->geo.world_y + circle->geo.height * 0.5f;
    EXPECT_EQ(scene.layout->hit_test(cx, cy), circle);
    EXPECT_NE(scene.layout->hit_test(circle->geo.world_x + 2.f, cy), circle);
}

// B3: radial-gradient and repeating-linear-gradient emit FillGradient items.
TEST(DifftestSelfCheck, RadialAndRepeatingGradients) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "radial-gradient.html")) << error;

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
    EXPECT_NE(prop(style_of("radial"), "background-image").find("radial-gradient"),
              std::string::npos);
    EXPECT_NE(prop(style_of("repeat"), "background-image").find("repeating-linear-gradient"),
              std::string::npos);
    EXPECT_NE(prop(style_of("conic"), "background-image").find("conic-gradient"),
              std::string::npos);
    EXPECT_NE(prop(style_of("reprad"), "background-image").find("repeating-radial-gradient"),
              std::string::npos);

    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.set_retained(false);
    painter.paint(*scene.layout->root(), list, 800, 600);

    int radial = 0;
    int repeating = 0;
    int conic = 0;
    int repeating_radial = 0;
    for (const auto& it : list.items()) {
        if (it.op != zui::paint::DisplayOp::FillGradient) {
            continue;
        }
        if (it.gradient.kind == zui::paint::GradientKind::Radial) {
            ++radial;
            EXPECT_GT(it.gradient.rx, 0.f);
            EXPECT_GT(it.gradient.ry, 0.f);
            if (it.gradient.repeating) {
                ++repeating_radial;
            }
        }
        if (it.gradient.kind == zui::paint::GradientKind::Linear && it.gradient.repeating) {
            ++repeating;
        }
        if (it.gradient.kind == zui::paint::GradientKind::Conic) {
            ++conic;
            EXPECT_NEAR(it.gradient.angle, 20.f * 3.14159265f / 180.f, 0.01f);
        }
    }
    EXPECT_GE(radial, 3);
    EXPECT_GE(repeating, 1);
    EXPECT_GE(conic, 1);
    EXPECT_GE(repeating_radial, 1);
}

// A5: linear-gradient with >4 stops keeps every stop on the DisplayList; GPU backends loop
// 4-stop windows via gradient_gpu_chunks (negative grad_count + stroke_flag clamp).
TEST(DifftestSelfCheck, LinearGradientManyStops) {
    zui::paint::Gradient g;
    g.kind = zui::paint::GradientKind::Linear;
    g.len = 1.f;
    g.dy = 0.f;
    g.dx = 1.f;
    auto push = [&](float o, std::uint8_t r, std::uint8_t gch, std::uint8_t b) {
        zui::paint::GradientStop s;
        s.offset = o;
        s.color = zui::paint::Color{r, gch, b, 255};
        g.stops.push_back(s);
    };
    push(0.f, 255, 0, 0);
    push(0.2f, 255, 255, 0);
    push(0.4f, 0, 255, 0);
    push(0.6f, 0, 255, 255);
    push(0.8f, 0, 0, 255);
    push(1.f, 255, 0, 255);
    std::vector<zui::paint::GradientGpuChunk> chunks;
    zui::paint::gradient_gpu_chunks(g, chunks);
    ASSERT_EQ(chunks.size(), std::size_t{2});
    EXPECT_LT(chunks[0].grad_count, 0);
    EXPECT_EQ(-chunks[0].grad_count, 4);
    EXPECT_FLOAT_EQ(chunks[0].stroke_flag, -2.f);
    EXPECT_LT(chunks[1].grad_count, 0);
    EXPECT_EQ(-chunks[1].grad_count, 3);
    EXPECT_FLOAT_EQ(chunks[1].stroke_flag, -1.f);

    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "gradient-stops.html")) << error;
    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.set_retained(false);
    painter.paint(*scene.layout->root(), list, 800, 600);
    int six = 0;
    int two = 0;
    for (const auto& it : list.items()) {
        if (it.op != zui::paint::DisplayOp::FillGradient) {
            continue;
        }
        if (it.gradient.stops.size() >= 6) {
            ++six;
        }
        if (it.gradient.stops.size() == 2) {
            ++two;
        }
    }
    EXPECT_GE(six, 1);
    EXPECT_GE(two, 1);
}

// A5: opacity < 1 emits a BeginFilter group. The group's composited colour is a framebuffer
// readback, not a second raster of this list.
TEST(DifftestSelfCheck, OpacityIsolationGroup) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "opacity-isolate.html")) << error;

    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.set_retained(false);
    painter.paint(*scene.layout->root(), list, 800, 600);
    int opacity_groups = 0;
    for (const auto& it : list.items()) {
        if (it.op == zui::paint::DisplayOp::BeginFilter && it.filter.opacity < 0.999f) {
            ++opacity_groups;
        }
    }
    EXPECT_GE(opacity_groups, 2);
}

// B3/A5: per-corner and elliptical border-radius resolve into LayoutStyle / paint radii.
TEST(DifftestSelfCheck, BorderRadiusCornersAndEllipse) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "border-radius.html")) << error;

    auto style = [&](const char* id) -> const zui::layout::LayoutStyle* {
        zui::layout::Box* b = scene.box(id);
        EXPECT_NE(b, nullptr) << id;
        if (b == nullptr || b->style_ptr == nullptr) {
            return nullptr;
        }
        return &b->style();
    };

    const auto* single = style("single");
    ASSERT_NE(single, nullptr);
    EXPECT_NEAR(single->border_top_left_radius, 12.f, 0.01f);
    EXPECT_NEAR(single->border_top_right_radius, 12.f, 0.01f);
    EXPECT_NEAR(single->border_bottom_right_radius, 12.f, 0.01f);
    EXPECT_NEAR(single->border_bottom_left_radius, 12.f, 0.01f);
    EXPECT_FALSE(single->border_top_left_radius_percent);

    const auto* per = style("per");
    ASSERT_NE(per, nullptr);
    EXPECT_NEAR(per->border_top_left_radius, 2.f, 0.01f);
    EXPECT_NEAR(per->border_top_right_radius, 16.f, 0.01f);
    EXPECT_NEAR(per->border_bottom_right_radius, 8.f, 0.01f);
    EXPECT_NEAR(per->border_bottom_left_radius, 4.f, 0.01f);

    const auto* slash = style("slash");
    ASSERT_NE(slash, nullptr);
    EXPECT_NEAR(slash->border_top_left_radius, 40.f, 0.01f);
    EXPECT_NEAR(slash->border_top_left_radius_y, 20.f, 0.01f);
    EXPECT_NEAR(slash->border_top_right_radius, 40.f, 0.01f);
    EXPECT_NEAR(slash->border_top_right_radius_y, 20.f, 0.01f);

    const auto* pct = style("pct");
    ASSERT_NE(pct, nullptr);
    EXPECT_TRUE(pct->border_top_left_radius_percent);
    EXPECT_NEAR(pct->border_top_left_radius, 50.f, 0.01f);

    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.set_retained(false);
    painter.paint(*scene.layout->root(), list, 800, 600);

    bool saw_ellipse = false;
    bool saw_asymmetric = false;
    for (const auto& it : list.items()) {
        if (it.op != zui::paint::DisplayOp::FillRect || !it.radii.any()) {
            continue;
        }
        if (std::fabs(it.radii.tl - it.radii.tl_y) > 1.f) {
            saw_ellipse = true;
        }
        if (std::fabs(it.radii.tl - it.radii.tr) > 1.f) {
            saw_asymmetric = true;
        }
    }
    EXPECT_TRUE(saw_ellipse) << "slash elliptical radii should reach FillRect";
    EXPECT_TRUE(saw_asymmetric) << "per-corner radii should reach FillRect";
}

// A5: unequal-width rounded borders keep outer arcs (punch or stroke_ring), not bare strips.
TEST(DifftestSelfCheck, BorderRadiusUnevenWidths) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "radius-uneven.html")) << error;

    zui::layout::Box* uneven = scene.box("uneven");
    zui::layout::Box* mixed = scene.box("mixed");
    zui::layout::Box* transparent = scene.box("transparent");
    zui::layout::Box* open = scene.box("open");
    ASSERT_NE(uneven, nullptr);
    ASSERT_NE(mixed, nullptr);
    ASSERT_NE(transparent, nullptr);
    ASSERT_NE(open, nullptr);

    EXPECT_NEAR(uneven->geo.border.top, 2.f, 0.01f);
    EXPECT_NEAR(uneven->geo.border.right, 10.f, 0.01f);
    EXPECT_NEAR(uneven->geo.border.bottom, 6.f, 0.01f);
    EXPECT_NEAR(uneven->geo.border.left, 14.f, 0.01f);
    EXPECT_NEAR(open->geo.border.left, 0.f, 0.01f);
    EXPECT_EQ(open->style().border_left_style, zui::layout::BorderLineStyle::None);

    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.set_retained(false);
    painter.paint(*scene.layout->root(), list, 800, 600);

    auto box_hit = [](const zui::layout::Box& box, const zui::paint::Rect& r) {
        const float x0 = box.geo.world_x - 1.f;
        const float y0 = box.geo.world_y - 1.f;
        const float x1 = box.geo.world_x + box.geo.width + 1.f;
        const float y1 = box.geo.world_y + box.geo.height + 1.f;
        return r.x + r.w > x0 && r.x < x1 && r.y + r.h > y0 && r.y < y1;
    };

    bool uneven_rounded = false;
    bool transparent_ring = false;
    bool mixed_rounded = false;
    for (const auto& it : list.items()) {
        if (it.op == zui::paint::DisplayOp::FillRect && it.radii.any()) {
            if (box_hit(*uneven, it.rect) && it.rect.w > 40.f && it.rect.h > 40.f) {
                uneven_rounded = true;
            }
            if (box_hit(*mixed, it.rect) && it.rect.w > 40.f && it.rect.h > 40.f) {
                mixed_rounded = true;
            }
        }
        if (it.op == zui::paint::DisplayOp::StrokeRect && it.hole.w > 0.5f && it.hole.h > 0.5f &&
            box_hit(*transparent, it.rect)) {
            transparent_ring = true;
            EXPECT_GT(it.radii.max_component(), 0.5f) << "transparent uneven border needs radii";
        }
    }
    EXPECT_TRUE(uneven_rounded) << "opaque unequal borders should fill a rounded outer box";
    EXPECT_TRUE(mixed_rounded) << "mixed-colour unequal borders should keep rounded fills";
    EXPECT_TRUE(transparent_ring) << "transparent bg should emit stroke_ring (outer − hole)";
}

// B3: single-layer outer box-shadow emits ShadowRect under the border box.
TEST(DifftestSelfCheck, BoxShadowOuter) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "box-shadow.html")) << error;

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

    EXPECT_NE(prop(style_of("hard"), "box-shadow").find("6px"), std::string::npos);
    EXPECT_NE(prop(style_of("ring"), "box-shadow").find("3px"), std::string::npos);

    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.set_retained(false);
    painter.paint(*scene.layout->root(), list, 800, 600);

    struct ShadowHit {
        float x = 0;
        float y = 0;
        float w = 0;
        float h = 0;
        float blur = 0;
        float radius = 0;
        bool inset = false;
        float hole_w = 0;
        float hole_h = 0;
        zui::paint::Color color;
    };
    std::vector<ShadowHit> shadows;
    for (const auto& it : list.items()) {
        if (it.op != zui::paint::DisplayOp::ShadowRect) {
            continue;
        }
        shadows.push_back(ShadowHit{it.rect.x, it.rect.y, it.rect.w, it.rect.h, it.blur,
                                    it.radii.max_component(), it.inset, it.hole.w, it.hole.h,
                                    it.color});
    }
    ASSERT_GE(shadows.size(), std::size_t{6})
        << "expected ShadowRect for hard/ring/soft/rounded/multi×2/inset";

    zui::layout::Box* hard = scene.box("hard");
    ASSERT_NE(hard, nullptr);
    bool saw_hard = false;
    for (const ShadowHit& s : shadows) {
        if (s.blur > 0.5f) {
            continue;
        }
        if (std::fabs(s.w - hard->geo.width) < 0.5f && std::fabs(s.h - hard->geo.height) < 0.5f &&
            std::fabs(s.x - (hard->geo.world_x + 6.f)) < 1.f &&
            std::fabs(s.y - (hard->geo.world_y + 6.f)) < 1.f && s.color.b > 200) {
            saw_hard = true;
            break;
        }
    }
    EXPECT_TRUE(saw_hard) << "hard shadow should be a 6px-offset copy of the border box";

    zui::layout::Box* ring = scene.box("ring");
    ASSERT_NE(ring, nullptr);
    bool saw_ring = false;
    for (const ShadowHit& s : shadows) {
        if (s.blur > 0.5f) {
            continue;
        }
        if (std::fabs(s.w - (ring->geo.width + 6.f)) < 0.5f &&
            std::fabs(s.h - (ring->geo.height + 6.f)) < 0.5f &&
            std::fabs(s.x - (ring->geo.world_x - 3.f)) < 1.f &&
            std::fabs(s.y - (ring->geo.world_y - 3.f)) < 1.f && s.color.g > 100) {
            EXPECT_LT(s.radius, 0.5f) << "zero-radius box keeps a sharp spread ring";
            saw_ring = true;
            break;
        }
    }
    EXPECT_TRUE(saw_ring) << "ring shadow should expand by spread 3px";

    bool saw_blur = false;
    for (const ShadowHit& s : shadows) {
        if (s.blur > 8.f) {
            saw_blur = true;
            break;
        }
    }
    EXPECT_TRUE(saw_blur) << "soft shadow should carry a non-zero blur radius";

    zui::layout::Box* rounded = scene.box("rounded");
    ASSERT_NE(rounded, nullptr);
    bool saw_rounded = false;
    for (const ShadowHit& s : shadows) {
        if (s.blur < 0.5f) {
            continue;
        }
        if (s.radius > 5.f && std::fabs(s.w - rounded->geo.width) < 1.f) {
            saw_rounded = true;
            break;
        }
    }
    EXPECT_TRUE(saw_rounded) << "rounded shadow should keep border-radius on the shape";

    zui::layout::Box* multi = scene.box("multi");
    ASSERT_NE(multi, nullptr);
    int multi_outer = 0;
    for (const ShadowHit& s : shadows) {
        if (s.inset || s.blur > 0.5f) {
            continue;
        }
        if (std::fabs(s.w - multi->geo.width) < 0.5f && std::fabs(s.h - multi->geo.height) < 0.5f) {
            const bool blue = std::fabs(s.x - (multi->geo.world_x + 6.f)) < 1.f && s.color.b > 200;
            const bool green = std::fabs(s.x - (multi->geo.world_x - 4.f)) < 1.f && s.color.g > 100;
            if (blue || green) {
                ++multi_outer;
            }
        }
    }
    EXPECT_GE(multi_outer, 2) << "multi-layer should emit two outer ShadowRect copies";

    zui::layout::Box* inset = scene.box("inset");
    ASSERT_NE(inset, nullptr);
    bool saw_inset = false;
    for (const ShadowHit& s : shadows) {
        if (!s.inset) {
            continue;
        }
        if (std::fabs(s.w - inset->geo.width) < 0.5f && std::fabs(s.h - inset->geo.height) < 0.5f &&
            std::fabs(s.hole_w - (inset->geo.width - 12.f)) < 1.f &&
            std::fabs(s.hole_h - (inset->geo.height - 12.f)) < 1.f && s.color.b > 200) {
            saw_inset = true;
            break;
        }
    }
    EXPECT_TRUE(saw_inset) << "inset ring should punch a hole shrunk by 2×spread";

    bool saw_inset_soft = false;
    for (const ShadowHit& s : shadows) {
        if (s.inset && s.blur > 8.f) {
            saw_inset_soft = true;
            break;
        }
    }
    EXPECT_TRUE(saw_inset_soft) << "soft inset should carry a non-zero blur radius";
}

// B2: object-fit sizes the DrawImage dest rect inside the replaced content box.
TEST(DifftestSelfCheck, ObjectFitPosition) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "object-fit.html")) << error;

    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.set_retained(false);
    painter.set_resource_base(scene.doc.source_path);
    painter.paint(*scene.layout->root(), list, 800, 600);

    auto image_for = [&](const char* id) -> const zui::paint::DisplayItem* {
        zui::layout::Box* b = scene.box(id);
        EXPECT_NE(b, nullptr) << id;
        if (b == nullptr) {
            return nullptr;
        }
        const float cx = b->geo.world_x + b->geo.border.left + b->geo.padding.left;
        const float cy = b->geo.world_y + b->geo.border.top + b->geo.padding.top;
        const float cw = b->geo.content_width;
        const float ch = b->geo.content_height;
        const zui::paint::DisplayItem* hit = nullptr;
        float best = 1e9f;
        for (const auto& it : list.items()) {
            if (it.op != zui::paint::DisplayOp::DrawImage) {
                continue;
            }
            const float mx = it.rect.x + it.rect.w * 0.5f;
            const float my = it.rect.y + it.rect.h * 0.5f;
            if (mx < cx || mx > cx + cw || my < cy || my > cy + ch) {
                continue;
            }
            const float d = std::fabs(mx - (cx + cw * 0.5f)) + std::fabs(my - (cy + ch * 0.5f));
            if (d < best) {
                best = d;
                hit = &it;
            }
        }
        return hit;
    };

    const zui::paint::DisplayItem* fill = image_for("fill");
    ASSERT_NE(fill, nullptr);
    EXPECT_NEAR(fill->rect.w, 96.f, 0.5f);
    EXPECT_NEAR(fill->rect.h, 64.f, 0.5f);

    const zui::paint::DisplayItem* contain = image_for("contain");
    ASSERT_NE(contain, nullptr);
    // 8×8 into 96×64 → scale 8 → 64×64.
    EXPECT_NEAR(contain->rect.w, 64.f, 0.5f);
    EXPECT_NEAR(contain->rect.h, 64.f, 0.5f);

    const zui::paint::DisplayItem* cover = image_for("cover");
    ASSERT_NE(cover, nullptr);
    // scale 12 → 96×96, clipped to content.
    EXPECT_NEAR(cover->rect.w, 96.f, 0.5f);
    EXPECT_NEAR(cover->rect.h, 96.f, 0.5f);

    const zui::paint::DisplayItem* none = image_for("none");
    ASSERT_NE(none, nullptr);
    EXPECT_NEAR(none->rect.w, 4.f, 0.5f);
    EXPECT_NEAR(none->rect.h, 4.f, 0.5f);
}

// A5: body gradient propagates onto the canvas (full viewport FillGradient).
TEST(DifftestSelfCheck, CanvasBackgroundPropagation) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "canvas-bg.html")) << error;

    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.set_retained(false);
    painter.paint(*scene.layout->root(), list, 800, 600);

    bool saw_canvas_gradient = false;
    for (const auto& it : list.items()) {
        if (it.op == zui::paint::DisplayOp::FillGradient && std::fabs(it.rect.w - 800.f) < 0.5f &&
            std::fabs(it.rect.h - 600.f) < 0.5f) {
            saw_canvas_gradient = true;
            break;
        }
    }
    EXPECT_TRUE(saw_canvas_gradient) << "body linear-gradient must paint the full canvas";
}

// B3: single-layer background-image:url() paints DrawImage items with clip/position/size/repeat.
TEST(DifftestSelfCheck, BackgroundImageUrl) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "bg-url.html")) << error;

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

    const auto* stretch = style_of("stretch");
    ASSERT_NE(stretch, nullptr);
    const std::string stretch_img = prop(stretch, "background-image");
    EXPECT_NE(stretch_img.find("url("), std::string::npos) << stretch_img;
    EXPECT_NE(stretch_img.find("red8.png"), std::string::npos) << stretch_img;

    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.set_retained(false);
    painter.set_resource_base(scene.doc.source_path);
    painter.paint(*scene.layout->root(), list, 800, 600);

    struct Hit {
        float x = 0;
        float y = 0;
        float w = 0;
        float h = 0;
        std::string path;
    };
    std::vector<Hit> images;
    for (const auto& it : list.items()) {
        if (it.op != zui::paint::DisplayOp::DrawImage) {
            continue;
        }
        images.push_back(Hit{it.rect.x, it.rect.y, it.rect.w, it.rect.h, it.text});
    }
    ASSERT_FALSE(images.empty()) << "expected DrawImage items for url() backgrounds";

    auto find_box = [&](const char* id) -> zui::layout::Box* {
        zui::layout::Box* b = scene.box(id);
        EXPECT_NE(b, nullptr) << id;
        return b;
    };

    zui::layout::Box* stretch_box = find_box("stretch");
    ASSERT_NE(stretch_box, nullptr);
    bool saw_stretch = false;
    for (const Hit& h : images) {
        if (h.path.find("red8.png") == std::string::npos) {
            continue;
        }
        if (std::fabs(h.w - stretch_box->geo.width) < 1.f &&
            std::fabs(h.h - stretch_box->geo.height) < 1.f &&
            std::fabs(h.x - stretch_box->geo.world_x) < 1.f &&
            std::fabs(h.y - stretch_box->geo.world_y) < 1.f) {
            saw_stretch = true;
            break;
        }
    }
    EXPECT_TRUE(saw_stretch) << "stretch box should draw one full-size red8 image";

    zui::layout::Box* tile_box = find_box("tile");
    ASSERT_NE(tile_box, nullptr);
    int green_tiles = 0;
    for (const Hit& h : images) {
        if (h.path.find("green4.png") == std::string::npos) {
            continue;
        }
        EXPECT_NEAR(h.w, 10.f, 0.5f);
        EXPECT_NEAR(h.h, 10.f, 0.5f);
        ++green_tiles;
    }
    EXPECT_GE(green_tiles, 16) << "40x40 with 10px tiles should emit at least 4x4 DrawImage items";

    zui::layout::Box* corner_box = find_box("corner");
    ASSERT_NE(corner_box, nullptr);
    bool saw_corner = false;
    const float expect_x = corner_box->geo.world_x + corner_box->geo.width - 16.f;
    const float expect_y = corner_box->geo.world_y + corner_box->geo.height - 16.f;
    for (const Hit& h : images) {
        if (h.path.find("red8.png") == std::string::npos) {
            continue;
        }
        if (std::fabs(h.w - 16.f) < 0.5f && std::fabs(h.h - 16.f) < 0.5f &&
            std::fabs(h.x - expect_x) < 1.f && std::fabs(h.y - expect_y) < 1.f) {
            saw_corner = true;
            break;
        }
    }
    EXPECT_TRUE(saw_corner) << "corner box should place a 16x16 tile at right bottom";

    // cover on 50x30 with square intrinsic → scale to fill 50x50, centered vertically.
    zui::layout::Box* cover_box = find_box("cover");
    ASSERT_NE(cover_box, nullptr);
    bool saw_cover = false;
    for (const Hit& h : images) {
        if (h.path.find("red8.png") == std::string::npos) {
            continue;
        }
        if (std::fabs(h.w - 50.f) < 1.f && std::fabs(h.h - 50.f) < 1.f) {
            EXPECT_NEAR(h.x, cover_box->geo.world_x, 1.f);
            EXPECT_NEAR(h.y, cover_box->geo.world_y + (30.f - 50.f) * 0.5f, 1.f);
            saw_cover = true;
            break;
        }
    }
    EXPECT_TRUE(saw_cover) << "cover should emit a 50x50 image centered on the 50x30 box";

    // Rounded clip: a PushClip with radius should wrap the stretch-like image for #clip.
    bool saw_round_clip = false;
    for (const auto& it : list.items()) {
        if (it.op == zui::paint::DisplayOp::PushClip && it.radii.any() &&
            it.radii.max_component() > 8.f) {
            saw_round_clip = true;
            break;
        }
    }
    EXPECT_TRUE(saw_round_clip) << "rounded background image should push a radiused clip";
}

// CSS Backgrounds 3 §2.1 / §3.11: stacked images paint bottom-first, each layer keeps its own
// clip and position, `fixed` is viewport-relative, and `local` moves with the element's scroll.
TEST(DifftestSelfCheck, BackgroundLayersAndAttachment) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "bg-layers.html")) << error;

    auto prop = [](zui::html::Node* n, const char* key) -> std::string {
        if (n == nullptr || n->computed == nullptr) {
            return {};
        }
        const auto it = n->computed->find(key);
        return it == n->computed->end() ? std::string() : std::string(it->second);
    };
    zui::html::Node* shorthand = zui::html::find(scene.doc, "shorthand");
    ASSERT_NE(shorthand, nullptr);
    EXPECT_NE(prop(shorthand, "background-image").find(", "), std::string::npos);
    EXPECT_EQ(prop(shorthand, "background-color"), "#101820");

    auto paint = [&](zui::paint::DisplayList& list) {
        zui::paint::Painter painter;
        painter.set_retained(false);
        painter.paint(*scene.layout->root(), list, 800, 600);
    };
    auto find_box = [&](const char* id) -> zui::layout::Box* {
        zui::layout::Box* b = scene.box(id);
        EXPECT_NE(b, nullptr) << id;
        return b;
    };
    auto is_rgb = [](const zui::paint::Gradient& g, int r, int gch, int b) {
        return !g.stops.empty() && g.stops.front().color.r == r && g.stops.front().color.g == gch &&
               g.stops.front().color.b == b;
    };
    auto find_grad = [&](const zui::paint::DisplayList& list, int r, int gch, int b, float x,
                         float y, float w, float h) {
        for (const auto& it : list.items()) {
            if (it.op != zui::paint::DisplayOp::FillGradient || !is_rgb(it.gradient, r, gch, b)) {
                continue;
            }
            if (std::fabs(it.rect.x - x) < 0.6f && std::fabs(it.rect.y - y) < 0.6f &&
                std::fabs(it.rect.w - w) < 0.6f && std::fabs(it.rect.h - h) < 0.6f) {
                return true;
            }
        }
        return false;
    };

    zui::layout::Box* stack = find_box("stack");
    ASSERT_NE(stack, nullptr);
    zui::paint::DisplayList list;
    paint(list);
    EXPECT_TRUE(find_grad(list, 0, 255, 0, stack->geo.world_x + 20.f, stack->geo.world_y + 16.f,
                          80.f, 50.f))
        << "bottom layer is the second image";
    EXPECT_TRUE(find_grad(list, 255, 0, 0, stack->geo.world_x + 10.f, stack->geo.world_y + 12.f,
                          40.f, 30.f))
        << "top layer is the first image";

    zui::layout::Box* clips = find_box("clips");
    ASSERT_NE(clips, nullptr);
    // border 8 + padding 16. border-box 140x100 → content 92x52.
    EXPECT_TRUE(find_grad(list, 64, 255, 128, clips->geo.world_x, clips->geo.world_y, 140.f, 100.f));
    EXPECT_TRUE(find_grad(list, 255, 64, 64, clips->geo.world_x + 24.f, clips->geo.world_y + 24.f,
                          92.f, 52.f));

    zui::layout::Box* fixed = find_box("fixed");
    ASSERT_NE(fixed, nullptr);
    EXPECT_TRUE(find_grad(list, 255, 255, 255, 80.f, 36.f, 30.f, 24.f))
        << "fixed attachment positions against the viewport";
    EXPECT_NEAR(fixed->geo.world_x, 70.f, 0.6f);

    zui::layout::Box* local = find_box("local");
    zui::layout::Box* stuck = find_box("stuck");
    ASSERT_NE(local, nullptr);
    ASSERT_NE(stuck, nullptr);
    EXPECT_TRUE(find_grad(list, 255, 204, 0, local->geo.world_x + 8.f, local->geo.world_y + 8.f,
                          40.f, 40.f));
    EXPECT_TRUE(find_grad(list, 255, 204, 0, stuck->geo.world_x + 8.f, stuck->geo.world_y + 8.f,
                          40.f, 40.f));

    local->geo.scroll_y = 24.f;
    stuck->geo.scroll_y = 24.f;
    zui::paint::DisplayList scrolled;
    paint(scrolled);
    EXPECT_TRUE(find_grad(scrolled, 255, 204, 0, stuck->geo.world_x + 8.f, stuck->geo.world_y + 8.f,
                          40.f, 40.f))
        << "scroll attachment stays with the padding box";
    EXPECT_TRUE(find_grad(scrolled, 255, 204, 0, local->geo.world_x + 8.f, local->geo.world_y, 40.f,
                          24.f))
        << "local attachment moves with the scroll and the clip keeps the visible slice";
    EXPECT_FALSE(find_grad(scrolled, 255, 204, 0, local->geo.world_x + 8.f, local->geo.world_y + 8.f,
                           40.f, 40.f));
}

// backdrop-filter: blur() is a display op on the border box. The Gaussian itself is the
// browser comparison paint-backdrop; this locks that the op is emitted with the CSS stddev.
TEST(DifftestSelfCheck, BackdropFilterBlur) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "backdrop.html")) << error;

    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.set_retained(false);
    painter.paint(*scene.layout->root(), list, 800, 600);

    int n = 0;
    bool saw_backdrop = false;
    bool white_after = false;
    for (const zui::paint::DisplayItem& it : list.items()) {
        if (it.op == zui::paint::DisplayOp::BackdropFilter) {
            ++n;
            saw_backdrop = true;
            EXPECT_FLOAT_EQ(it.blur, 8.f);
            EXPECT_GT(it.rect.w, 1.f);
            EXPECT_GT(it.rect.h, 1.f);
            continue;
        }
        if (saw_backdrop && it.op == zui::paint::DisplayOp::FillRect && it.color.a > 40 &&
            it.color.a < 120 && it.color.r > 240) {
            white_after = true;
        }
    }
    EXPECT_EQ(n, 2);
    EXPECT_TRUE(white_after) << "translucent background must paint after the backdrop sample";
}
