// Stacking order, culling, damage, and retained listings.
// L6 architecture self-checks (TESTPLAN section 12).
// Shared scene harness: selfcheck_scene.h.

#include "selfcheck_scene.h"

#include <type_traits>

using namespace difftest_selfcheck;

// P1-6: the stacking predicate is LAYOUT.md section 11's list, one line per bool, and the order it
// produces is the one the painter and the hit tester share.
TEST(DifftestSelfCheck, StackingListAndPaintOrder) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "stacking.html")) << error;

    const auto info = [&scene](const char* id) {
        zui::layout::Box* box = scene.box(id);
        EXPECT_NE(box, nullptr) << id;
        return box == nullptr ? zui::layout::StackingInfo{} : zui::layout::stacking_info(*box);
    };
    // Root, negative and positive z-index, fixed / sticky, opacity, transform, filter,
    // mix-blend-mode, isolation, will-change, contain: layout / paint.
    EXPECT_FALSE(info("plain").context);
    EXPECT_FALSE(info("plain").positioned);
    EXPECT_TRUE(info("relative").positioned);
    EXPECT_FALSE(info("relative").context) << "z-index: auto builds neither context nor z-order";
    EXPECT_TRUE(info("relative").z_auto);
    EXPECT_TRUE(info("relative-z").context);
    EXPECT_EQ(info("relative-z").z_index, 3);
    EXPECT_TRUE(info("absolute-neg").context);
    EXPECT_EQ(info("absolute-neg").z_index, -2);
    EXPECT_TRUE(info("fixed").context) << "fixed is a context even with z-index: auto";
    EXPECT_TRUE(info("fixed").z_auto);
    EXPECT_TRUE(info("sticky").context);
    EXPECT_TRUE(info("faded").context);
    EXPECT_TRUE(info("transformed").context);
    EXPECT_TRUE(info("filtered").context);
    EXPECT_TRUE(info("blended").context);
    EXPECT_TRUE(info("isolated").context);
    EXPECT_TRUE(info("will-change").context);
    EXPECT_TRUE(info("contain-paint").context);
    EXPECT_TRUE(info("contain-layout").context);
    // `z-index` on a `position: static` box has no effect: no context, and the box stays in the
    // in-flow bucket of the order.
    EXPECT_FALSE(info("static-z").context);
    EXPECT_FALSE(info("static-z").positioned);
    EXPECT_FALSE(info("static-z").z_auto) << "the value is still read; it just does not apply";
    // A flex item with a non-auto `z-index` is a context even without `position`.
    EXPECT_TRUE(info("flex-z-pos").context);
    EXPECT_EQ(info("flex-z-pos").z_index, 2);
    EXPECT_TRUE(info("flex-z-neg").context);
    EXPECT_EQ(info("flex-z-neg").z_index, -1);
    EXPECT_EQ(info("order-first").order, 1);
    EXPECT_EQ(info("order-second").order, -1);

    // `order` decides the sequence inside one context: both boxes are in-flow, so the negative
    // order paints first even though the DOM lists it second.
    zui::layout::Box* order_host = scene.box("flex-order");
    ASSERT_NE(order_host, nullptr);
    ASSERT_TRUE(zui::layout::has_stacking_children(*order_host));
    std::vector<zui::layout::Box*> order;
    zui::layout::stacking_paint_order(*order_host, order);
    ASSERT_EQ(order.size(), std::size_t{2});
    EXPECT_EQ(order[0], scene.box("order-second"));
    EXPECT_EQ(order[1], scene.box("order-first"));

    // The z buckets: the negative context paints before the positive one, which reverses the DOM.
    zui::layout::Box* z_host = scene.box("flex-z");
    ASSERT_NE(z_host, nullptr);
    zui::layout::stacking_paint_order(*z_host, order);
    ASSERT_EQ(order.size(), std::size_t{2});
    EXPECT_EQ(order[0], scene.box("flex-z-neg"));
    EXPECT_EQ(order[1], scene.box("flex-z-pos"));

    // z-index:auto positioned mid does not trap a negative-z absolute: the ancestor context lifts
    // it ahead of in-flow siblings (appendix E step 6 / step 2).
    zui::layout::Box* auto_host = scene.box("auto-host");
    ASSERT_NE(auto_host, nullptr);
    ASSERT_TRUE(zui::layout::has_stacking_children(*auto_host));
    zui::layout::stacking_paint_order(*auto_host, order);
    ASSERT_GE(order.size(), std::size_t{3});
    EXPECT_EQ(order[0], scene.box("auto-lifted"));
    EXPECT_TRUE(
        zui::layout::stacking_skip_lifted_child(*scene.box("auto-mid"), *scene.box("auto-lifted")));

    // An out-of-flow child is a direct child of the box that collects it, so its ink belongs to
    // that box's ink rectangle (P1-5 B, LAYOUT.md section 4). `#abs-far` sits 900px down, far past
    // the body's own border box, so the rectangle cannot reach it by accident.
    zui::layout::Box* far = scene.box("abs-far");
    ASSERT_NE(far, nullptr);
    // The collecting box is the one whose padding box the child's offsets are measured in, which
    // is the initial containing block here - not the DOM parent it happens to hang under.
    zui::layout::Box* collector = far->abs_containing_block;
    ASSERT_NE(collector, nullptr);
    const zui::layout::LayoutResult* collector_result = Scene::result(collector);
    ASSERT_NE(collector_result, nullptr);
    EXPECT_GT(collector_result->overflow.ink.h, 900.f)
        << "the out-of-flow child's ink was not merged into its containing block";
}

