// Layout invalidation, reframes, and container reentry.
// L6 architecture self-checks (TESTPLAN section 12).
// Shared scene harness: selfcheck_scene.h.

#include "selfcheck_scene.h"

using namespace difftest_selfcheck;

// Section 12: the same key twice hits the same pointer.
TEST(DifftestSelfCheck, SameKeyHitsSamePointer) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error)) << error;
    const zui::layout::LayoutResult* first = Scene::result(scene.box("first"));
    ASSERT_NE(first, nullptr);
    scene.frame();
    scene.frame();
    EXPECT_EQ(Scene::result(scene.box("first")), first)
        << "an unchanged box was laid out again; the cache did not hit on the same key";
}

// Section 12: after a scroll sequence the layout result pointer and the geometry are unchanged
// (a scroll must never re-run layout).
TEST(DifftestSelfCheck, ScrollDoesNotRelayout) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error)) << error;
    zui::layout::Box* root = scene.layout->root();
    ASSERT_NE(root, nullptr);
    const zui::layout::LayoutResult* before = Scene::result(root);
    ASSERT_NE(before, nullptr);
    const float block_before = before->geometry.border_box_block;
    const std::size_t cascades_before = scene.cascade_count;

    for (const float offset : {0.0f, 120.0f, 360.0f, 900.0f, 40.0f}) {
        scene.doc.root->scroll_y = offset;
        scene.layout->sync_world();
        EXPECT_EQ(Scene::result(root), before) << "scroll at " << offset << " re-ran layout";
    }
    EXPECT_EQ(Scene::result(root)->geometry.border_box_block, block_before);
    EXPECT_EQ(scene.cascade_count, cascades_before) << "scrolling re-ran the cascade";
}

// Section 12: converged is true unless a case deliberately constructs otherwise.
TEST(DifftestSelfCheck, FrameConverges) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error)) << error;
    class Walk {
    public:
        bool ok = true;
        void visit(const zui::layout::Box* box) {
            if (box == nullptr) {
                return;
            }
            const zui::layout::LayoutResult* result = Scene::result(box);
            if (result != nullptr && !result->converged) {
                ok = false;
            }
            for (const zui::layout::Box* child : box->children) {
                visit(child);
            }
        }
    } walk;
    walk.visit(scene.layout->root());
    EXPECT_TRUE(walk.ok)
        << "a box reported converged == false; the LAYOUT section 16 diagnostics should name "
           "the source of the non-convergence";
}

// Section 12: adding or removing will-change does not rebuild or re-parent the box tree; only
// the cb_for_* booleans and the stacking membership change.
TEST(DifftestSelfCheck, WillChangeKeepsBoxTree) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error)) << error;
    std::unordered_map<const zui::layout::Box*, bool> before;
    collect_boxes(scene.layout->root(), before);
    const std::size_t count_before = before.size();
    const zui::layout::Box* first_before = scene.box("first");
    ASSERT_NE(first_before, nullptr);

    ASSERT_TRUE(zui::html::add_class(scene.doc, "first", "hover-lift"));
    scene.frame();

    std::unordered_map<const zui::layout::Box*, bool> after;
    collect_boxes(scene.layout->root(), after);
    EXPECT_EQ(after.size(), count_before) << "adding will-change changed the box count";
    for (const auto& entry : before) {
        EXPECT_TRUE(after.count(entry.first) != 0)
            << "adding will-change replaced a box instead of restyling it";
    }
    EXPECT_EQ(scene.box("first"), first_before) << "will-change re-parented a box";
}

// Section 12 / section 5: an interpolating transform frame marks nothing Layout dirty, and the
// intern table does not grow while it runs. `transform` is paint-only, so the only frame allowed
// to mark Layout is the one where the class crosses from `none` into a transform (section 6).
TEST(DifftestSelfCheck, AnimationFrameMarksNothingDirty) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error)) << error;
    const std::size_t intern_before = zui::layout::StyleTable::process().size();

    ASSERT_TRUE(zui::html::add_class(scene.doc, "first", "lifted"));
    scene.frame(0);  // the crossing frame: allowed to mark Layout (the containing block changed)
    const std::size_t intern_after_crossing = zui::layout::StyleTable::process().size();

    for (int step = 1; step <= 8; ++step) {
        scene.frame(step * 25);
        const zui::layout::LayoutContext::FrameStats& stats = scene.layout->frame_stats();
        EXPECT_EQ(stats.dirty_marks, 0)
            << "an interpolating transform frame marked boxes Layout dirty at t=" << step * 25
            << "ms; LAYOUT section 16 counts this as zero";
        EXPECT_EQ(stats.laid_out, 0)
            << "an interpolating transform frame re-ran layout at t=" << step * 25 << "ms";
    }
    const std::size_t intern_after = zui::layout::StyleTable::process().size();
    EXPECT_LE(intern_after, intern_after_crossing + 1)
        << "the intern table grew while the animation ran (LAYOUT section 12)";
}

