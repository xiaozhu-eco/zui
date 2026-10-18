#include "app/frame_session.h"
#include "backends/sdl_finger_test.h"
#include "fake_window.h"
#include "recording_renderer.h"

#include <zui/app.h>
#include <zui/event.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path write_page(const std::string& html) {
    const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
    const std::string tag = info != nullptr ? info->name() : "no-test";
    const fs::path page = fs::temp_directory_path() / ("zui-window-chrome-" + tag + ".html");
    std::ofstream out(page);
    out << html;
    return page;
}

zui::WindowIcon icon_rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    zui::WindowIcon icon;
    icon.width = 2;
    icon.height = 1;
    icon.rgba = {r, g, b, 255, r, g, b, 128};
    return icon;
}

/// True when a non-comment line declares `name(`. Comments that name the seam do not count.
bool declares_call(const fs::path& header, std::string_view name) {
    std::ifstream in(header);
    if (!in) {
        return true;
    }
    std::string line;
    bool in_block = false;
    while (std::getline(in, line)) {
        std::string code;
        code.reserve(line.size());
        for (std::size_t i = 0; i < line.size();) {
            if (in_block) {
                const auto end = line.find("*/", i);
                if (end == std::string::npos) {
                    break;
                }
                in_block = false;
                i = end + 2;
                continue;
            }
            if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/') {
                break;
            }
            if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '*') {
                in_block = true;
                i += 2;
                continue;
            }
            code.push_back(line[i]);
            ++i;
        }
        for (std::size_t pos = 0; (pos = code.find(name, pos)) != std::string::npos;) {
            if (pos > 0) {
                const unsigned char prev = static_cast<unsigned char>(code[pos - 1]);
                if (std::isalnum(prev) || code[pos - 1] == '_') {
                    pos += name.size();
                    continue;
                }
            }
            std::size_t j = pos + name.size();
            while (j < code.size() && (code[j] == ' ' || code[j] == '\t')) {
                ++j;
            }
            if (j < code.size() && code[j] == '(') {
                return true;
            }
            pos += name.size();
        }
    }
    return false;
}

}  // namespace

TEST(WindowChrome, NoWindowRejectsChromeAndQueuesIcon) {
    zui::App app(0, nullptr);
    EXPECT_FALSE(app.window().minimize());
    EXPECT_FALSE(app.window().set_maximized(true));
    EXPECT_FALSE(app.window().set_fullscreen(true));
    EXPECT_FALSE(app.window().set_topmost(true));
    EXPECT_FALSE(app.window().minimized());
    EXPECT_FALSE(app.window().display().known);
    EXPECT_FALSE(app.window().set_icon(zui::WindowIcon{}));
    EXPECT_TRUE(app.window().set_icon(icon_rgba(1, 2, 3)));
    EXPECT_EQ(app.host().start_task({}, [] {}, "job"), 0u);
    EXPECT_FALSE(app.host().cancel_task(0));
}

