// The idle present lock for the example/tests programs (docs/TEST_ARCHITECTURE.md 渲染):
// two quiet frames must each present a non-empty display list of the same length. This is the
// `render-present` half; the work-unit half lives in example_tests_perf_test.cpp and the two must
// not share a TEST (Release skips the perf one but still runs this).
//
// `ColorTransitionRepaintsUntilItEnds` is the other half of "what reaches the screen": a running
// transition has to keep painting by itself, with no input to drive it.

#include "handler_rig.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include <chrono>
#include <fstream>
#include <thread>

#include "../../example/tests/checks/actions.h"
#include "../../example/tests/hello/actions.h"
#include "../../example/tests/studio/actions.h"
#include "../../example/tests/transition/actions.h"

namespace fs = std::filesystem;

namespace {

struct Windowed {
    zui::test::HandlerRig rig;

    bool load(const char* program) {
        return load(program, [](zui::App&) {});
    }

    bool load(const char* program, const std::function<void(zui::App&)>& bind) {
        const fs::path html = fs::path(ZUI_SOURCE_DIR) / "example" / "tests" / program /
                              (std::string(program) + ".html");
        if (!rig.app.document().load(html.string())) {
            return false;
        }
        bind(rig.app);
        return rig.windowed() && rig.pump();
    }
};

/// Two quiet frames: each presents a non-empty list of the same length (docs/PERF.md section 2.2;
/// the idle "still painted" oracle, not the `painted_walked` work gate).
void expect_idle_resubmits(zui::test::HandlerRig& rig) {
    ASSERT_TRUE(rig.pump());
    ASSERT_FALSE(rig.renderer().submit_sizes.empty());
    const std::size_t first = rig.renderer().submit_sizes.back();
    ASSERT_GT(first, 0u);
    const int present1 = rig.renderer().present_count;
    ASSERT_TRUE(rig.pump());
    EXPECT_GT(rig.renderer().present_count, present1);
    ASSERT_FALSE(rig.renderer().submit_sizes.empty());
    EXPECT_EQ(rig.renderer().submit_sizes.back(), first);
}

}  // namespace

TEST(AppInteract, ExampleHelloIdleResubmitsTheLastList) {
    Windowed w;
    ASSERT_TRUE(w.load("hello", hello_example::bind_actions));
    expect_idle_resubmits(w.rig);
}

TEST(AppInteract, ExampleChecksIdleResubmitsTheLastList) {
    Windowed w;
    ASSERT_TRUE(w.load("checks", checks_example::bind_actions));
    expect_idle_resubmits(w.rig);
}

TEST(AppInteract, ExampleTransitionIdleResubmitsTheLastList) {
    Windowed w;
    ASSERT_TRUE(w.load("transition", transition_example::bind_actions));
    expect_idle_resubmits(w.rig);
}

TEST(AppInteract, ExampleStudioIdleResubmitsTheLastList) {
    Windowed w;
    studio_example::Actions actions(w.rig.app);
    ASSERT_TRUE(w.load("studio", [&](zui::App&) { actions.bind(); }));
    expect_idle_resubmits(w.rig);
}

TEST(AppInteract, ExampleFlowIdleResubmitsTheLastList) {
    Windowed w;
    ASSERT_TRUE(w.load("flow"));
    expect_idle_resubmits(w.rig);
}

TEST(AppInteract, ExampleStyleIdleResubmitsTheLastList) {
    Windowed w;
    ASSERT_TRUE(w.load("style"));
    expect_idle_resubmits(w.rig);
}

TEST(AppInteract, ExampleTableIdleResubmitsTheLastList) {
    Windowed w;
    ASSERT_TRUE(w.load("table"));
    expect_idle_resubmits(w.rig);
}

TEST(AppInteract, ExampleTextIdleResubmitsTheLastList) {
    Windowed w;
    ASSERT_TRUE(w.load("text"));
    expect_idle_resubmits(w.rig);
}

