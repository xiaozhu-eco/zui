#include <gtest/gtest.h>

#include "css/engine/engine.h"
#include "html/document.h"
#include "layout/engine.h"

#include <cmath>

namespace {

zui::layout::LayoutEngine* g_layout = nullptr;
zui::html::Document* g_doc = nullptr;

const zui::layout::Box* box(const char* id) {
    return g_layout->principal_box(zui::html::find(*g_doc, id));
}

}  // namespace

class LayoutGeometry : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        doc_ = std::make_unique<zui::html::Document>();
        std::string error;
        if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/geometry_math.html", *doc_,
                                  error)) {
            FAIL() << error;
        }
        css_ = std::make_unique<zui::css::Engine>();
        if (!css_->load_sheets(*doc_, error)) {
            FAIL() << error;
        }
        css_->cascade(*doc_);
        metrics_ = std::make_unique<zui::layout::FreeTypeTextMetrics>();
        if (!metrics_->ready()) {
            FAIL() << "no font";
        }
        layout_ = std::make_unique<zui::layout::LayoutEngine>(*metrics_);
        // Tall enough that paint-map culling does not skip boxes below the window.
        // A culled box keeps a layout result and publishes no border box.
        layout_->layout(*doc_, zui::layout::Viewport{800.0f, 8000.0f});
        g_layout = layout_.get();
        g_doc = doc_.get();
    }

    static void TearDownTestSuite() {
        g_layout = nullptr;
        g_doc = nullptr;
        layout_.reset();
        metrics_.reset();
        css_.reset();
        doc_.reset();
    }

    static std::unique_ptr<zui::html::Document> doc_;
    static std::unique_ptr<zui::css::Engine> css_;
    static std::unique_ptr<zui::layout::FreeTypeTextMetrics> metrics_;
    static std::unique_ptr<zui::layout::LayoutEngine> layout_;
};

std::unique_ptr<zui::html::Document> LayoutGeometry::doc_;
std::unique_ptr<zui::css::Engine> LayoutGeometry::css_;
std::unique_ptr<zui::layout::FreeTypeTextMetrics> LayoutGeometry::metrics_;
std::unique_ptr<zui::layout::LayoutEngine> LayoutGeometry::layout_;

// CSS 2.1 §10.4: max is applied, then min. min-width: 300px wins over max-width: 200px.
TEST_F(LayoutGeometry, MinWidthWinsOverMaxWidth) {
    const zui::layout::Box* b = box("wconflict");
    ASSERT_NE(b, nullptr);
    EXPECT_NEAR(b->geo.width, 300.f, 0.05f);
}

// Same rule on the block axis for an auto height: min-height wins when it exceeds max-height.
TEST_F(LayoutGeometry, MinHeightWinsOverMaxHeight) {
    const zui::layout::Box* b = box("hconflict");
    ASSERT_NE(b, nullptr);
    EXPECT_NEAR(b->geo.height, 300.f, 0.05f);
}

// A specified height is still clamped. min-height raises 50px to 80px.
TEST_F(LayoutGeometry, SpecifiedHeightHonorsMinHeight) {
    const zui::layout::Box* b = box("hmin");
    ASSERT_NE(b, nullptr);
    EXPECT_NEAR(b->geo.height, 80.f, 0.05f);
}

// max-height cuts a specified 200px height down to 80px, and a child percentage
// resolves against that clamped content height.
TEST_F(LayoutGeometry, SpecifiedHeightHonorsMaxHeight) {
    const zui::layout::Box* b = box("hmax");
    const zui::layout::Box* child = box("hmaxchild");
    ASSERT_NE(b, nullptr);
    ASSERT_NE(child, nullptr);
    EXPECT_NEAR(b->geo.height, 80.f, 0.05f);
    EXPECT_NEAR(child->geo.height, 80.f, 0.05f);
}

// CSS Values: clamp(MIN, VAL, MAX) is max(MIN, min(VAL, MAX)). The minimum wins when inverted.
TEST_F(LayoutGeometry, ClampMinimumWinsWhenInverted) {
    const zui::layout::Box* b = box("clampw");
    ASSERT_NE(b, nullptr);
    EXPECT_NEAR(b->geo.width, 300.f, 0.05f);
}