TEST(WindowChrome, IconChromeDisplayRoundTripOnTheOneWindow) {
    const fs::path page = write_page(
        "<!DOCTYPE html><html><body><progress id=\"job\" max=\"1\" value=\"0\"></progress>"
        "</body></html>\n");
    zui::test::FakeWindow window;
    zui::test::FakeWindow other;
    zui::test::RecordingRenderer renderer;
    zui::App app(0, nullptr);
    struct Detach {
        zui::App& app;
        ~Detach() { zui::frame::Access::detach(app); }
    } detach{app};

    ASSERT_TRUE(app.document().load(page.string()));
    const zui::WindowIcon queued = icon_rgba(9, 8, 7);
    ASSERT_TRUE(app.window().set_icon(queued));

    zui::frame::Session session;
    session.window = &window;
    session.renderer = &renderer;
    ASSERT_TRUE(zui::frame::Access::attach(app, session));
    ASSERT_TRUE(window.has_icon_);
    EXPECT_EQ(window.icon_.rgba, queued.rgba);
    EXPECT_FALSE(other.has_icon_);

    const zui::WindowIcon replaced = icon_rgba(4, 5, 6);
    ASSERT_TRUE(app.window().set_icon(replaced));
    EXPECT_EQ(window.icon_.width, 2);
    EXPECT_EQ(window.icon_.height, 1);
    EXPECT_EQ(window.icon_.rgba, replaced.rgba);
    EXPECT_FALSE(other.has_icon_);

    ASSERT_TRUE(app.window().minimize());
    EXPECT_TRUE(window.minimized_);
    EXPECT_TRUE(app.window().minimized());
    EXPECT_FALSE(other.minimized_);

    ASSERT_TRUE(app.window().set_maximized(true));
    EXPECT_TRUE(window.maximized_);
    EXPECT_TRUE(app.window().maximized());
    EXPECT_FALSE(window.minimized_);
    EXPECT_FALSE(app.window().minimized());
    EXPECT_FALSE(other.maximized_);

    ASSERT_TRUE(app.window().set_maximized(false));
    EXPECT_FALSE(window.maximized_);
    EXPECT_FALSE(app.window().maximized());

    ASSERT_TRUE(app.window().set_fullscreen(true));
    EXPECT_TRUE(window.fullscreen_);
    EXPECT_TRUE(app.window().fullscreen());
    ASSERT_TRUE(app.window().set_fullscreen(false));
    EXPECT_FALSE(window.fullscreen_);
    EXPECT_FALSE(app.window().fullscreen());
    EXPECT_FALSE(other.fullscreen_);

    ASSERT_TRUE(app.window().set_topmost(true));
    EXPECT_TRUE(window.topmost_);
    EXPECT_TRUE(app.window().topmost());
    ASSERT_TRUE(app.window().set_topmost(false));
    EXPECT_FALSE(window.topmost_);
    EXPECT_FALSE(app.window().topmost());
    EXPECT_FALSE(other.topmost_);

    window.display_.known = true;
    window.display_.id = "DISPLAY1";
    window.display_.index = 0;
    window.display_.width_px = 1920;
    window.display_.height_px = 1080;
    window.display_.scale = 1.25f;
    const zui::DisplayIdentity shown = app.window().display();
    EXPECT_TRUE(shown.known);
    EXPECT_EQ(shown.id, "DISPLAY1");
    EXPECT_EQ(shown.index, 0);
    EXPECT_EQ(shown.width_px, 1920);
    EXPECT_EQ(shown.height_px, 1080);
    EXPECT_FLOAT_EQ(shown.scale, 1.25f);
    EXPECT_FALSE(other.display().known);

    // The one close path and the one geometry path are unchanged.
    EXPECT_FALSE(window.take_close_request());
    EXPECT_TRUE(app.window().set_window_size(640, 480));

    int finished = 0;
    const zui::TaskId done = app.host().start_task([](const zui::TaskSignal&) {}, [&] { ++finished; }, "job");
    ASSERT_NE(done, 0u);
    for (int i = 0; i < 40 && finished == 0; ++i) {
        ASSERT_TRUE(app.pump_frame());
    }
    EXPECT_EQ(finished, 1);

    int cancelled_done = 0;
    std::atomic<bool> stopped{false};
    const zui::TaskId running = app.host().start_task(
        [&](const zui::TaskSignal& signal) {
            signal.set_progress(0.4f);
            while (!signal.cancelled()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            stopped.store(true);
        },
        [&] { ++cancelled_done; }, "job");
    ASSERT_NE(running, 0u);
    bool saw_progress = false;
    for (int i = 0; i < 40; ++i) {
        ASSERT_TRUE(app.pump_frame());
        if (app.document().get("job", "value") == "0.4") {
            saw_progress = true;
            break;
        }
    }
    EXPECT_TRUE(saw_progress);
    EXPECT_FALSE(stopped.load());
    ASSERT_TRUE(app.host().cancel_task(running));
    EXPECT_TRUE(stopped.load());
    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(app.pump_frame());
    }
    EXPECT_EQ(cancelled_done, 0);
    EXPECT_EQ(finished, 1);
    EXPECT_EQ(app.document().get("job", "value"), "0.4");
    EXPECT_FALSE(app.host().cancel_task(running));
}

