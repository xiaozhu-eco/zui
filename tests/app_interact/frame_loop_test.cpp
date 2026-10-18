#include "app/frame_session.h"
#include "fake_window.h"
#include "recording_renderer.h"

#include "perf/perf_budget.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

fs::path write_page(const char* name, const std::string& html) {
    // One scratch file per test: ctest runs this binary's tests several at a time.
    const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
    const std::string tag = info != nullptr ? std::string(info->name()) : std::string("no-test");
    const fs::path page = fs::temp_directory_path() / (std::string(name) + "-" + tag);
    std::ofstream out(page);
    out << html;
    return page;
}

std::string idle_html() {
    return "<!DOCTYPE html><html><head><style>"
           "html,body{margin:0}"
           "#block{width:80px;height:40px;background-color:rgb(12,140,70)}"
           "</style></head><body><div id=\"block\"></div></body></html>\n";
}

std::string typing_html() {
    std::string html = "<!DOCTYPE html><html><head><style>"
                       "html,body{margin:0}"
                       "input{display:block;width:160px;height:28px}"
                       "p{margin:0;height:14px}"
                       "</style></head><body><input id=\"field\">";
    for (int i = 0; i < 40; ++i) {
        html += "<p id=\"p" + std::to_string(i) + "\">row</p>";
    }
    html += "</body></html>\n";
    return html;
}

struct BorrowedSession {
    zui::test::FakeWindow window;
    zui::test::RecordingRenderer renderer;
    zui::App app{0, nullptr};

    ~BorrowedSession() { zui::frame::Access::detach(app); }

    bool attach() {
        zui::frame::Session session;
        session.window = &window;
        session.renderer = &renderer;
        return zui::frame::Access::attach(app, session);
    }
};

struct PumpSnap {
    int present = 0;
    bool submitted = false;
    std::size_t submit_size = 0;
};

PumpSnap take_snap(const zui::test::RecordingRenderer& renderer, int previous_submits) {
    PumpSnap snap;
    snap.present = renderer.present_count;
    snap.submitted = static_cast<int>(renderer.submit_sizes.size()) > previous_submits;
    if (snap.submitted) {
        snap.submit_size = renderer.submit_sizes.back();
    }
    return snap;
}

std::string format_pumps(const PumpSnap& first, const PumpSnap& second, const PumpSnap& third,
                         std::size_t expected_n) {
    std::ostringstream out;
    auto one = [&](int index, const PumpSnap& snap) {
        out << "pump " << index << " present=" << snap.present << " submit=";
        if (snap.submitted) {
            out << snap.submit_size;
        } else {
            out << "none";
        }
        out << " expected N=" << expected_n << "\n";
    };
    one(1, first);
    one(2, second);
    one(3, third);
    return out.str();
}

std::string type_letter_a(BorrowedSession& rig) {
    if (!rig.app.pump_frame()) {
        return "pump 1";
    }
    if (!rig.app.document().focus("field")) {
        return "focus";
    }
    if (!rig.app.pump_frame()) {
        return "pump 2";
    }
    zui::backends::TextInputFrame frame;
    frame.text = "a";
    zui::backends::KeyPress press;
    press.key = "a";
    frame.key_events.push_back(std::move(press));
    rig.window.inject(std::move(frame));
    if (!rig.app.pump_frame()) {
        return "pump 3";
    }
    return {};
}

}  // namespace

TEST(AppInteract, IdleFrameResubmitsTheLastList) {
    BorrowedSession rig;
    ASSERT_TRUE(rig.app.document().load(write_page("zui-idle-present.html", idle_html()).string()));
    int fw = 0;
    int fh = 0;
    rig.window.framebuffer_size(fw, fh);
    EXPECT_EQ(fw, 800);
    EXPECT_EQ(fh, 600);
    EXPECT_FLOAT_EQ(rig.window.content_scale(), 1.f);
    ASSERT_TRUE(rig.attach());

    const int submits_before = static_cast<int>(rig.renderer.submit_sizes.size());
    const bool ok1 = rig.app.pump_frame();
    const PumpSnap first = take_snap(rig.renderer, submits_before);
    const int submits_after_first = static_cast<int>(rig.renderer.submit_sizes.size());
    const bool ok2 = rig.app.pump_frame();
    const PumpSnap second = take_snap(rig.renderer, submits_after_first);
    const int submits_after_second = static_cast<int>(rig.renderer.submit_sizes.size());
    const bool ok3 = rig.app.pump_frame();
    const PumpSnap third = take_snap(rig.renderer, submits_after_second);
    const std::size_t expected_n = first.submitted ? first.submit_size : 0;
    const std::string pumps = format_pumps(first, second, third, expected_n);

    EXPECT_TRUE(ok1) << pumps;
    EXPECT_TRUE(ok2) << pumps;
    EXPECT_TRUE(ok3) << pumps;
    EXPECT_GT(first.present, 0) << pumps;
    EXPECT_TRUE(first.submitted) << pumps;
    EXPECT_GT(expected_n, 0u) << pumps;
    EXPECT_GT(second.present, first.present) << pumps;
    EXPECT_TRUE(second.submitted) << pumps;
    EXPECT_EQ(second.submit_size, expected_n) << pumps;
    EXPECT_GT(third.present, second.present) << pumps;
    EXPECT_TRUE(third.submitted) << pumps;
    EXPECT_EQ(third.submit_size, expected_n) << pumps;
}

