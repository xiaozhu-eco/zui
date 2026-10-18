// Scroll snap/anchoring, overscroll, and layout transitions.
// L6 architecture self-checks (TESTPLAN section 12).
// Shared scene harness: selfcheck_scene.h.

#include "selfcheck_scene.h"

using namespace difftest_selfcheck;

// B2: scroll-snap-type:y mandatory + scroll-snap-align:start snaps programmatic scroll.
TEST(DifftestSelfCheck, ScrollSnapMandatory) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "scroll-snap.html")) << error;

    zui::html::Node* pane = zui::html::find(scene.doc, "mandatory");
    ASSERT_NE(pane, nullptr);
    zui::layout::Box* box = scene.box("mandatory");
    ASSERT_NE(box, nullptr);

    zui::input::set_scroll(*box, 0.f, 60.f);
    EXPECT_NEAR(pane->scroll_y, 100.f, 1.f)
        << "mandatory snap from y=60 must land on the nearest start align (100)";
    zui::input::set_scroll(*box, 0.f, 20.f);
    EXPECT_NEAR(pane->scroll_y, 0.f, 1.f) << "mandatory snap from y=20 must land on start align 0";
}

// B2: scroll anchoring keeps the selected anchor's viewport position when content grows above it.
TEST(DifftestSelfCheck, ScrollAnchoringInsertAbove) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "scroll-anchor.html")) << error;

    zui::html::Node* scroller = zui::html::find(scene.doc, "scroller");
    zui::html::Node* anchor = zui::html::find(scene.doc, "anchor");
    ASSERT_NE(scroller, nullptr);
    ASSERT_NE(anchor, nullptr);
    zui::layout::Box* box = scene.box("scroller");
    ASSERT_NE(box, nullptr);

    // Scroll far enough that #top (120px) is fully clipped and #anchor is selected.
    zui::input::set_scroll(*box, 0.f, 120.f);
    EXPECT_NEAR(scroller->scroll_y, 120.f, 0.5f);
    ASSERT_EQ(scroller->scroll_anchor, anchor) << "selected anchor must be #anchor";
    const float anchor_y0 = scroller->scroll_anchor_y;

    // Insert a 40px spacer above the anchor (id matches selfcheck CSS).
    auto spacer = std::make_unique<zui::html::Node>();
    spacer->tag = "div";
    spacer->id = "spacer";
    spacer->parent = scroller;
    zui::html::Node* raw = spacer.get();
    auto& kids = scroller->children;
    auto at =
        std::find_if(kids.begin(), kids.end(), [&](const std::unique_ptr<zui::html::Node>& kid) {
            return kid.get() == anchor;
        });
    ASSERT_NE(at, kids.end());
    kids.insert(at, std::move(spacer));
    scene.doc.ids["spacer"] = raw;
    scene.app_frame({scroller});

    EXPECT_NEAR(scroller->scroll_y, 160.f, 1.f)
        << "scroll offset must absorb the 40px insertion above the anchor";
    EXPECT_NEAR(scroller->scroll_anchor_y, anchor_y0 + 40.f, 1.f);

    // overflow-anchor: none on the scroller opts out.
    zui::html::Node* optout = zui::html::find(scene.doc, "optout");
    zui::html::Node* oanchor = zui::html::find(scene.doc, "oanchor");
    ASSERT_NE(optout, nullptr);
    ASSERT_NE(oanchor, nullptr);
    zui::layout::Box* obox = scene.box("optout");
    ASSERT_NE(obox, nullptr);
    zui::input::set_scroll(*obox, 0.f, 120.f);
    EXPECT_EQ(optout->scroll_anchor, nullptr) << "overflow-anchor:none must not select an anchor";
    const float before = optout->scroll_y;
    auto ospacer = std::make_unique<zui::html::Node>();
    ospacer->tag = "div";
    ospacer->id = "ospacer";
    ospacer->parent = optout;
    zui::html::Node* oraw = ospacer.get();
    auto& okids = optout->children;
    auto oat =
        std::find_if(okids.begin(), okids.end(), [&](const std::unique_ptr<zui::html::Node>& kid) {
            return kid.get() == oanchor;
        });
    ASSERT_NE(oat, okids.end());
    okids.insert(oat, std::move(ospacer));
    scene.doc.ids["ospacer"] = oraw;
    scene.app_frame({optout});
    EXPECT_NEAR(optout->scroll_y, before, 0.5f)
        << "overflow-anchor:none must leave scrollTop unchanged after insert";
}