TEST(WindowChrome, MultiWindowAndPrintStayASeam) {
    const fs::path app_header = fs::path(ZUI_SOURCE_DIR) / "include" / "zui" / "app.h";
    const fs::path window_header = fs::path(ZUI_SOURCE_DIR) / "source" / "backends" / "window.h";
    for (const char* name : {"print", "open_window", "print_pdf", "export_pdf"}) {
        EXPECT_FALSE(declares_call(app_header, name)) << name;
        EXPECT_FALSE(declares_call(window_header, name)) << name;
    }

    const fs::path page = write_page("<!DOCTYPE html><html><body></body></html>\n");
    zui::test::FakeWindow window;
    zui::test::FakeWindow other;
    zui::test::RecordingRenderer renderer;
    zui::App app(0, nullptr);
    struct Detach {
        zui::App& app;
        ~Detach() { zui::frame::Access::detach(app); }
    } detach{app};
    ASSERT_TRUE(app.document().load(page.string()));
    zui::frame::Session session;
    session.window = &window;
    session.renderer = &renderer;
    ASSERT_TRUE(zui::frame::Access::attach(app, session));

    ASSERT_TRUE(app.window().set_icon(icon_rgba(3, 3, 3)));
    ASSERT_TRUE(app.window().minimize());
    EXPECT_TRUE(window.minimized_);
    EXPECT_FALSE(other.minimized_);
    ASSERT_TRUE(app.window().set_maximized(true));
    EXPECT_TRUE(window.maximized_);
    EXPECT_FALSE(window.minimized_);
    EXPECT_FALSE(other.maximized_);
    ASSERT_TRUE(app.window().set_fullscreen(true));
    EXPECT_TRUE(window.fullscreen_);
    EXPECT_FALSE(other.fullscreen_);
    ASSERT_TRUE(app.window().set_topmost(true));
    EXPECT_TRUE(window.topmost_);
    EXPECT_FALSE(other.topmost_);
    EXPECT_TRUE(window.has_icon_);
    EXPECT_FALSE(other.has_icon_);
    EXPECT_FALSE(other.display().known);
    EXPECT_FALSE(window.take_close_request());
    EXPECT_TRUE(app.window().set_window_size(320, 240));
}

TEST(WindowChrome, EventPayloadShapeStaysFrozen) {
    zui::Key key;
    key.composing = false;
    zui::Wheel wheel;
    wheel.delta_y = 1.f;
    zui::Drop drop;
    EXPECT_TRUE(drop.path(0).empty());
    zui::Pointer pointer;
    pointer.pointer_id = 1;
    pointer.is_primary = true;
    zui::Drag drag;
    drag.effect = zui::DropEffect::None;
    EXPECT_FALSE(key.composing);
    EXPECT_FLOAT_EQ(wheel.delta_y, 1.f);
    EXPECT_EQ(pointer.pointer_id, 1);
    EXPECT_EQ(drag.effect, zui::DropEffect::None);
}