TEST(AppInteract, IdleFrameDoesNotWalkPaint) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    BorrowedSession rig;
    ASSERT_TRUE(rig.app.document().load(write_page("zui-idle-walk.html", idle_html()).string()));
    ASSERT_TRUE(rig.attach());
    ASSERT_TRUE(rig.app.pump_frame());
    ASSERT_TRUE(rig.app.pump_frame());
    const zui::perf::FrameWork second = zui::perf::last_frame_work();
    ASSERT_TRUE(rig.app.pump_frame());
    const zui::perf::FrameWork third = zui::perf::last_frame_work();
    EXPECT_EQ(second.cascade_visited, 0u);
    EXPECT_EQ(second.laid_out, 0u);
    EXPECT_EQ(second.painted_walked, 0u);
    EXPECT_EQ(third.cascade_visited, 0u);
    EXPECT_EQ(third.laid_out, 0u);
    EXPECT_EQ(third.painted_walked, 0u);
}

TEST(AppInteract, PumpKeystrokeInsertsTheCharacter) {
    BorrowedSession rig;
    ASSERT_TRUE(rig.app.document().load(write_page("zui-keystroke-value.html", typing_html()).string()));
    ASSERT_TRUE(rig.attach());
    const std::string step = type_letter_a(rig);
    ASSERT_TRUE(step.empty()) << step;
    EXPECT_EQ(rig.app.document().get("field", "value"), "a");
}

TEST(AppInteract, PumpKeystrokeRestylesOnlyTheControl) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    BorrowedSession rig;
    ASSERT_TRUE(rig.app.document().load(write_page("zui-keystroke-closure.html", typing_html()).string()));
    ASSERT_TRUE(rig.attach());
    const std::string step = type_letter_a(rig);
    ASSERT_TRUE(step.empty()) << step;
    EXPECT_EQ(zui::perf::last_frame_work().cascade_visited, 1u);
}

TEST(AppInteract, KeyDoesNotInsertText) {
    BorrowedSession rig;
    ASSERT_TRUE(rig.app.document().load(write_page("zui-key-guard.html", typing_html()).string()));
    ASSERT_TRUE(rig.attach());
    ASSERT_TRUE(rig.app.document().focus("field"));
    EXPECT_TRUE(zui::frame::Access::key(rig.app, "a")) << "Interaction is not live";
    EXPECT_EQ(rig.app.document().get("field", "value"), "");
}

TEST(AppInteract, BorrowedSessionRejectsRun) {
    BorrowedSession rig;
    ASSERT_TRUE(rig.app.document().load(write_page("zui-borrowed-run.html", idle_html()).string()));
    ASSERT_TRUE(rig.attach());
    EXPECT_NE(rig.app.run(), 0);
    EXPECT_TRUE(rig.window.poll());
    EXPECT_STREQ(rig.renderer.name(), "record");

    zui::frame::Access::detach(rig.app);
    zui::frame::Access::detach(rig.app);
    EXPECT_TRUE(rig.window.poll());
    EXPECT_STREQ(rig.renderer.name(), "record");
    rig.renderer.present();
    EXPECT_EQ(rig.renderer.present_count, 1);
    int fw = 0;
    int fh = 0;
    rig.window.framebuffer_size(fw, fh);
    EXPECT_EQ(fw, 800);
    EXPECT_EQ(fh, 600);
    EXPECT_EQ(rig.app.document().get("block", "visible"), "true");
    EXPECT_FALSE(rig.app.pump_frame());
}
