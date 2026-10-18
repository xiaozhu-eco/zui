// Transform functions, Aff2 display-list records, and group boundaries.
// L6 architecture self-checks (TESTPLAN section 12).
// Shared scene harness: selfcheck_scene.h.

#include "selfcheck_scene.h"

using namespace difftest_selfcheck;

// B1: transform function subset — matrix / matrix3d(2D) / skew* / perspective no-op / origin 3-value.
TEST(DifftestSelfCheck, TransformFunctionsMatrixSkew) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "transform-functions.html")) << error;
    scene.layout->sync_world();

    zui::layout::Box* matrix = scene.box("matrix");
    zui::layout::Box* matrix3d = scene.box("matrix3d");
    zui::layout::Box* skewx = scene.box("skewx");
    zui::layout::Box* origin3 = scene.box("origin3");
    ASSERT_NE(matrix, nullptr);
    ASSERT_NE(matrix3d, nullptr);
    ASSERT_NE(skewx, nullptr);
    ASSERT_NE(origin3, nullptr);

    // Pure translate: AABB size equals layout size. matrix vs matrix3d sit on different rows.
    EXPECT_NEAR(matrix->geo.aabb_w, matrix->geo.width, 0.5f);
    EXPECT_NEAR(matrix->geo.aabb_h, matrix->geo.height, 0.5f);
    EXPECT_NEAR(matrix3d->geo.aabb_w, matrix3d->geo.width, 0.5f);
    EXPECT_NEAR(matrix3d->geo.aabb_h, matrix3d->geo.height, 0.5f);

    // skewX grows the AABB width beyond the layout width.
    EXPECT_GT(skewx->geo.aabb_w, skewx->geo.width + 5.f);
    EXPECT_TRUE(skewx->paint_transformed);

    // rotate(90deg) about left top: AABB swaps roughly to height x width.
    EXPECT_TRUE(origin3->paint_transformed);
    EXPECT_NEAR(origin3->geo.aabb_w, origin3->geo.height, 1.f);
    EXPECT_NEAR(origin3->geo.aabb_h, origin3->geo.width, 1.f);
}

// B3/A6: sharp Aff2 FillRect + push_clip_xform; establishing boxes use BeginTransform.
// Counts come from the display list the painter recorded. Pixel samples of that list were
// the software raster and are gone.
TEST(DifftestSelfCheck, TransformAff2FillRect) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "transform-aff2-fill.html")) << error;
    scene.layout->sync_world();

    zui::layout::Box* rot = scene.box("rot");
    zui::layout::Box* parent = scene.box("parent");
    zui::layout::Box* child = scene.box("child");
    zui::layout::Box* clip = scene.box("clip");
    ASSERT_NE(rot, nullptr);
    ASSERT_NE(parent, nullptr);
    ASSERT_NE(child, nullptr);
    ASSERT_NE(clip, nullptr);
    EXPECT_TRUE(rot->paint_transformed);
    EXPECT_TRUE(parent->paint_transformed);
    EXPECT_TRUE(child->paint_transformed) << "child under rotated parent must compose Aff2";
    EXPECT_TRUE(clip->paint_transformed);
    EXPECT_FALSE(rot->geo.culled) << "rot culled unexpectedly";
    EXPECT_FALSE(parent->geo.culled) << "parent culled unexpectedly";

    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.set_retained(false);
    painter.paint(*scene.layout->root(), list, 800, 600);

    int fills = 0;
    int xform_fills = 0;
    int begin_xforms = 0;
    int xform_clips = 0;
    for (const auto& it : list.items()) {
        if (it.op == zui::paint::DisplayOp::FillRect) {
            ++fills;
            if (it.has_xform && !it.radii.any()) {
                ++xform_fills;
            }
        }
        if (it.op == zui::paint::DisplayOp::BeginTransform) {
            ++begin_xforms;
        }
        if (it.op == zui::paint::DisplayOp::PushClip && it.has_xform) {
            ++xform_clips;
        }
    }
    EXPECT_GE(fills, 3) << "expected background fills; items=" << list.items().size();
    EXPECT_GE(begin_xforms + xform_fills, 3)
        << "BeginTransform and/or fill_rect_xform; begin=" << begin_xforms
        << " xform_fills=" << xform_fills;
    EXPECT_GE(begin_xforms, 2) << "establishing transforms emit BeginTransform";
    EXPECT_GE(xform_clips, 1) << "overflow:hidden under transform emits push_clip_xform";
}