TEST(SdlWindow, VisibilityAndOrientationReachTheAuthor) {
    auto window = zui::backends::create_window("sdl3");
    if (window == nullptr) {
        GTEST_SKIP() << "SDL window backend is not built";
    }
    zui::backends::WindowDesc desc;
    desc.title = "zui lifecycle";
    desc.width = 400;
    desc.height = 200;
    desc.hidden = false;
    desc.api = zui::backends::GfxApi::Vulkan;
    ASSERT_TRUE(window->create(desc));
    EXPECT_EQ(window->visibility(), zui::backends::HostVisibility::Visible);
    EXPECT_FALSE(window->take_visibility_change());
    EXPECT_EQ(window->orientation(), zui::backends::HostOrientation::Landscape);
    EXPECT_FALSE(window->take_orientation_change());

    const fs::path page = write_page(
        "<!DOCTYPE html><html onvisibilitychange=\"on_vis\" onorientationchange=\"on_ori\">"
        "<body></body></html>\n");
    zui::test::RecordingRenderer renderer;
    zui::App app(0, nullptr);
    struct Detach {
        zui::App& app;
        ~Detach() { zui::frame::Access::detach(app); }
    } detach{app};
    ASSERT_TRUE(app.document().load(page.string()));
    int vis = 0;
    int ori = 0;
    app.document().bind("on_vis", [&](zui::Event& e) {
        if (e.type() == "visibilitychange") {
            ++vis;
        }
    });
    app.document().bind("on_ori", [&](zui::Event& e) {
        if (e.type() == "orientationchange") {
            ++ori;
        }
    });
    zui::frame::Session session;
    session.window = window.get();
    session.renderer = &renderer;
    ASSERT_TRUE(zui::frame::Access::attach(app, session));
    ASSERT_TRUE(app.pump_frame());
    vis = 0;
    ori = 0;

    window->set_window_size(180, 420);
    bool saw_ori = false;
    for (int i = 0; i < 10 && !saw_ori; ++i) {
        ASSERT_TRUE(app.pump_frame());
        saw_ori = ori > 0 && window->orientation() == zui::backends::HostOrientation::Portrait;
    }
    EXPECT_TRUE(saw_ori);
    EXPECT_GT(ori, 0);

    // The A-tier read the app uses to answer `visibilitychange`: the event says the answer
    // moved, `Window::visible()` is the answer. The real SDL window reports Visible here and
    // only the next line's minimize takes it away.
    EXPECT_TRUE(app.window().visible());
    ASSERT_TRUE(window->minimize());
    bool saw_vis = false;
    for (int i = 0; i < 10 && !saw_vis; ++i) {
        ASSERT_TRUE(app.pump_frame());
        saw_vis = vis > 0 && window->visibility() == zui::backends::HostVisibility::Hidden;
    }
    EXPECT_TRUE(saw_vis);
    EXPECT_GT(vis, 0);
    EXPECT_FALSE(app.window().visible());
    window->destroy();
}

TEST(SdlWindow, HiddenWindowAcceptsIconAndReportsDisplay) {
    auto hidden = zui::backends::create_window("sdl3");
    if (hidden == nullptr) {
        GTEST_SKIP() << "SDL window backend is not built";
    }
    zui::backends::WindowDesc desc;
    desc.title = "zui icon";
    desc.width = 200;
    desc.height = 150;
    desc.hidden = true;
    desc.api = zui::backends::GfxApi::Vulkan;
    ASSERT_TRUE(hidden->create(desc));
    EXPECT_TRUE(hidden->set_icon(icon_rgba(12, 34, 56)));
    EXPECT_FALSE(hidden->set_icon(zui::WindowIcon{}));
    const zui::DisplayIdentity shown = hidden->display();
    EXPECT_TRUE(shown.known);
    EXPECT_FALSE(shown.id.empty());
    EXPECT_GE(shown.index, 0);
    EXPECT_GT(shown.width_px, 0);
    EXPECT_GT(shown.height_px, 0);
    hidden->destroy();
}

TEST(WindowChrome, CancelledTaskObservesCancelAndSkipsCompletion) {
    const fs::path page = write_page(
        "<!DOCTYPE html><html><body><progress id=\"job\" max=\"1\" value=\"0\"></progress>"
        "</body></html>\n");
    zui::test::FakeWindow window;
    zui::test::RecordingRenderer renderer;
    zui::App app(0, nullptr);
    struct Detach {
        zui::App& app;
        ~Detach() { zui::frame::Access::detach(app); }
    } detach{app};
    ASSERT_TRUE(app.document().load(page.string()));
    zui::frame::Session session;
    session.window = &window;
    session.renderer = &renderer;
    ASSERT_TRUE(zui::frame::Access::attach(app, session));

    int finished = 0;
    const zui::TaskId done = app.host().start_task([](const zui::TaskSignal&) {}, [&] { ++finished; }, "");
    ASSERT_NE(done, 0u);
    for (int i = 0; i < 40 && finished == 0; ++i) {
        ASSERT_TRUE(app.pump_frame());
    }
    EXPECT_EQ(finished, 1);

    int cancelled_done = 0;
    std::atomic<bool> observed{false};
    const zui::TaskId running = app.host().start_task(
        [&](const zui::TaskSignal& signal) {
            signal.set_progress(0.25f);
            while (!signal.cancelled()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            observed.store(signal.cancelled());
        },
        [&] { ++cancelled_done; }, "job");
    ASSERT_NE(running, 0u);
    bool saw_progress = false;
    for (int i = 0; i < 40 && !saw_progress; ++i) {
        ASSERT_TRUE(app.pump_frame());
        saw_progress = app.document().get("job", "value") == "0.25";
    }
    EXPECT_TRUE(saw_progress);
    EXPECT_FALSE(observed.load());
    EXPECT_EQ(cancelled_done, 0);
    ASSERT_TRUE(app.host().cancel_task(running));
    EXPECT_TRUE(observed.load());
    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(app.pump_frame());
    }
    EXPECT_EQ(cancelled_done, 0);
    EXPECT_EQ(finished, 1);
    EXPECT_EQ(app.document().get("job", "value"), "0.25");
    EXPECT_FALSE(app.host().cancel_task(running));
    EXPECT_FALSE(app.host().cancel_task(0));
}