// LAYOUT.md section 9 step B: a subtree that leaves the visible rectangle is skipped whole. Its
// world coordinates are not written and it must not stay hittable at the position it had before -
// that is what would otherwise send a click to a box that scrolled away.
TEST(DifftestSelfCheck, OffScreenSubtreeIsCulledAndUnhittable) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "scroll-cull.html")) << error;

    zui::layout::Box* pane = scene.box("pane");
    zui::layout::Box* far = scene.box("far");
    zui::layout::Box* deep = scene.box("deep");
    ASSERT_NE(pane, nullptr);
    ASSERT_NE(far, nullptr);
    ASSERT_NE(deep, nullptr);
    // Frame one: the whole pane is on screen and every box has a rectangle.
    EXPECT_FALSE(far->geo.culled);
    EXPECT_FALSE(deep->geo.culled);
    const float deep_x = deep->geo.world_x;
    const float deep_y = deep->geo.world_y;
    EXPECT_GT(deep->geo.width, 0.0f);
    EXPECT_EQ(scene.layout->hit_test(deep_x + 2.0f, deep_y + 2.0f), deep);

    // The frame a wheel event produces: the scroll offset moves, nothing is laid out again.
    zui::html::Node* node = zui::html::find(scene.doc, "pane");
    ASSERT_NE(node, nullptr);
    node->scroll_y = 300.0f;
    scene.layout->sync_world();

    EXPECT_TRUE(far->geo.culled) << "the subtree that scrolled out was still mapped";
    EXPECT_GE(zui::layout::paint_stats().culled, std::size_t{1});
    // The point that used to land on the descendant now belongs to the scroll container.
    EXPECT_NE(scene.layout->hit_test(deep_x + 2.0f, deep_y + 2.0f), deep)
        << "a hit reached a box the frame never placed";
    EXPECT_EQ(scene.layout->hit_test(deep_x + 2.0f, deep_y + 2.0f), pane);
}

