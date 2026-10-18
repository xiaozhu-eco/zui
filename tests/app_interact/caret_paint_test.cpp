#include "handler_rig.h"
#include "log/log.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace fs = std::filesystem;

namespace {

const char* kCaretPage =
    "<!DOCTYPE html><html><head><style>"
    "body{margin:0}"
    "textarea{width:400px;height:60px;font:16px monospace;caret-color:rgb(0,0,0);padding:4px}"
    "</style></head><body>"
    "<textarea id=\"t\">Hello world</textarea>"
    "</body></html>\n";

struct CaretPage {
    zui::test::HandlerRig rig;

    bool load() {
        const fs::path dir = zui::test::scratch_dir("caret-p54");
        fs::create_directories(dir);
        const fs::path page = dir / "page.html";
        {
            std::ofstream out(page);
            out << kCaretPage;
        }
        if (!rig.app.document().load(page.string())) {
            return false;
        }
        if (!rig.windowed() || !rig.pump()) {
            return false;
        }
        rig.settle();
        return true;
    }

    bool caret_bar(float& out_x, float& out_y, float& out_h) {
        const std::optional<zui::ElementBox> box = rig.app.document().measure("t");
        if (!box || !box->laid) {
            return false;
        }
        for (const zui::paint::DisplayItem& item : rig.renderer().last_items) {
            if (item.op != zui::paint::DisplayOp::FillRect || item.rect.w > 1.01f ||
                item.rect.h < 8.f || item.color.a == 0) {
                continue;
            }
            if (item.rect.x < box->border.x || item.rect.x > box->border.x + box->border.width ||
                item.rect.y < box->border.y || item.rect.y > box->border.y + box->border.height) {
                continue;
            }
            out_x = item.rect.x;
            out_y = item.rect.y;
            out_h = item.rect.h;
            return true;
        }
        return false;
    }
};

}  // namespace

TEST(CaretService, P54CaretOnlyDamageNoRelayout) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    CaretPage page;
    ASSERT_TRUE(page.load());
    ASSERT_TRUE(page.rig.app.document().focus("t"));
    ASSERT_TRUE(page.rig.pump());

    // Loader leaves caret at end; Home walks to the start via apply_editing (Clip + frame_visual).
    zui::backends::TextInputFrame home;
    home.home = 1;
    page.rig.window().inject(std::move(home));
    ASSERT_TRUE(page.rig.pump());
    EXPECT_EQ(std::stoul(page.rig.get("t", "caret")), 0u);

    float x0 = 0.f, y0 = 0.f, h0 = 0.f;
    ASSERT_TRUE(page.caret_bar(x0, y0, h0)) << "no caret bar after Home";

    zui::backends::TextInputFrame right;
    right.right = 1;
    page.rig.window().inject(std::move(right));
    ASSERT_TRUE(page.rig.pump());

    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    ZUI_FILE_INFO("[p54] laid={} damage_rects={} painted_walked={}", work.laid_out,
                  work.damage_rects, work.painted_walked);

    EXPECT_EQ(std::stoul(page.rig.get("t", "caret")), 1u);
    EXPECT_EQ(work.laid_out, 0u) << "caret-only move must not relayout";
    EXPECT_GE(work.damage_rects, 1u) << "Clip(caret) damage must be recorded";
    EXPECT_GT(work.painted_walked, 0u) << "caret Clip damage must trigger a paint walk";

    float x1 = 0.f, y1 = 0.f, h1 = 0.f;
    ASSERT_TRUE(page.caret_bar(x1, y1, h1)) << "no caret bar after ArrowRight";
    EXPECT_GT(x1, x0 + 2.f) << "caret bar must move with the caret";
    EXPECT_NEAR(y1, y0, 1.f);
}


TEST(CaretService, BlinkTogglesWithoutLayout) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    CaretPage page;
    ASSERT_TRUE(page.load());
    ASSERT_TRUE(page.rig.app.document().focus("t"));
    ASSERT_TRUE(page.rig.pump());

    float x = 0.f, y = 0.f, h = 0.f;
    ASSERT_TRUE(page.caret_bar(x, y, h)) << "caret should be visible right after focus";

    // Cross one 530ms blink half-period without real sleep.
    zui::frame::Access::advance_time(page.rig.app, 0.55);
    ASSERT_TRUE(page.rig.pump());

    const zui::perf::FrameWork work = zui::perf::last_frame_work();
    ZUI_FILE_INFO("[blink] laid={} damage_rects={} painted_walked={}", work.laid_out,
                  work.damage_rects, work.painted_walked);

    EXPECT_EQ(work.laid_out, 0u) << "blink must not relayout";
    EXPECT_GE(work.damage_rects, 1u) << "blink edge must Clip(caret)";
    EXPECT_FALSE(page.caret_bar(x, y, h)) << "caret fill must hide on the off phase";

    // Next half-period brings the bar back; still no layout.
    zui::frame::Access::advance_time(page.rig.app, 0.55);
    ASSERT_TRUE(page.rig.pump());
    const zui::perf::FrameWork work2 = zui::perf::last_frame_work();
    EXPECT_EQ(work2.laid_out, 0u);
    EXPECT_GE(work2.damage_rects, 1u);
    ASSERT_TRUE(page.caret_bar(x, y, h)) << "caret fill must return on the on phase";
}