// Flex hypothetical main size uses the same min-wins clamp (CSS Sizing §3 / Flexbox §9.2).
TEST_F(LayoutGeometry, FlexItemMinWinsOverMax) {
    const zui::layout::Box* b = box("flexmm");
    ASSERT_NE(b, nullptr);
    EXPECT_NEAR(b->geo.width, 300.f, 0.05f);
}

// A growing item is clamped the same way: free space cannot pull it under the minimum.
TEST_F(LayoutGeometry, FlexGrowMinWinsOverMax) {
    const zui::layout::Box* b = box("flexgrow");
    ASSERT_NE(b, nullptr);
    EXPECT_NEAR(b->geo.width, 300.f, 0.05f);
}

// Grid item contribution uses the same clamp, so the track and the item are the minimum.
TEST_F(LayoutGeometry, GridItemMinWinsOverMax) {
    const zui::layout::Box* b = box("griditem");
    ASSERT_NE(b, nullptr);
    EXPECT_NEAR(b->geo.width, 300.f, 0.05f);
}

// CSS 2.1 §10.3.3, direction:rtl: the used margin-left absorbs the over-constraint,
// so a 50px box in a 200px content box starts at x=150.
TEST_F(LayoutGeometry, RtlBlockSitsOnInlineStart) {
    const zui::layout::Box* b = box("rtlchild");
    ASSERT_NE(b, nullptr);
    EXPECT_NEAR(b->geo.width, 50.f, 0.05f);
    EXPECT_NEAR(b->geo.x, 150.f, 0.05f);
}

// Horizontal-tb placement is physical x. padding-left is the inline-end under rtl.
TEST_F(LayoutGeometry, RtlPaddingUsesPhysicalLeft) {
    const zui::layout::Box* host = box("rtlpad");
    const zui::layout::Box* b = box("rtlpadchild");
    ASSERT_NE(host, nullptr);
    ASSERT_NE(b, nullptr);
    // content-box: the border box is the 200px content width plus 10px and 40px of padding.
    EXPECT_NEAR(host->geo.width, 250.f, 0.05f);
    EXPECT_NEAR(b->geo.width, 200.f, 0.05f);
    EXPECT_NEAR(b->geo.x, 10.f, 0.05f);
}

// `left` is physical. Under direction:rtl a relative left:30px moves the box right.
TEST_F(LayoutGeometry, RtlRelativeLeftMovesRight) {
    const zui::layout::Box* b = box("rtlrelchild");
    ASSERT_NE(b, nullptr);
    EXPECT_NEAR(b->geo.width, 200.f, 0.05f);
    EXPECT_NEAR(b->geo.x, 30.f, 0.05f);
}

// CSS Transforms: the function list applies right to left. rotate(90deg) about the
// origin leaves (0,0) put, then translateX(100px) moves that origin to (100, 0).
TEST_F(LayoutGeometry, TransformListAppliesRightToLeft) {
    const zui::layout::Box* b = box("xform");
    ASSERT_NE(b, nullptr);
    EXPECT_NEAR(b->geo.world_x, 100.f, 0.05f);
    EXPECT_NEAR(b->geo.world_y, 0.f, 0.05f);
}

// Padding percentages resolve against the containing block's inline size (CSS Box §4).
TEST_F(LayoutGeometry, PercentPaddingUsesInlineSize) {
    const zui::layout::Box* b = box("pad");
    ASSERT_NE(b, nullptr);
    EXPECT_NEAR(b->geo.padding.top, 20.f, 0.05f);
}

