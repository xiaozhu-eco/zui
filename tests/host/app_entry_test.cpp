// A translation unit whose only zui include is app.h.

#include <zui/app.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace {

std::filesystem::path write_page() {
    const std::filesystem::path page = std::filesystem::temp_directory_path() / "zui-app-entry.html";
    std::ofstream out(page);
    out << "<!DOCTYPE html><html><body><p id=\"n\">n</p></body></html>\n";
    return page;
}

struct Rows final : zui::ListModel {
    std::size_t row_count() const override { return 1; }
    std::string value(std::size_t, std::string_view) const override { return {}; }
};

}  // namespace

TEST(AppEntry, SessionOwnsOptionalServicesAndValues) {
    zui::App app(0, nullptr);
    const zui::HostServices live = app.host().services();
    EXPECT_TRUE(static_cast<bool>(live));
    const zui::HostServices idle;
    EXPECT_FALSE(static_cast<bool>(idle));

    const zui::plugin::OsResult idle_toast = idle.notify("IdleTitle", "IdleBody");
    EXPECT_FALSE(idle_toast.ok);
    EXPECT_EQ(idle_toast.os_error, 0u);
    EXPECT_TRUE(idle_toast.title.empty());
    EXPECT_TRUE(idle_toast.body.empty());

    const zui::plugin::OsResult toast = live.notify("AppEntry", "Body");
#if defined(_WIN32)
    ASSERT_TRUE(toast.ok) << toast.os_error;
    EXPECT_EQ(toast.title, "AppEntry");
    EXPECT_EQ(toast.body, "Body");
#else
    EXPECT_FALSE(toast.ok);
    EXPECT_EQ(toast.os_error, 0u);
    EXPECT_TRUE(toast.title.empty());
    EXPECT_TRUE(toast.body.empty());
#endif

    zui::plugin::Hotkey hotkey;
    hotkey.key = "F23";
    hotkey.control = true;
    hotkey.alt = true;
    hotkey.shift = true;
    const zui::plugin::OsResult hot = live.global_hotkey(hotkey, [] {});
#if defined(_WIN32)
    ASSERT_TRUE(hot.ok) << hot.os_error << " " << hot.body;
    EXPECT_EQ(hot.title, "ctrl+alt+shift+F23");
#else
    EXPECT_FALSE(hot.ok);
    EXPECT_TRUE(hot.title.empty());
#endif

    const zui::plugin::MediaKey actions[] = {
        zui::plugin::MediaKey::Play, zui::plugin::MediaKey::Next, zui::plugin::MediaKey::Previous};
    const char* titles[] = {"play", "next", "previous"};
    for (int i = 0; i < 3; ++i) {
        const zui::plugin::OsResult media = live.media_key(actions[i], [] {});
#if defined(_WIN32)
        ASSERT_TRUE(media.ok) << titles[i] << " os_error=" << media.os_error;
        EXPECT_EQ(media.title, titles[i]);
#else
        EXPECT_FALSE(media.ok) << titles[i];
        EXPECT_TRUE(media.title.empty()) << titles[i];
#endif
    }

    zui::ClipboardImage pixels;
    pixels.width = 2;
    pixels.height = 2;
    pixels.rgba = {1, 2, 3, 255, 4, 5, 6, 128, 7, 8, 9, 255, 10, 11, 12, 64};
    EXPECT_FALSE(app.host().set_clipboard_image(pixels));
    const zui::ClipboardImage read = app.host().clipboard_image();
    EXPECT_EQ(read.status, zui::ClipboardStatus::Unsupported);
    EXPECT_EQ(read.width, 0);
    EXPECT_EQ(read.height, 0);
    EXPECT_TRUE(read.rgba.empty());

    zui::WindowIcon empty;
    EXPECT_FALSE(empty.valid());
    EXPECT_FALSE(app.window().set_icon(empty));
    zui::WindowIcon icon;
    icon.width = 1;
    icon.height = 1;
    icon.rgba = {9, 8, 7, 255};
    EXPECT_TRUE(icon.valid());
    EXPECT_TRUE(app.window().set_icon(icon));

    EXPECT_FALSE(app.document().measure("n").has_value());
    const std::filesystem::path page = write_page();
    const zui::ModelBind unloaded = app.document().set_model("n", Rows{});
    EXPECT_FALSE(static_cast<bool>(unloaded));
    EXPECT_EQ(unloaded.reason, "no document loaded");

    ASSERT_TRUE(app.document().load(page.string()));
    const zui::ModelBind missing = app.document().set_model("missing", Rows{});
    EXPECT_FALSE(static_cast<bool>(missing));
    EXPECT_EQ(missing.reason, "model host id not found");
    EXPECT_FALSE(app.document().scroll_into_view("missing", zui::ScrollAlign::Start));

    zui::ChildDocument opened = app.window().open_document(page.string(), false);
    ASSERT_TRUE(opened);
    zui::Document* raw = opened.get();
    {
        const zui::ChildDocument copy = opened;
        EXPECT_EQ(copy.get(), raw);
    }
    EXPECT_EQ(opened.get(), raw);
    EXPECT_EQ(opened->get("n", "text"), "n");
    const auto box = opened->measure("n");
    ASSERT_TRUE(box.has_value());
    EXPECT_GE(box->border.width, 0.f);
    EXPECT_FALSE(opened->measure("missing").has_value());
    const zui::ModelBind on_child = opened->set_model("n", Rows{});
    EXPECT_FALSE(static_cast<bool>(on_child));
    EXPECT_EQ(on_child.reason, "child document");
    EXPECT_TRUE(app.window().close_document(opened));
    EXPECT_FALSE(app.window().close_document(opened));
    EXPECT_EQ(app.document().get("n", "text"), "n");

    std::atomic<bool> ran{false};
    std::atomic<bool> was_cancelled{true};
    const zui::TaskId task = app.host().start_task(
        [&](const zui::TaskSignal& signal) {
            signal.set_progress(0.5f);
            was_cancelled.store(signal.cancelled());
            ran.store(true);
        },
        {}, "");
    ASSERT_NE(task, 0u);
    for (int i = 0; i < 50 && !ran.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(ran.load());
    EXPECT_FALSE(was_cancelled.load());

    EXPECT_EQ(app.host().permission_state(zui::Permission::Camera), zui::PermissionState::Unsupported);
    EXPECT_FALSE(app.host().gamepad(0).connected);
    EXPECT_EQ(app.host().gamepad_count(), 0u);
}