/// A running transition is a paint input of its own: `Transitions::sample` writes the used overlay
/// outside the interned style, so neither the cascade nor the layout reports it. The frame loop has
/// to paint while it samples, or the screen keeps the last frame an input happened to paint and a
/// colour transition freezes at whatever intermediate value that was until the next mouse move
/// (docs/PERF.md I6). The page is synthetic so the transition is long enough to span several
/// frames even on a slow build: a real page's frames can be slower than its own transition.
namespace {

/// Whether the presented list fills something with exactly this colour.
bool presents_fill(const std::vector<zui::paint::DisplayItem>& items, zui::paint::Color want) {
    for (const zui::paint::DisplayItem& item : items) {
        if (item.op == zui::paint::DisplayOp::FillRect && item.color.r == want.r &&
            item.color.g == want.g && item.color.b == want.b && item.color.a == want.a) {
            return true;
        }
    }
    return false;
}

/// Sum of DrawText y for items whose utf8 contains `needle`. Sidebar labels that do not scroll
/// stay put; the scrolled copy moves, so the sum changes even when the same string is painted twice.
float sum_text_y(const std::vector<zui::paint::DisplayItem>& items, std::string_view needle) {
    float sum = 0.f;
    int n = 0;
    for (const zui::paint::DisplayItem& item : items) {
        if (item.op != zui::paint::DisplayOp::DrawText) {
            continue;
        }
        if (item.text.find(needle) == std::string::npos) {
            continue;
        }
        sum += item.rect.y;
        ++n;
    }
    return n == 0 ? 0.f : sum;
}

int count_text(const std::vector<zui::paint::DisplayItem>& items, std::string_view needle) {
    int n = 0;
    for (const zui::paint::DisplayItem& item : items) {
        if (item.op == zui::paint::DisplayOp::DrawText &&
            item.text.find(needle) != std::string::npos) {
            ++n;
        }
    }
    return n;
}

bool page_box(zui::App& app, float& x, float& y, float& w, float& h) {
    for (const zui::frame::NodeCensus& row : zui::frame::Access::census(app)) {
        if (zui::test::class_token(row.class_attr, "page")) {
            x = row.x;
            y = row.y;
            w = row.w;
            h = row.h;
            return h > 40.f;
        }
    }
    return false;
}

}  // namespace

/// Wheel must rebuild (or shift-replay) the submitted list. Geometry tests that only read
/// `box_rect` stay green while `paint_now` is false and the GPU keeps the previous frame
/// (docs/PERF.md I6; album lightbox and flow).
TEST(AppInteract, ExampleFlowPageScrollPaintsTheNewOffset) {
    Windowed w;
    ASSERT_TRUE(w.load("flow"));
    w.rig.observe();
    float x = 0.f, y = 0.f, box_w = 0.f, box_h = 0.f;
    ASSERT_TRUE(page_box(w.rig.app, x, y, box_w, box_h));
    const float cx = x + box_w * 0.5f;
    const float cy = y + box_h * 0.5f;
    ASSERT_TRUE(w.rig.wheel_xy(cx, cy, 0.f));
    ASSERT_TRUE(w.rig.pump());
    const std::vector<zui::paint::DisplayItem> before = w.rig.renderer().last_items;
    ASSERT_GT(count_text(before, "相邻垂直"), 0);
    const float y0 = sum_text_y(before, "相邻垂直");
    ASSERT_TRUE(w.rig.wheel_xy(cx, cy, -2.f));
    const std::vector<zui::paint::DisplayItem> after = w.rig.renderer().last_items;
    ASSERT_GT(count_text(after, "相邻垂直"), 0);
    EXPECT_LT(sum_text_y(after, "相邻垂直"), y0 - 8.f)
        << "flow .page wheel must present the new offset, not resubmit the last list";
}

TEST(AppInteract, ExampleHelloContentScrollPaintsTheNewOffset) {
    Windowed w;
    ASSERT_TRUE(w.load("hello", hello_example::bind_actions));
    w.rig.observe();
    float x = 0.f, y = 0.f, box_w = 0.f, box_h = 0.f;
    ASSERT_TRUE(zui::frame::Access::box_rect(w.rig.app, "heading", x, y, box_w, box_h));
    const float cx = x + box_w * 0.5f;
    const float cy = y + box_h * 0.5f;
    ASSERT_TRUE(w.rig.wheel_xy(cx, cy, 0.f));
    ASSERT_TRUE(w.rig.pump());
    const std::vector<zui::paint::DisplayItem> before = w.rig.renderer().last_items;
    ASSERT_GT(count_text(before, "Home"), 0);
    const float y0 = sum_text_y(before, "Home");
    ASSERT_TRUE(w.rig.wheel_xy(cx, cy, -2.f));
    const std::vector<zui::paint::DisplayItem> after = w.rig.renderer().last_items;
    ASSERT_GT(count_text(after, "Home"), 0);
    EXPECT_LT(sum_text_y(after, "Home"), y0 - 8.f)
        << "hello .content wheel must present the new offset, not resubmit the last list";
}