// calc(100% - 40px) against a 200px containing block is 160px.
TEST_F(LayoutGeometry, CalcPercentMinusLength) {
    const zui::layout::Box* host = box("calchost");
    const zui::layout::Box* b = box("calc");
    ASSERT_NE(host, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_NEAR(host->geo.width, 200.f, 0.05f);
    EXPECT_NEAR(b->geo.width, 160.f, 0.05f);
    EXPECT_NEAR(b->geo.height, 10.f, 0.05f);
}

// margin-left/right: auto split the free space of a definite width.
TEST_F(LayoutGeometry, AutoMarginsCenterBlock) {
    const zui::layout::Box* b = box("center");
    ASSERT_NE(b, nullptr);
    EXPECT_NEAR(b->geo.width, 100.f, 0.05f);
    EXPECT_NEAR(b->geo.x, 100.f, 0.05f);
}

// A single auto main margin takes the flex line's free space (Flexbox §9.5).
TEST_F(LayoutGeometry, FlexAutoMarginTakesFreeSpace) {
    const zui::layout::Box* b = box("flexitem");
    ASSERT_NE(b, nullptr);
    EXPECT_NEAR(b->geo.width, 100.f, 0.05f);
    EXPECT_NEAR(b->geo.x, 200.f, 0.05f);
}

// Two equal fr tracks share the space left after the fixed track.
TEST_F(LayoutGeometry, GridFrSharesFreeSpace) {
    const zui::layout::Box* g1 = box("g1");
    const zui::layout::Box* g2 = box("g2");
    ASSERT_NE(g1, nullptr);
    ASSERT_NE(g2, nullptr);
    EXPECT_NEAR(g1->geo.width, 100.f, 0.05f);
    EXPECT_NEAR(g2->geo.width, 100.f, 0.05f);
    EXPECT_NEAR(g1->geo.x, 100.f, 0.05f);
    EXPECT_NEAR(g2->geo.x, 200.f, 0.05f);
}

// A percentage height resolves against a definite containing-block height.
TEST_F(LayoutGeometry, PercentHeightUsesDefiniteBlock) {
    const zui::layout::Box* b = box("hpct");
    ASSERT_NE(b, nullptr);
    EXPECT_NEAR(b->geo.height, 100.f, 0.05f);
}

// The flex container's used size is clamped before items share free space.
// min-width wins over max-width, so a flex: 1 0 0px item fills 300px, not 200px.
TEST_F(LayoutGeometry, FlexContainerMinWinsBeforeDistribute) {
    const zui::layout::Box* b = box("flexclampitem");
    ASSERT_NE(b, nullptr);
    EXPECT_NEAR(b->geo.width, 300.f, 0.05f);
}

// The same clamp on the block axis. A column distributes in the unclamped height
// when only the inline size is clamped, so the item stays 250 instead of 300.
TEST_F(LayoutGeometry, FlexColumnMinWinsBeforeDistribute) {
    const zui::layout::Box* b = box("flexcolitem");
    ASSERT_NE(b, nullptr);
    EXPECT_NEAR(b->geo.height, 300.f, 0.05f);
}

// A shrink pass that hits max-width still floors at min-width when min is larger.
// The item's own layout already reports 300; the sibling shows the free space the
// frozen target actually consumed. Flooring only at the end leaves the sibling at 200.
TEST_F(LayoutGeometry, FlexShrinkMinWinsOverMax) {
    const zui::layout::Box* item = box("flexshrink");
    const zui::layout::Box* sibling = box("flexsib");
    ASSERT_NE(item, nullptr);
    ASSERT_NE(sibling, nullptr);
    EXPECT_NEAR(item->geo.width, 300.f, 0.05f);
    EXPECT_NEAR(sibling->geo.width, 100.f, 0.05f);
    EXPECT_NEAR(sibling->geo.x, 300.f, 0.05f);
}

// A horizontal RTL float's border edge adds the physical left margin.
// margin-right is the logical inline-start and must not move the border box.
TEST_F(LayoutGeometry, RtlFloatUsesPhysicalLeftMargin) {
    const zui::layout::Box* b = box("rtlfloatchild");
    ASSERT_NE(b, nullptr);
    EXPECT_NEAR(b->geo.width, 40.f, 0.05f);
    EXPECT_NEAR(b->geo.x, 0.f, 0.05f);
}

// CSS 2.1 §9.4.3: when both left and right are set, rtl ignores left and shifts by -right.
TEST_F(LayoutGeometry, RtlRelativeBothInsetsUseRight) {
    const zui::layout::Box* b = box("rtlbothchild");
    ASSERT_NE(b, nullptr);
    EXPECT_NEAR(b->geo.x, -10.f, 0.05f);
}

// vertical-align: sub lowers by floor((font-size / 5 + 1) * 64) / 64.
// At 16px that is 4.1875, not 0.2em (3.2).
TEST_F(LayoutGeometry, VerticalAlignSubFloorsOntoTheQuantum) {
    const zui::layout::Box* base = box("valignbase");
    const zui::layout::Box* sub = box("valignsub");
    ASSERT_NE(base, nullptr);
    ASSERT_NE(sub, nullptr);
    EXPECT_NEAR(sub->geo.y - base->geo.y, 4.1875f, 0.01f);
}