// LAYOUT.md section 9 step D: a scroll container keeps the display list of its content, and a frame
// that only scrolled replays it. The counters below are the check: reused, walked, damaged, refused.
// Picture equality of a replay against a fresh walk needs the presented framebuffer. The software
// raster that used to sample those pixels is gone.
// docs/PERF.md I6: a paint-only change whose node is known damages only that node's rectangle, so a
// listing the rectangle misses is replayed instead of re-recorded.
// A bar outside the scroller keeps taking hits after the scroller below it is scrolled
// programmatically - the frames `Document::scroll_into_view` produces, with no pointer input.
TEST(DifftestSelfCheck, FixedBarStaysHittableAfterAScroll) {
    Scene scene;
    std::string error;
    const std::filesystem::path page =
        std::filesystem::temp_directory_path() / "zui_fixed_bar_scroll.html";
    {
        std::ofstream out(page);
        out << "<!DOCTYPE html><html><head><style>"
               "html,body{margin:0}"
               "[hidden]{display:none}"
               "#bar{position:absolute;top:0;left:0;width:400px;height:36px}"
               "#btn{position:absolute;right:8px;top:6px;width:60px;height:24px}"
               "#sc{position:absolute;top:36px;left:0;width:400px;height:120px;overflow:auto}"
               "#long{height:600px}"
               "#sheet{position:absolute;top:0;left:0;width:400px;height:200px}"
               "</style></head><body>"
               "<div id=\"bar\"><button id=\"btn\">B</button></div>"
               "<div id=\"sc\"><div id=\"long\"></div></div>"
               "<div id=\"sheet\" hidden>x</div>"
               "</body></html>\n";
    }
    ASSERT_TRUE(scene.load_path(error, page.string())) << error;
    std::error_code ec;
    std::filesystem::remove(page, ec);

    zui::layout::Box* button = scene.box("btn");
    ASSERT_NE(button, nullptr);
    ASSERT_GT(button->geo.width, 0.f);
    const float x = button->geo.world_x + button->geo.width * 0.5f;
    const float y = button->geo.world_y + button->geo.height * 0.5f;
    auto hit_reaches = [](zui::layout::Box* hit, const zui::layout::Box* target) {
        for (zui::layout::Box* box = hit; box != nullptr; box = box->parent) {
            if (box == target) {
                return true;
            }
        }
        return false;
    };
    {
        zui::layout::Box* hit = scene.layout->hit_test(x, y);
        std::string chain;
        for (zui::layout::Box* box = hit; box != nullptr; box = box->parent) {
            chain += box->node != nullptr ? box->node->tag : "-";
            if (box->node != nullptr && !box->node->id.empty()) {
                chain += "#" + box->node->id;
            }
            chain += " ";
        }
        EXPECT_TRUE(hit_reaches(hit, button))
            << "hit chain: " << chain << " at (" << x << "," << y << ") button=("
            << button->geo.world_x << "," << button->geo.world_y << " " << button->geo.width << "x"
           << button->geo.height << ")";
    }

    zui::html::Node* sc = zui::html::find(scene.doc, "sc");
    zui::html::Node* sheet = zui::html::find(scene.doc, "sheet");
    ASSERT_NE(sc, nullptr);
    ASSERT_NE(sheet, nullptr);
    sc->scroll_y = 28.f;
    scene.layout->sync_world();
    // An overlay that opens and closes again, the way a dialog does.
    sheet->visible = false;
    scene.frame();
    sheet->visible = true;
    scene.frame();
    sheet->visible = false;
    scene.frame();

    zui::layout::Box* later = scene.box("btn");
    ASSERT_NE(later, nullptr);
    EXPECT_TRUE(hit_reaches(scene.layout->hit_test(x, y), later))
        << "the bar left the hit map when the scroller moved";
}

TEST(DifftestSelfCheck, PaintDamageKeepsListingsOutsideIt) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "scroll-retain.html")) << error;
    zui::html::Node* label = zui::html::find(scene.doc, "label");
    ASSERT_NE(label, nullptr);
    ASSERT_NE(label->computed, nullptr);

    constexpr int kWidth = 800;
    constexpr int kHeight = 600;
    zui::paint::Painter retained;
#ifdef _WIN32
    _putenv_s("ZUI_PERF_DAMAGE", "1");
#else
    setenv("ZUI_PERF_DAMAGE", "1", 1);
#endif
    const auto paint_frame = [&]() {
        zui::paint::DisplayList list;
        retained.paint(*scene.layout->root(), list, kWidth, kHeight);
    };
    paint_frame();

    // A paint-only change on one node: rewrite its computed colour and report it as node damage.
    using ComputedMap = std::decay_t<decltype(*label->computed)>;
    auto map = std::make_shared<ComputedMap>(*label->computed);
    (*map)["color"] = "rgb(10,20,30)";
    label->computed = map;
    zui::html::note_paint_change_at(label);
    scene.layout->sync_world();

    paint_frame();
    // The frame went through the damage path (a stamp change used to clear every listing
    // unconditionally), and the listing the damage touched was the one erased.
    EXPECT_GE(retained.retained_stats().damaged, std::size_t{1})
        << "a node damage did not reach the retained listings";
    // `damage.items` (docs/PERF.md section 2.2): the erased listings held items, and those are the
    // items the damage reclaimed.
    EXPECT_GE(retained.retained_stats().damaged_items, std::size_t{1})
        << "the damage reclaimed no retained items";
}