// B3/A6: rounded Aff2 FillRect — establishing boxes use BeginTransform; nested inherited
// Aff2 keeps fill_rect_xform.
TEST(DifftestSelfCheck, TransformAff2RoundedFill) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "transform-aff2-radius.html")) << error;
    scene.layout->sync_world();

    zui::layout::Box* rot = scene.box("rot");
    zui::layout::Box* parent = scene.box("parent");
    zui::layout::Box* child = scene.box("child");
    zui::layout::Box* clip = scene.box("clip");
    ASSERT_NE(rot, nullptr);
    ASSERT_NE(parent, nullptr);
    ASSERT_NE(child, nullptr);
    ASSERT_NE(clip, nullptr);
    EXPECT_TRUE(rot->paint_transformed);
    EXPECT_TRUE(child->paint_transformed);

    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.set_retained(false);
    painter.paint(*scene.layout->root(), list, 800, 600);

    int rounded_xform_fills = 0;
    int begin_xforms = 0;
    int xform_clips = 0;
    for (const auto& it : list.items()) {
        if (it.op == zui::paint::DisplayOp::FillRect && it.has_xform && it.radii.any()) {
            ++rounded_xform_fills;
        }
        if (it.op == zui::paint::DisplayOp::BeginTransform) {
            ++begin_xforms;
        }
        if (it.op == zui::paint::DisplayOp::PushClip && it.has_xform) {
            ++xform_clips;
        }
    }
    EXPECT_GE(begin_xforms + rounded_xform_fills, 3)
        << "establishing BeginTransform and/or nested fill_rect_xform; begin=" << begin_xforms
        << " xform_fills=" << rounded_xform_fills;
    EXPECT_GE(begin_xforms, 2) << "rot/parent/clip establish own transform → BeginTransform";
    EXPECT_GE(xform_clips, 1) << "overflow:hidden under transform emits push_clip_xform";
}

// B3/A6: Aff2 DrawText + DrawImage. The painter records has_xform; the GPU draws the quad.
TEST(DifftestSelfCheck, TransformAff2TextImage) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "transform-aff2-text.html")) << error;
    scene.layout->sync_world();

    zui::layout::Box* rot = scene.box("rot");
    zui::layout::Box* parent = scene.box("parent");
    zui::layout::Box* child = scene.box("child");
    zui::layout::Box* img = scene.box("img");
    ASSERT_NE(rot, nullptr);
    ASSERT_NE(parent, nullptr);
    ASSERT_NE(child, nullptr);
    ASSERT_NE(img, nullptr);
    EXPECT_TRUE(rot->paint_transformed);
    EXPECT_TRUE(img->paint_transformed);

    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.set_retained(false);
    painter.paint(*scene.layout->root(), list, 800, 600);

    int xform_texts = 0;
    int xform_images = 0;
    for (const auto& it : list.items()) {
        if (it.op == zui::paint::DisplayOp::DrawText && it.has_xform) {
            ++xform_texts;
        }
        if (it.op == zui::paint::DisplayOp::DrawImage && it.has_xform) {
            ++xform_images;
        }
    }
    EXPECT_GE(xform_texts, 1) << "rotated/nested text should emit DrawText with Aff2";
    EXPECT_GE(xform_images, 1) << "rotated image should emit DrawImage with Aff2";
}

// A 0x0 FillRect is outside every clip. Begin/EndFilter carry no area of their own, so the same
// clip must keep the pair or a later GPU primitive stays inside the group's opacity.
TEST(DifftestSelfCheck, AppendShiftedKeepsGroupBoundaries) {
    zui::paint::DisplayList src;
    zui::paint::DisplayItem begin;
    begin.op = zui::paint::DisplayOp::BeginFilter;
    begin.filter.opacity = 0.8f;
    begin.rect = {};
    zui::paint::DisplayItem fill;
    fill.op = zui::paint::DisplayOp::FillRect;
    fill.rect = zui::paint::Rect{4.f, 4.f, 0.f, 0.f};
    fill.color = zui::paint::Color{1, 2, 3, 255};
    zui::paint::DisplayItem end;
    end.op = zui::paint::DisplayOp::EndFilter;
    end.rect = {};
    // begin_filter() drops a 0x0 bounds. EndFilter is recorded at 0x0, which is the cull.
    auto& raw = const_cast<std::vector<zui::paint::DisplayItem>&>(src.items());
    raw.push_back(begin);
    raw.push_back(fill);
    raw.push_back(end);

    zui::paint::DisplayList out;
    out.append_shifted(src, 0.f, 0.f, zui::paint::Rect{0.f, 0.f, 32.f, 32.f}, true);

    ASSERT_EQ(out.items().size(), 2u);
    EXPECT_EQ(out.items()[0].op, zui::paint::DisplayOp::BeginFilter);
    EXPECT_FLOAT_EQ(out.items()[0].filter.opacity, 0.8f);
    EXPECT_EQ(out.items()[1].op, zui::paint::DisplayOp::EndFilter);
}
