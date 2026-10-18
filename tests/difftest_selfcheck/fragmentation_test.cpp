// Multicol fragmentation, columns, and shape-outside.
// L6 architecture self-checks (TESTPLAN section 12).
// Shared scene harness: selfcheck_scene.h.

#include "selfcheck_scene.h"

using namespace difftest_selfcheck;

// B2: solid column-rule fills in the middle of each column-gap.
TEST(DifftestSelfCheck, FragmentationBreakStyles) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "fragmentation-breaks.html")) << error;
    zui::layout::Box* sample = scene.box("sample");
    ASSERT_NE(sample, nullptr);
    EXPECT_EQ(sample->style().break_before, zui::layout::BreakBetween::Column);
    EXPECT_EQ(sample->style().break_inside, zui::layout::BreakInside::Avoid);
    EXPECT_EQ(sample->style().orphans, 3);
    EXPECT_EQ(sample->style().widows, 4);
}

TEST(DifftestSelfCheck, ColumnRuleSolid) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "column-rule.html")) << error;

    zui::layout::Box* rules = scene.box("rules");
    ASSERT_NE(rules, nullptr);
    EXPECT_EQ(rules->style().inner, zui::layout::DisplayInner::Multicol);

    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.set_retained(false);
    painter.paint(*scene.layout->root(), list, 800, 600);

    const float content_x = rules->geo.world_x + rules->geo.border.left + rules->geo.padding.left;
    const float content_w = rules->geo.content_width;
    const float gap = 24.f;
    const float col_w = (content_w - 2.f * gap) / 3.f;
    const float rule0_x = content_x + col_w + (gap - 4.f) * 0.5f;
    bool saw = false;
    for (const auto& it : list.items()) {
        if (it.op != zui::paint::DisplayOp::FillRect) {
            continue;
        }
        if (std::fabs(it.rect.w - 4.f) > 0.1f) {
            continue;
        }
        if (std::fabs(it.rect.x - rule0_x) > 1.f) {
            continue;
        }
        if (it.color.r > 200 && it.color.g > 180 && it.color.b < 80) {
            saw = true;
            break;
        }
    }
    EXPECT_TRUE(saw) << "expected a 4px gold column-rule fill near x=" << rule0_x;
}

// B2: shape-outside circle/ellipse/inset shortens float exclusion for a following BFC.
TEST(DifftestSelfCheck, ShapeOutsideBasicShapes) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "shape-outside.html")) << error;

    zui::layout::Box* rect = scene.box("probe-rect");
    zui::layout::Box* circle = scene.box("probe-circle");
    zui::layout::Box* ellipse = scene.box("probe-ellipse");
    zui::layout::Box* inset = scene.box("probe-inset");
    ASSERT_NE(rect, nullptr);
    ASSERT_NE(circle, nullptr);
    ASSERT_NE(ellipse, nullptr);
    ASSERT_NE(inset, nullptr);

    EXPECT_NEAR(rect->geo.world_x, 100.f, 0.5f) << "rectangular float keeps full margin exclusion";
    EXPECT_NEAR(rect->geo.width, 16.f, 0.5f);
    EXPECT_NEAR(circle->geo.world_x, 90.f, 0.5f) << "circle(50%) chord at line-height 20";
    EXPECT_NEAR(ellipse->geo.world_x, 82.f, 0.5f) << "ellipse(40% 50%) chord at line-height 20";
    EXPECT_NEAR(inset->geo.world_x, 60.f, 0.5f) << "inset(0 40px 0 0) leaves 60px of exclusion";
}

// B2: column-span:all breaks out of the column run at full content width.
TEST(DifftestSelfCheck, ColumnSpanAll) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "column-span.html")) << error;

    zui::layout::Box* span = scene.box("span");
    zui::layout::Box* a1 = scene.box("a1");
    zui::layout::Box* a2 = scene.box("a2");
    ASSERT_NE(span, nullptr);
    ASSERT_NE(a1, nullptr);
    ASSERT_NE(a2, nullptr);
    EXPECT_TRUE(span->style().column_span_all);
    EXPECT_GT(span->geo.width, 300.f) << "spanner must break out across columns";
    EXPECT_LT(a1->geo.width, 200.f)
        << "non-spanner stays in a column, not full multicol width; w=" << a1->geo.width
        << " span_w=" << span->geo.width << " cols=" << scene.box("cols")->style().column_count;
    EXPECT_GT(a2->geo.world_y, span->geo.world_y + span->geo.height - 1.f)
        << "post-spanner content must start a new column run below the spanner";
    EXPECT_LE(a1->geo.world_y, span->geo.world_y + 0.5f);
}

// A4: nested block containers keep the part that fits across fragmentainers (deeper than lines).
TEST(DifftestSelfCheck, MulticolNestedBlockFragmentation) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "multicol-block-nested.html")) << error;

    zui::layout::Box* a = scene.box("a");
    zui::layout::Box* b = scene.box("b");
    zui::layout::Box* c = scene.box("c");
    zui::layout::Box* d = scene.box("d");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_NE(c, nullptr);
    ASSERT_NE(d, nullptr);

    // Column 1 holds a+b; column 2 holds c+d (deeper nested child-boundary break).
    EXPECT_LT(a->geo.world_x, 100.f) << "a must stay in the first column";
    EXPECT_LT(b->geo.world_x, 100.f) << "b must stay in the first column";
    EXPECT_GT(c->geo.world_x, 150.f) << "c must resume in the second column";
    EXPECT_GT(d->geo.world_x, 150.f) << "d must resume in the second column";
    EXPECT_NEAR(a->geo.world_y, b->geo.world_y - 50.f, 2.f);
    EXPECT_NEAR(c->geo.world_y, d->geo.world_y - 50.f, 2.f);
}

// A4: a float taller than the column is fragmented across columns (empty trailing parent
// fragments + non-BFC scrollHeight includes the float band).
TEST(DifftestSelfCheck, MulticolFloatSplitAcrossColumns) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "multicol-float-split.html")) << error;

    zui::layout::Box* tall = scene.box("tall");
    zui::layout::Box* inner = scene.box("inner");
    zui::layout::Box* fl = scene.box("fl");
    ASSERT_NE(tall, nullptr);
    ASSERT_NE(inner, nullptr);
    ASSERT_NE(fl, nullptr);

    ASSERT_EQ(fl->geo.fragments.size(), 3u)
        << "float taller than the column must yield three client rects";
    EXPECT_NEAR(fl->geo.fragments[0].height, 100.f, 1.f);
    EXPECT_NEAR(fl->geo.fragments[1].height, 100.f, 1.f);
    EXPECT_NEAR(fl->geo.fragments[2].height, 40.f, 1.f);
    EXPECT_GT(fl->geo.fragments[1].world_x, fl->geo.fragments[0].world_x + 50.f);
    EXPECT_GT(fl->geo.fragments[2].world_x, fl->geo.fragments[1].world_x + 50.f);

    ASSERT_GE(inner->geo.fragments.size(), 3u)
        << "inner must open an empty trailing fragment for the float remainder";
    EXPECT_NEAR(inner->geo.fragments[2].height, 0.f, 0.5f);
    EXPECT_NEAR(inner->geo.scroll_height, 240.f, 1.f)
        << "non-BFC scrollHeight must include the float band";
    EXPECT_NEAR(tall->geo.scroll_width, 650.f, 1.f)
        << "overflow columns from the float must grow scrollWidth";
}