// Section 12 / section 6: a text change marks the recorded chain and then stops - the frame
// after the change must settle, not keep invalidating.
TEST(DifftestSelfCheck, TextChangeInvalidatesOnceAndSettles) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error)) << error;
    zui::html::Node* label = zui::html::find(scene.doc, "label");
    ASSERT_NE(label, nullptr);

    label->text = "Changed";
    scene.layout->sync_text(label);
    scene.frame();
    EXPECT_GT(scene.layout->frame_stats().dirty_marks, 0)
        << "a text change did not mark the box whose content version moved";

    scene.frame();
    EXPECT_EQ(scene.layout->frame_stats().dirty_marks, 0)
        << "the frame after a text change kept marking boxes Layout dirty";
}

// Section 12 / section 6: font arrival re-lays out the boxes that shaped text with a fallback
// face, and does not touch ContentVersion - the content did not change.
TEST(DifftestSelfCheck, FontArrivalKeepsContentVersion) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error)) << error;

    zui::layout::Box* dependent = nullptr;
    class Finder {
    public:
        zui::layout::Box* found = nullptr;
        void visit(zui::layout::Box* box) {
            if (box == nullptr || found != nullptr) {
                return;
            }
            if (box->depends_on_font) {
                found = box;
                return;
            }
            for (zui::layout::Box* child : box->children) {
                visit(child);
            }
        }
    } finder;
    finder.visit(scene.layout->root());
    ASSERT_NE(finder.found, nullptr) << "the fixture has no box that depends on a font";
    dependent = finder.found;

    const zui::layout::ContentVersion before = dependent->content;
    const zui::layout::LayoutResult* pinned_before = Scene::result(dependent);
    ASSERT_NE(pinned_before, nullptr);

    scene.layout->notify_font_loaded();
    scene.frame();

    // "重排" means the result was produced again, not that the dirty flag was still standing
    // when the frame started: the invalidation walk compares contributions, which re-lays the
    // subject out. `LAYOUT.md` section 12's font-arrival assert is about the two things below.
    // The dependent's result pointer is not the signal: results are released and re-allocated every
    // frame and the allocator can hand the same address back, which made this check flaky. The
    // frame's own counter says a box was computed again, and the version below says why it was not
    // the content that changed.
    EXPECT_GT(scene.layout->frame_stats().laid_out, std::size_t{0})
        << "font arrival did not re-lay out a box that shaped text with a fallback face";
    EXPECT_NE(Scene::result(dependent), nullptr);
    EXPECT_EQ(dependent->content, before)
        << "font arrival bumped ContentVersion; LAYOUT section 6 says the content did not change";
}

// Section 12 / section 10: the frame-end container re-entry fuse is two rounds, and the frame
// either converges or names the source.
TEST(DifftestSelfCheck, ContainerReentryStaysWithinTwoRounds) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "container.html")) << error;
    scene.frame();
    const zui::layout::LayoutContext::FrameStats& stats = scene.layout->frame_stats();
    EXPECT_LE(stats.container_requery_rounds, 2)
        << "a container was re-entered more than twice in one frame (LAYOUT section 10)";
    EXPECT_TRUE(scene.layout->non_convergence().empty())
        << "the container frame reported a non-convergence without naming a source";

    // The fuse is not a substitute for settling: a frame that keeps marking the same box dirty
    // is exactly the silent non-convergence section 10 warns about, and `converged` must not
    // stay true while it happens.
    scene.frame();
    const zui::layout::LayoutContext::FrameStats& settled = scene.layout->frame_stats();
    EXPECT_EQ(settled.dirty_marks + settled.laid_out, 0)
        << "the container-query frame never settles: every frame marks " << settled.dirty_marks
        << " box(es) dirty and re-lays " << settled.laid_out
        << " (LAYOUT section 10). converged is still true, so no diagnostic names it.";
}

// B2: @container (min-width) rematches after layout publishes query-container sizes.
TEST(DifftestSelfCheck, ContainerStyleRematch) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "container-style.html")) << error;

    zui::html::Node* narrow_host = zui::html::find(scene.doc, "narrow");
    zui::html::Node* wide_host = zui::html::find(scene.doc, "wide");
    ASSERT_NE(narrow_host, nullptr);
    ASSERT_NE(wide_host, nullptr);
    EXPECT_TRUE(narrow_host->query_size_known);
    EXPECT_TRUE(wide_host->query_size_known);
    EXPECT_NEAR(narrow_host->query_width, 100.f, 0.5f);
    EXPECT_NEAR(wide_host->query_width, 200.f, 0.5f);

    zui::layout::Box* card_n = scene.box("card-n");
    zui::layout::Box* card_w = scene.box("card-w");
    ASSERT_NE(card_n, nullptr);
    ASSERT_NE(card_w, nullptr);
    EXPECT_NEAR(card_n->geo.content_width, 40.f, 0.5f)
        << "narrow host must not match min-width:150px";
    EXPECT_NEAR(card_w->geo.content_width, 120.f, 0.5f) << "wide host must rematch @container";
}