TEST(DifftestSelfCheck, RetainedListingMatchesTheWalkItReplaced) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "scroll-retain.html")) << error;
    zui::html::Node* pane = zui::html::find(scene.doc, "pane");
    zui::html::Node* inner = zui::html::find(scene.doc, "pane2");
    zui::html::Node* row = zui::html::find(scene.doc, "r3");
    zui::html::Node* label = zui::html::find(scene.doc, "label");
    ASSERT_NE(pane, nullptr);
    ASSERT_NE(inner, nullptr);
    ASSERT_NE(row, nullptr);
    ASSERT_NE(label, nullptr);

    constexpr int kWidth = 800;
    constexpr int kHeight = 600;
    zui::paint::Painter retained;
    ASSERT_TRUE(retained.retained());

    const auto paint_frame = [&]() {
        zui::paint::DisplayList list;
        retained.paint(*scene.layout->root(), list, kWidth, kHeight);
    };

    paint_frame();

    // A scroll the retained margin covers: the listing is replayed instead of walked.
    pane->scroll_y = 48.f;
    scene.layout->sync_world();
    paint_frame();
    EXPECT_GE(retained.retained_stats().reused, std::size_t{1})
        << "a scroll inside the margin did not replay a listing";

    pane->scroll_y = 96.f;
    scene.layout->sync_world();
    paint_frame();
    EXPECT_GE(retained.retained_stats().reused, std::size_t{1})
        << "the second scroll inside the margin did not replay a listing";

    // The nested container's own scroll, while it is on screen: the outer listing covers that
    // content, so the outer has to be re-recorded rather than shifted. The inner container keeps a
    // listing of its own (`Scope`), which is what makes the pages that scroll a pane instead of the
    // page retain anything at all.
    inner->scroll_y = 60.f;
    scene.layout->sync_world();
    paint_frame();
    EXPECT_GE(retained.retained_stats().walked, std::size_t{1})
        << "a nested container's scroll did not re-record the container above it";

    // A jump past the margin: the frame pays for one walk, and still paints the same frame.
    pane->scroll_y = 1248.f;
    scene.layout->sync_world();
    paint_frame();
    EXPECT_GE(retained.retained_stats().walked, std::size_t{1})
        << "a jump past the margin did not re-record";

    // A paint-only style change. Nothing lays out, and the cascade is the only place that sees it:
    // it is the stamp from there that has to drop the listing.
    row->class_name += " hot";
    scene.app_frame();
    paint_frame();
    EXPECT_EQ(retained.retained_stats().reused, std::size_t{0})
        << "a paint-only change replayed a listing of the old colour";

    // A text change: content rather than style, and it has to drop the listing too.
    label->text = "changed";
    scene.layout->sync_text(label);
    scene.app_frame();
    paint_frame();
    EXPECT_GE(retained.retained_stats().walked, std::size_t{1});

    // Back to a plain scroll: the listing the last frame recorded is what this one replays.
    pane->scroll_y = 1200.f;
    scene.layout->sync_world();
    paint_frame();
    EXPECT_GE(retained.retained_stats().reused, std::size_t{1})
        << "the frame after the changes did not replay";
}

// A `sticky` box is positioned against its container's scroll rather than carried by it, so a
// listing of that container's content would move it along with everything else. The painter keeps
// the refusal instead of a listing.
TEST(DifftestSelfCheck, RetainedListingIsRefusedAroundStickyContent) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "scroll-sticky.html")) << error;
    zui::html::Node* pane = zui::html::find(scene.doc, "pane");
    ASSERT_NE(pane, nullptr);

    constexpr int kWidth = 800;
    constexpr int kHeight = 600;
    zui::paint::Painter retained;
    const auto paint_frame = [&]() {
        zui::paint::DisplayList list;
        retained.paint(*scene.layout->root(), list, kWidth, kHeight);
    };

    paint_frame();
    EXPECT_GE(retained.retained_stats().refused, std::size_t{1})
        << "a container with a sticky box inside it kept a listing";

    pane->scroll_y = 48.f;
    scene.layout->sync_world();
    paint_frame();
    EXPECT_EQ(retained.retained_stats().reused, std::size_t{0})
        << "a listing was replayed under a sticky box";
    EXPECT_GE(retained.retained_stats().refused, std::size_t{1}) << "the refusal was forgotten";
}

// A scroller inside position:fixed (album lightbox `.stage`, motion sheet) still shifts by its
// own overflow. The fixed ancestor is outside the listing, so a replay is valid.
TEST(DifftestSelfCheck, RetainedListingReplaysScrollerInsideFixed) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "scroll-fixed.html")) << error;
    zui::html::Node* stage = zui::html::find(scene.doc, "stage");
    ASSERT_NE(stage, nullptr);

    constexpr int kWidth = 800;
    constexpr int kHeight = 600;
    zui::paint::Painter retained;
    ASSERT_TRUE(retained.retained());
    const auto paint_frame = [&]() {
        zui::paint::DisplayList list;
        retained.paint(*scene.layout->root(), list, kWidth, kHeight);
    };

    paint_frame();
    stage->scroll_y = 48.f;
    scene.layout->sync_world();
    paint_frame();
    EXPECT_GE(retained.retained_stats().reused, std::size_t{1})
        << "overflow:auto inside position:fixed did not replay a listing";
    // The document listing is still refused: it contains the fixed overlay. The inner `#stage`
    // listing is the one that must replay; a leftover ancestor-fixed rule would leave reused at 0.
}