// B1: % width and height:auto (interpolate-size) length overlays feed layout mid-transition.
TEST(DifftestSelfCheck, TransitionPercentAutoLayout) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "transition-percent.html")) << error;

    zui::layout::Box* pct0 = scene.box("pct");
    ASSERT_NE(pct0, nullptr);
    EXPECT_NEAR(pct0->geo.content_width, 40.f, 0.5f);

    ASSERT_TRUE(zui::html::add_class(scene.doc, "pct", "on"));
    ASSERT_TRUE(zui::html::add_class(scene.doc, "open", "on"));
    scene.frame(0);
    scene.frame(100);

    zui::html::Node* pct_n = zui::html::find(scene.doc, "pct");
    zui::html::Node* open_n = zui::html::find(scene.doc, "open");
    ASSERT_NE(pct_n, nullptr);
    ASSERT_NE(open_n, nullptr);
    ASSERT_TRUE(pct_n->used.flags & zui::html::VisualUsed::kWidth);
    // 40 → 100 (50% of 200); mid = 70
    EXPECT_NEAR(pct_n->used.width, 70.f, 1.f);
    zui::layout::Box* pct1 = scene.box("pct");
    ASSERT_NE(pct1, nullptr);
    EXPECT_NEAR(pct1->geo.content_width, 70.f, 1.5f);

    ASSERT_TRUE(open_n->used.flags & zui::html::VisualUsed::kHeight)
        << "height overlay missing; used.height=" << open_n->used.height;
    EXPECT_GT(open_n->used.height, 0.5f) << "auto height must resolve via interpolate-size";
}

// B2: overscroll-behavior:contain stops unused wheel delta from chaining to an ancestor.
TEST(DifftestSelfCheck, OverscrollBehaviorContain) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "overscroll-contain.html")) << error;

    zui::html::Node* outer = zui::html::find(scene.doc, "outer");
    zui::html::Node* inner = zui::html::find(scene.doc, "inner");
    ASSERT_NE(outer, nullptr);
    ASSERT_NE(inner, nullptr);

    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::InteractionHost host;
    host.document = &scene.doc;
    host.layout = scene.layout.get();
    host.metrics = &scene.metrics;
    host.hovered = &hovered;
    host.focused = &focused;
    host.scroll_drag = &scroll_drag;
    host.dispatch = [](zui::Event&) {};
    zui::input::Interaction interaction(host);

    zui::layout::Box* inner_box = scene.box("inner");
    ASSERT_NE(inner_box, nullptr);
    const float hit_x = inner_box->geo.world_x + inner_box->geo.width * 0.5f;
    const float hit_y = inner_box->geo.world_y + inner_box->geo.height * 0.5f;

    outer->scroll_y = 0.f;
    inner->scroll_y = 0.f;
    EXPECT_FALSE(interaction.wheel(hit_x, hit_y, 0, 80.f, 0, zui::Wheel{}));
    EXPECT_NEAR(outer->scroll_y, 0.f, 0.5f) << "contain must block chain to #outer";
}

// CSS Transitions length overlays feed layout: mid-frame width/height/padding match sample().
TEST(DifftestSelfCheck, TransitionLengthLayout) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "transition-length.html")) << error;

    zui::layout::Box* box0 = scene.box("box");
    zui::layout::Box* pad0 = scene.box("pad");
    ASSERT_NE(box0, nullptr);
    ASSERT_NE(pad0, nullptr);
    EXPECT_NEAR(box0->geo.content_width, 40.f, 0.5f);
    EXPECT_NEAR(box0->geo.content_height, 20.f, 0.5f);
    EXPECT_NEAR(pad0->geo.padding.left, 4.f, 0.5f);

    ASSERT_TRUE(zui::html::add_class(scene.doc, "box", "on"));
    ASSERT_TRUE(zui::html::add_class(scene.doc, "pad", "on"));
    scene.frame(0);    // start transitions at t=0 (overlay = from)
    scene.frame(100);  // halfway through 200ms

    zui::html::Node* box_n = zui::html::find(scene.doc, "box");
    zui::html::Node* pad_n = zui::html::find(scene.doc, "pad");
    ASSERT_NE(box_n, nullptr);
    ASSERT_NE(pad_n, nullptr);
    ASSERT_TRUE(box_n->used.flags & zui::html::VisualUsed::kWidth);
    EXPECT_NEAR(box_n->used.width, 60.f, 0.6f);
    EXPECT_NEAR(box_n->used.height, 40.f, 0.6f);
    EXPECT_NEAR(pad_n->used.padding_left, 14.f, 0.6f);

    zui::layout::Box* box1 = scene.box("box");
    zui::layout::Box* pad1 = scene.box("pad");
    ASSERT_NE(box1, nullptr);
    ASSERT_NE(pad1, nullptr);
    EXPECT_NEAR(box1->geo.content_width, 60.f, 0.8f)
        << "layout must consume width overlay, got " << box1->geo.content_width;
    EXPECT_NEAR(box1->geo.content_height, 40.f, 0.8f)
        << "layout must consume height overlay, got " << box1->geo.content_height;
    EXPECT_NEAR(pad1->geo.padding.left, 14.f, 0.8f) << "layout must consume padding overlay";
}