TEST(SdlWindow, PushedFingersReachTheAuthor) {
    auto window = zui::backends::create_window("sdl3");
    if (window == nullptr) {
        GTEST_SKIP() << "SDL window backend is not built";
    }
    zui::backends::WindowDesc desc;
    desc.title = "zui fingers";
    desc.width = 400;
    desc.height = 300;
    desc.hidden = true;
    desc.api = zui::backends::GfxApi::Vulkan;
    ASSERT_TRUE(window->create(desc));
    struct Destroy {
        zui::backends::IWindow& window;
        ~Destroy() { window.destroy(); }
    } destroy{*window};

    const fs::path page = write_page(
        "<!DOCTYPE html><html><head><style>"
        "html,body{margin:0;width:100%;height:100%}"
        "#pad{position:absolute;left:0;top:0;width:100%;height:100%}"
        "</style></head><body onpointerdown=\"on_down\"><div id=\"pad\"></div>"
        "</body></html>\n");
    zui::test::RecordingRenderer renderer;
    zui::App app(0, nullptr);
    struct Detach {
        zui::App& app;
        ~Detach() { zui::frame::Access::detach(app); }
    } detach{app};
    ASSERT_TRUE(app.document().load(page.string()));
    struct Down {
        std::int32_t id = -1;
        zui::PointerType type = zui::PointerType::Mouse;
        float pressure = -1.f;
    };
    std::vector<Down> downs;
    app.document().bind("on_down", [&](zui::Event& event) {
        if (const zui::Pointer* pointer = event.is_pointer()) {
            downs.push_back(Down{pointer->pointer_id, pointer->pointer_type, pointer->pressure});
        }
    });
    zui::frame::Session session;
    session.window = window.get();
    session.renderer = &renderer;
    ASSERT_TRUE(zui::frame::Access::attach(app, session));
    ASSERT_TRUE(app.pump_frame());
    downs.clear();

    ASSERT_TRUE(zui::backends::push_finger_for_test(*window, true, 0.25f, 0.25f, 11, 0.35f));
    ASSERT_TRUE(zui::backends::push_finger_for_test(*window, true, 0.75f, 0.75f, 12, 0.8f));
    ASSERT_TRUE(app.pump_frame());

    auto find_down = [&](std::int32_t id) -> const Down* {
        for (const Down& down : downs) {
            if (down.id == id) {
                return &down;
            }
        }
        return nullptr;
    };
    const Down* first = find_down(11);
    const Down* second = find_down(12);
    ASSERT_NE(first, nullptr) << "finger 11 did not reach the author";
    ASSERT_NE(second, nullptr) << "finger 12 did not reach the author";
    EXPECT_EQ(first->type, zui::PointerType::Touch);
    EXPECT_EQ(second->type, zui::PointerType::Touch);
    EXPECT_NEAR(first->pressure, 0.35f, 0.01f);
    EXPECT_NEAR(second->pressure, 0.8f, 0.01f);
}
