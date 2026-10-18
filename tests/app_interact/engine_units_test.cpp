// Whole-engine work-unit gates (docs/PERF.md section 6.2, the 2026-10-07 rows). Before this file
// the work-unit account stopped at layout: the counters that reach into text shaping, image decode
// and event dispatch were counted but never gated. Debug only - FrameWork is zero in Release, so
// each starts with `GTEST_SKIP`, like the other 性能-tagged cases.

#include "handler_rig.h"

#include "../../example/tests/hello/actions.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

fs::path repo(const char* rel) { return fs::path(ZUI_SOURCE_DIR) / rel; }

/// The largest value each whole-engine counter reached across the frames from the current state to
/// quiet. A layout frame publishes `text_shape_calls` / `image_decodes`, a dispatch publishes
/// `input_events`. The per-field max is robust to which observe the driver lands the work on - the
/// headless seam may flush inside `prepare_interaction`, so the first layout can already be in
/// `last_frame_work()` before the loop starts.
zui::perf::FrameWork peak_engine_units(zui::test::HandlerRig& rig, int frames = 12) {
    zui::perf::FrameWork best = zui::perf::last_frame_work();
    auto fold = [](zui::perf::FrameWork& into, const zui::perf::FrameWork& frame) {
        into.text_shape_calls = std::max(into.text_shape_calls, frame.text_shape_calls);
        into.image_decodes = std::max(into.image_decodes, frame.image_decodes);
        into.input_events = std::max(into.input_events, frame.input_events);
    };
    fold(best, best);
    for (int i = 0; i < frames; ++i) {
        rig.observe();
        const zui::perf::FrameWork frame = zui::perf::last_frame_work();
        fold(best, frame);
        if (zui::test::HandlerRig::idle_frame(frame)) {
            break;
        }
    }
    return best;
}

}  // namespace

// A load builds the engine Node tree and parses the stylesheets (UA included). Both are counted in
// the window `App::load` opens, so a load can be read as work without a trace (docs/PERF.md
// section 6.1). This is the parse half the earlier rows could not reach: `load.html` / `load.css`
// are trace zones, and before the load window the counters never left zero.
TEST(AppInteract, EngineUnitsHtmlAndCssParseCountTheWork) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    zui::test::HandlerRig rig;
    ASSERT_TRUE(rig.app.document().load(repo("example/tests/hello/hello.html").string()));
    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    EXPECT_GT(work.html_nodes, 0u) << "html_nodes=" << work.html_nodes;
    EXPECT_GE(work.css_sheets, 1u) << "css_sheets=" << work.css_sheets;
}

// One click on a bound control dispatches one handler event. The unit is the dispatch, not the
// hit-test walk (`hit_boxes`) and not the cascade the handler may cause.
TEST(AppInteract, EngineUnitsInputEventsCountTheDispatch) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    zui::test::HandlerRig rig;
    ASSERT_TRUE(rig.app.document().load(repo("example/tests/hello/hello.html").string()));
    hello_example::bind_actions(rig.app);
    ASSERT_TRUE(rig.headless());
    rig.observe();
    ASSERT_TRUE(zui::frame::Access::click_id(rig.app, "inc"));
    const zui::perf::FrameWork work = peak_engine_units(rig);
    EXPECT_GE(work.input_events, 1u) << "input_events=" << work.input_events;
}

// A page with text shapes it on the layout that first places the runs. The unit is the uncached
// `LayoutContext::shape` call, so a fully memoised re-layout contributes zero.
TEST(AppInteract, EngineUnitsTextShapeCountsTheMiss) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    zui::test::HandlerRig rig;
    ASSERT_TRUE(rig.app.document().load(repo("example/tests/hello/hello.html").string()));
    hello_example::bind_actions(rig.app);
    ASSERT_TRUE(rig.headless());
    const zui::perf::FrameWork work = peak_engine_units(rig);
    EXPECT_GT(work.text_shape_calls, 0u) << "text_shape_calls=" << work.text_shape_calls;
}

// A substituted element decodes its bitmap once, on the layout that measures it. Re-decoding the
// same path is a cache hit and must not move the unit.
TEST(AppInteract, EngineUnitsImageDecodeCountsTheMiss) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    zui::test::HandlerRig rig;
    ASSERT_TRUE(rig.app.document().load(repo("tests/difftest/cases/object-fit/page.html").string()));
    ASSERT_TRUE(rig.headless());
    const zui::perf::FrameWork work = peak_engine_units(rig);
    EXPECT_GT(work.image_decodes, 0u) << "image_decodes=" << work.image_decodes;
}