TEST(AppInteract, ScrollInsideFixedPaintsTheNewOffset) {
    const fs::path html =
        fs::path(ZUI_SOURCE_DIR) / "tests" / "difftest" / "selfcheck" / "scroll-fixed.html";
    Windowed w;
    ASSERT_TRUE(w.rig.app.document().load(html.string()));
    ASSERT_TRUE(w.rig.windowed());
    ASSERT_TRUE(w.rig.pump());
    w.rig.observe();
    float x = 0.f, y = 0.f, box_w = 0.f, box_h = 0.f;
    ASSERT_TRUE(zui::frame::Access::box_rect(w.rig.app, "stage", x, y, box_w, box_h));
    const float cx = x + box_w * 0.5f;
    const float cy = y + box_h * 0.5f;
    ASSERT_TRUE(w.rig.wheel_xy(cx, cy, 0.f));
    ASSERT_TRUE(w.rig.pump());
    const std::vector<zui::paint::DisplayItem> before = w.rig.renderer().last_items;
    float rows0 = 0.f;
    int n0 = 0;
    for (const zui::paint::DisplayItem& item : before) {
        if (item.op == zui::paint::DisplayOp::FillRect && item.rect.h > 20.f &&
            item.rect.h < 28.f) {
            rows0 += item.rect.y;
            ++n0;
        }
    }
    ASSERT_GT(n0, 0) << "scroll-fixed .row fills were not in the submitted list";
    // One notch = 48 px: two of the 24 px rows stay inside the 120 px stage clip.
    ASSERT_TRUE(w.rig.wheel_xy(cx, cy, -1.f));
    const std::vector<zui::paint::DisplayItem> after = w.rig.renderer().last_items;
    float rows1 = 0.f;
    int n1 = 0;
    for (const zui::paint::DisplayItem& item : after) {
        if (item.op == zui::paint::DisplayOp::FillRect && item.rect.h > 20.f &&
            item.rect.h < 28.f) {
            rows1 += item.rect.y;
            ++n1;
        }
    }
    ASSERT_GT(n1, 0);
    EXPECT_LT(rows1, rows0 - 8.f)
        << "overflow:auto under position:fixed must present the new offset (album lightbox)";
}

TEST(AppInteract, ColorTransitionRepaintsUntilItEnds) {
    const fs::path dir = zui::test::scratch_dir("transition-paint");
    fs::create_directories(dir);
    const fs::path page = dir / "page.html";
    {
        std::ofstream out(page);
        out << "<!DOCTYPE html><html><head><style>"
               "body{margin:0}"
               ".box{width:100px;height:100px;background-color:rgb(0,0,0);"
               "transition:background-color 400ms linear}"
               ".box.on{background-color:rgb(255,0,0)}"
               "</style></head><body><div id=\"box\" class=\"box\"></div></body></html>\n";
    }
    Windowed w;
    ASSERT_TRUE(w.rig.app.document().load(page.string()));
    ASSERT_TRUE(w.rig.windowed());
    ASSERT_TRUE(w.rig.pump());
    w.rig.settle();

    ASSERT_TRUE(w.rig.app.document().add_class("box", "on"));
    ASSERT_TRUE(w.rig.pump());
    // The host loop keeps pumping while the transition runs; give it wall-clock time.
    for (int i = 0; i < 60; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        ASSERT_TRUE(w.rig.pump());
    }
    const std::vector<zui::paint::DisplayItem> settled = w.rig.renderer().last_items;
    EXPECT_TRUE(presents_fill(settled, zui::paint::Color{255, 0, 0, 255}))
        << "a finished colour transition must be on screen without further input";
}
