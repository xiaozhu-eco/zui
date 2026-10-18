// `App::measure` is the A-tier CSSOM View measurement (docs/CONTRACT.md): it flushes pending
// style/layout and returns the border box plus the client/scroll sizes. It is not the same thing as
// the test-only no-flush observation in `frame::Access::measure`, and nothing exercised the public
// entry point before this test.

#include "app/frame_session.h"

#include <zui/app.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

fs::path write_page(const char* name, const std::string& html) {
    const fs::path page = fs::temp_directory_path() / name;
    std::ofstream out(page);
    out << html;
    return page;
}

}  // namespace

TEST(AppInteract, MeasureFlushesAndTheObservationSeamDoesNot) {
    const fs::path page = write_page(
        "zui-measure-api.html",
        "<!DOCTYPE html><html><head><style>"
        "html,body{margin:0}"
        "#box{width:120px;height:40px;padding:0 8px;overflow:hidden}"
        "#box.wide{width:200px}"
        "</style></head><body><div id=\"box\">x</div></body></html>\n");
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string()));
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app, 400, 300));
    zui::frame::Access::sync_observation(app);

    const auto box = app.document().measure("box");
    ASSERT_TRUE(box) << "measure() must find a laid-out box";
    EXPECT_NEAR(box->border.width, 136.f, 1.f) << "border box = content 120 + padding 16";
    EXPECT_NEAR(box->border.height, 40.f, 1.f);
    EXPECT_NEAR(box->content_width, 120.f, 1.f);
    EXPECT_NEAR(box->client_width, 136.f, 1.f) << "client = content + padding";
    EXPECT_NEAR(box->scroll_width, 136.f, 1.f);
    EXPECT_TRUE(box->laid);

    // A class that changes the inline size, without publishing a new observation. Reads through the
    // no-flush test seam still see the last frame (PERF.md I8); the CSSOM-style measure flushes and
    // sees the new one.
    ASSERT_TRUE(app.document().add_class("box", "wide"));
    EXPECT_NEAR(std::stof(zui::frame::Access::measure(app, "box", "content_width")), 120.f, 1.f)
        << "the observation seam must not flush";
    const auto wide = app.document().measure("box");
    ASSERT_TRUE(wide) << "measure() must flush the pending style/layout before reading";
    EXPECT_NEAR(wide->content_width, 200.f, 1.f);
    EXPECT_NEAR(wide->border.width, 216.f, 1.f) << "border box follows the new content width";
}
