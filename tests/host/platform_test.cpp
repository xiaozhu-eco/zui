#include "backends/host_media.h"
#include "backends/platform.h"
#include "backends/posix_platform_util.h"
#include "backends/renderer.h"
#include "backends/window.h"
#include "fake_window.h"

#include <zui/app.h>
#include <zui/host_types.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

using zui::test::FakeWindow;

namespace {

#if defined(_WIN32)
void set_platform_env(const char* value) {
    _putenv_s("ZUI_PLATFORM", value);
}
#else
void set_platform_env(const char* value) {
    setenv("ZUI_PLATFORM", value, 1);
}
#endif

}  // namespace

// Runtime permissions: a desktop has none, and the mobile kind answers through
// the async seam a device uses. Host::request_permission is the A-tier entry;
// the state is what the app checks before it asks.
TEST(HostPermission, DesktopUnsupportedAndMobileStubAnswers) {
    set_platform_env("");
    zui::App desktop(0, nullptr);
    for (int i = 0; i <= static_cast<int>(zui::Permission::FullScreenIntent); ++i) {
        const auto value = static_cast<zui::Permission>(i);
        EXPECT_EQ(desktop.host().permission_state(value), zui::PermissionState::Unsupported)
            << zui::permission_name(value);
        int calls = 0;
        desktop.host().request_permission(value, [&calls](zui::PermissionState state) {
            ++calls;
            EXPECT_EQ(state, zui::PermissionState::Unsupported);
        });
        EXPECT_EQ(calls, 1) << zui::permission_name(value);
    }

    set_platform_env("android");
    zui::App mobile(0, nullptr);
    zui::backends::mobile_set_next_permission("", zui::PermissionState::Unsupported);
    EXPECT_EQ(mobile.host().permission_state(zui::Permission::Camera), zui::PermissionState::Unsupported);
    zui::backends::mobile_set_next_permission("camera", zui::PermissionState::Granted);
    EXPECT_EQ(mobile.host().permission_state(zui::Permission::Camera), zui::PermissionState::Granted);
    std::vector<zui::PermissionState> got;
    mobile.host().request_permission(zui::Permission::Camera, [&got](zui::PermissionState state) {
        got.push_back(state);
    });
    ASSERT_EQ(got.size(), 1u) << "the mobile answer completes the pending request";
    EXPECT_EQ(got.front(), zui::PermissionState::Granted);
    EXPECT_EQ(mobile.host().permission_state(zui::Permission::Microphone), zui::PermissionState::Unsupported);
    set_platform_env("");
}

TEST(HostPermission, ParkedRequestKeepsTheFirstHandler) {
    set_platform_env("android");
    zui::App mobile(0, nullptr);
    zui::backends::mobile_set_next_permission("", zui::PermissionState::Unsupported);
    EXPECT_EQ(mobile.host().permission_state(zui::Permission::Camera), zui::PermissionState::Unsupported);

    std::vector<zui::PermissionState> first;
    std::vector<zui::PermissionState> second;
    mobile.host().request_permission(zui::Permission::Camera, [&first](zui::PermissionState state) {
        first.push_back(state);
    });
    EXPECT_TRUE(first.empty()) << "the first request stays parked until the OS answers";
    mobile.host().request_permission(zui::Permission::Camera, [&second](zui::PermissionState state) {
        second.push_back(state);
    });
    ASSERT_EQ(second.size(), 1u);
    EXPECT_EQ(second.front(), zui::PermissionState::Unsupported);
    EXPECT_TRUE(first.empty());

    zui::backends::mobile_complete_permission("camera", zui::PermissionState::Denied);
    ASSERT_EQ(first.size(), 1u);
    EXPECT_EQ(first.front(), zui::PermissionState::Denied);
    EXPECT_EQ(second.size(), 1u);
    set_platform_env("");
}

TEST(HostPermission, NameRoundTripCoversTheClosedSet) {
    for (int i = 0; i <= static_cast<int>(zui::Permission::FullScreenIntent); ++i) {
        const auto value = static_cast<zui::Permission>(i);
        const char* name = zui::permission_name(value);
        ASSERT_NE(name, nullptr);
        ASSERT_NE(name[0], '\0') << i;
        zui::Permission parsed{};
        ASSERT_TRUE(zui::permission_parse(name, parsed)) << name;
        EXPECT_EQ(parsed, value) << name;
    }
    zui::Permission missing{};
    EXPECT_FALSE(zui::permission_parse("nope", missing));
    EXPECT_STREQ(zui::permission_name(zui::Permission::StorageWrite), "storage-write");
    EXPECT_STREQ(zui::permission_name(zui::Permission::ManageStorage), "manage-storage");
    EXPECT_STREQ(zui::permission_name(zui::Permission::MediaVisualSelected),
                 "media-visual-selected");
}

TEST(PlatformFactory, NoopPickOpenUnsupported) {
    auto platform = zui::backends::create_platform("noop");
    ASSERT_NE(platform, nullptr);

    const zui::PickPaths paths = platform->pick_open(zui::NativeWindowId{}, {});
    EXPECT_EQ(paths.status, zui::PickStatus::Unsupported);
    EXPECT_TRUE(paths.paths.empty());

    const zui::PickPath save = platform->pick_save(zui::NativeWindowId{}, {});
    EXPECT_EQ(save.status, zui::PickStatus::Unsupported);
    EXPECT_TRUE(save.path.empty());

    const zui::PickPath folder = platform->pick_folder(zui::NativeWindowId{}, {});
    EXPECT_EQ(folder.status, zui::PickStatus::Unsupported);
    EXPECT_TRUE(folder.path.empty());

    EXPECT_FALSE(platform->open_uri("https://example.com"));
}

/// The noop platform's pick seams: the default answer is Unsupported (the headless fallback),
/// and one programmed answer drives the Ok branch of an app's pick flow for exactly one ask.
TEST(PlatformFactory, NoopNextPickAnswersOnce) {
    auto platform = zui::backends::create_platform("noop");
    ASSERT_NE(platform, nullptr);

    zui::backends::noop_set_next_pick_open({zui::PickStatus::Ok, {"docs/a.md"}});
    const zui::PickPaths open = platform->pick_open(zui::NativeWindowId{}, {});
    ASSERT_EQ(open.status, zui::PickStatus::Ok);
    ASSERT_EQ(open.paths.size(), 1u);
    EXPECT_EQ(open.paths.front(), "docs/a.md");
    EXPECT_EQ(platform->pick_open(zui::NativeWindowId{}, {}).status, zui::PickStatus::Unsupported)
        << "the programmed answer was not one-shot";

    zui::backends::noop_set_next_pick_save({zui::PickStatus::Ok, "docs/b.html"});
    const zui::PickPath save = platform->pick_save(zui::NativeWindowId{}, {});
    ASSERT_EQ(save.status, zui::PickStatus::Ok);
    EXPECT_EQ(save.path, "docs/b.html");
    EXPECT_EQ(platform->pick_save(zui::NativeWindowId{}, {}).status, zui::PickStatus::Unsupported);
}

TEST(PlatformFactory, NoopUserDataDirExists) {
    auto platform = zui::backends::create_platform("noop");
    ASSERT_NE(platform, nullptr);

    const std::string dir = platform->user_data_dir("zui-host-test");
    EXPECT_FALSE(dir.empty());
    EXPECT_TRUE(fs::is_directory(dir));

    const std::string cache = platform->cache_dir("zui-host-test");
    EXPECT_FALSE(cache.empty());
    EXPECT_TRUE(fs::is_directory(cache));
}

TEST(AppHost, UserDataDirBeforeRun) {
    set_platform_env("noop");
    char arg0[] = "zui_host_probe";
    char* argv[] = {arg0, nullptr};
    zui::App app(1, argv);
    app.host().set_app_id("zui-host-app");

    const std::string dir = app.host().user_data_dir();
    EXPECT_FALSE(dir.empty());
    EXPECT_TRUE(fs::is_directory(dir));

    const std::string cache = app.host().cache_dir();
    EXPECT_FALSE(cache.empty());
    EXPECT_TRUE(fs::is_directory(cache));
}

TEST(AppHost, PickViaNoopUnsupported) {
    set_platform_env("noop");
    char arg0[] = "zui_host_pick";
    char* argv[] = {arg0, nullptr};
    zui::App app(1, argv);

    const zui::PickPaths open = app.host().pick_open({});
    EXPECT_EQ(open.status, zui::PickStatus::Unsupported);
    EXPECT_TRUE(open.paths.empty());

    const zui::PickPath save = app.host().pick_save({});
    EXPECT_EQ(save.status, zui::PickStatus::Unsupported);
    EXPECT_TRUE(save.path.empty());

    const zui::PickPath folder = app.host().pick_folder({});
    EXPECT_EQ(folder.status, zui::PickStatus::Unsupported);
    EXPECT_TRUE(folder.path.empty());
}

TEST(AppHost, OpenUriViaNoopFalse) {
    set_platform_env("noop");
    char arg0[] = "zui_host_uri";
    char* argv[] = {arg0, nullptr};
    zui::App app(1, argv);
    EXPECT_FALSE(app.host().open_uri("https://example.com"));
    EXPECT_FALSE(app.host().open_uri("mailto:a@b.c"));
}

#if defined(_WIN32)
TEST(PlatformFactory, Win32OpenUriRejectsBadScheme) {
    auto platform = zui::backends::create_platform("win32");
    ASSERT_NE(platform, nullptr);
    EXPECT_FALSE(platform->open_uri("javascript:alert(1)"));
    EXPECT_FALSE(platform->open_uri("vbscript:msgbox"));
    EXPECT_FALSE(platform->open_uri(""));
}
#endif

TEST(PlatformFactory, CrossPlatformKindsResolve) {
    // Wrong-OS factories return NoopPlatform stand-ins so the abstract kinds always resolve.
    auto linux_p = zui::backends::create_platform("linux");
    ASSERT_NE(linux_p, nullptr);
    auto macos_p = zui::backends::create_platform("macos");
    ASSERT_NE(macos_p, nullptr);
    auto native = zui::backends::create_platform("native");
    ASSERT_NE(native, nullptr);

#if defined(__linux__)
    const std::string dir = linux_p->user_data_dir("zui-xp-linux");
    EXPECT_NE(dir.find("zui"), std::string::npos);
    EXPECT_TRUE(fs::is_directory(dir));
    EXPECT_FALSE(linux_p->open_uri("javascript:x"));
#elif defined(__APPLE__)
    const std::string dir = macos_p->user_data_dir("zui-xp-mac");
    EXPECT_NE(dir.find("Application Support"), std::string::npos);
    EXPECT_TRUE(fs::is_directory(dir));
    EXPECT_FALSE(macos_p->open_uri("javascript:x"));
#elif defined(_WIN32)
    // On Windows, linux/macos kinds are Noop stand-ins (dirs under temp).
    EXPECT_EQ(linux_p->pick_open(zui::NativeWindowId{}, {}).status, zui::PickStatus::Unsupported);
    EXPECT_EQ(macos_p->pick_open(zui::NativeWindowId{}, {}).status, zui::PickStatus::Unsupported);
    const std::string dir = native->user_data_dir("zui-xp-win");
    EXPECT_TRUE(fs::is_directory(dir));
#endif
}

TEST(PlatformFactory, MobileIosAndroidStubs) {
    // Mobile kinds always resolve to stubs (even on desktop CI), not Noop stand-ins.
    auto ios = zui::backends::create_platform("ios");
    ASSERT_NE(ios, nullptr);
    const std::string ios_data = ios->user_data_dir("zui-xp-ios");
    EXPECT_TRUE(fs::is_directory(ios_data));
    const std::string ios_cache = ios->cache_dir("zui-xp-ios");
    EXPECT_TRUE(fs::is_directory(ios_cache));
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
    EXPECT_NE(ios_data.find("Application Support"), std::string::npos);
    EXPECT_NE(ios_cache.find("Caches"), std::string::npos);
#else
    EXPECT_NE(ios_data.find("zui-ios-data"), std::string::npos);
    EXPECT_NE(ios_cache.find("zui-ios-cache"), std::string::npos);
#endif
    EXPECT_EQ(ios->pick_open(zui::NativeWindowId{}, {}).status, zui::PickStatus::Unsupported);
    EXPECT_EQ(ios->pick_save(zui::NativeWindowId{}, {}).status, zui::PickStatus::Unsupported);
    EXPECT_EQ(ios->pick_folder(zui::NativeWindowId{}, {}).status, zui::PickStatus::Unsupported);
    EXPECT_FALSE(ios->open_uri("javascript:alert(1)"));
    EXPECT_FALSE(ios->open_uri("https://example.com"));

    auto android = zui::backends::create_platform("android");
    ASSERT_NE(android, nullptr);
    const std::string and_data = android->user_data_dir("zui-xp-and");
    EXPECT_NE(and_data.find("zui-android-data"), std::string::npos);
    EXPECT_TRUE(fs::is_directory(and_data));
    const std::string and_cache = android->cache_dir("zui-xp-and");
    EXPECT_NE(and_cache.find("zui-android-cache"), std::string::npos);
    EXPECT_TRUE(fs::is_directory(and_cache));
    EXPECT_EQ(android->pick_open(zui::NativeWindowId{}, {}).status, zui::PickStatus::Unsupported);
    EXPECT_FALSE(android->open_uri("javascript:alert(1)"));
    EXPECT_FALSE(android->open_uri("https://example.com"));
}

TEST(HostProbe, DialogAndMotionClassifiers) {
    using zui::backends::gnome_enable_animations_is_off;
    using zui::backends::kde_animation_factor_is_off;
    using zui::backends::posix_util::classify_osascript_pick;
    using zui::backends::posix_util::OsascriptPickKind;

    EXPECT_TRUE(gnome_enable_animations_is_off("false"));
    EXPECT_TRUE(gnome_enable_animations_is_off("FALSE\n"));
    EXPECT_FALSE(gnome_enable_animations_is_off("true"));
    EXPECT_FALSE(gnome_enable_animations_is_off(""));

    EXPECT_TRUE(kde_animation_factor_is_off("0"));
    EXPECT_TRUE(kde_animation_factor_is_off("0.0"));
    EXPECT_FALSE(kde_animation_factor_is_off("1"));
    EXPECT_FALSE(kde_animation_factor_is_off(""));
    EXPECT_FALSE(kde_animation_factor_is_off("off"));

    EXPECT_EQ(classify_osascript_pick(0, "/Users/a/note.md"), OsascriptPickKind::Ok);
    EXPECT_EQ(classify_osascript_pick(0, ""), OsascriptPickKind::Cancelled);
    EXPECT_EQ(classify_osascript_pick(1, "execution error: User canceled. (-128)"),
              OsascriptPickKind::Cancelled);
    EXPECT_EQ(classify_osascript_pick(127, "osascript: command not found"),
              OsascriptPickKind::Unsupported);
}

TEST(PlatformFactory, MobilePickInjection) {
    auto ios = zui::backends::create_platform("ios");
    ASSERT_NE(ios, nullptr);
    EXPECT_TRUE(ios->picks_are_async());

    zui::PickPaths injected;
    injected.status = zui::PickStatus::Ok;
    injected.paths = {"/tmp/shared.md"};
    zui::backends::mobile_set_next_pick_open(injected);

    const zui::PickPaths got = ios->pick_open(zui::NativeWindowId{}, {});
    EXPECT_EQ(got.status, zui::PickStatus::Ok);
    ASSERT_EQ(got.paths.size(), 1u);
    EXPECT_EQ(got.paths[0], "/tmp/shared.md");

    // One-shot: next call is Unsupported again.
    EXPECT_EQ(ios->pick_open(zui::NativeWindowId{}, {}).status, zui::PickStatus::Unsupported);

    zui::backends::mobile_set_next_pick_save({zui::PickStatus::Cancelled, {}});
    EXPECT_EQ(ios->pick_save(zui::NativeWindowId{}, {}).status, zui::PickStatus::Cancelled);
    EXPECT_EQ(ios->pick_save(zui::NativeWindowId{}, {}).status, zui::PickStatus::Unsupported);
}

TEST(AppHost, PickOpenAsyncNoopInvokesHandler) {
    set_platform_env("noop");
    char arg0[] = "zui_host_pick_async";
    char* argv[] = {arg0, nullptr};
    zui::App app(1, argv);

    int calls = 0;
    zui::PickStatus status = zui::PickStatus::Ok;
    app.host().pick_open_async({}, [&](zui::PickPaths result) {
        ++calls;
        status = result.status;
        EXPECT_TRUE(result.paths.empty());
    });
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(status, zui::PickStatus::Unsupported);

    // Empty handler is a no-op.
    app.host().pick_open_async({}, {});
    EXPECT_EQ(calls, 1);
}

TEST(AppHost, PickOpenAsyncMobileWaitsForComplete) {
    set_platform_env("ios");
    char arg0[] = "zui_host_pick_async_mobile";
    char* argv[] = {arg0, nullptr};
    zui::App app(1, argv);

    int calls = 0;
    zui::PickPaths got;
    app.host().pick_open_async({}, [&](zui::PickPaths result) {
        ++calls;
        got = std::move(result);
    });
    EXPECT_EQ(calls, 0);

    zui::PickPaths completed;
    completed.status = zui::PickStatus::Ok;
    completed.paths = {"/tmp/async-open.md"};
    zui::backends::mobile_complete_pick_open(std::move(completed));

    EXPECT_EQ(calls, 1);
    EXPECT_EQ(got.status, zui::PickStatus::Ok);
    ASSERT_EQ(got.paths.size(), 1u);
    EXPECT_EQ(got.paths[0], "/tmp/async-open.md");

    // Second complete with no pending handler injects for sync pick_*.
    zui::backends::mobile_complete_pick_save({zui::PickStatus::Cancelled, {}});
    EXPECT_EQ(app.host().pick_save({}).status, zui::PickStatus::Cancelled);
}

TEST(HostMedia, QueryHostMediaSmoke) {
    const zui::HostMediaFacts facts = zui::backends::query_host_media();
    // Values are OS-dependent; only require a defined enum member.
    EXPECT_TRUE(facts.color_scheme == zui::ColorSchemePreference::NoPreference ||
                facts.color_scheme == zui::ColorSchemePreference::Light ||
                facts.color_scheme == zui::ColorSchemePreference::Dark);
}

TEST(HostMedia, WindowsPointerSignalsMapToMediaFacts) {
    using zui::backends::PointerSignals;
    using zui::backends::windows_any_hovers;
    using zui::backends::windows_any_pointer;
    using zui::backends::windows_primary_hovers;
    using zui::backends::windows_primary_pointer;

    // A plain desktop: mouse, no digitizer.
    PointerSignals desktop;
    EXPECT_TRUE(windows_primary_hovers(desktop));
    EXPECT_TRUE(windows_any_hovers(desktop));
    EXPECT_EQ(windows_primary_pointer(desktop), zui::PointerAccuracy::Fine);
    EXPECT_EQ(windows_any_pointer(desktop), zui::PointerAccuracy::Fine);

    // A slate in tablet mode with no mouse: the touchscreen is the primary pointer.
    PointerSignals slate;
    slate.mouse_present = false;
    slate.touch_digitizer = true;
    slate.slate_mode = true;
    EXPECT_FALSE(windows_primary_hovers(slate));
    EXPECT_FALSE(windows_any_hovers(slate));
    EXPECT_EQ(windows_primary_pointer(slate), zui::PointerAccuracy::Coarse);
    EXPECT_EQ(windows_any_pointer(slate), zui::PointerAccuracy::Coarse);

    // Tablet mode with a mouse attached: `pointer: coarse` / `hover: none` for the primary, but
    // `any-pointer: fine` / `any-hover: hover` keep the desktop affordances alive.
    slate.mouse_present = true;
    EXPECT_FALSE(windows_primary_hovers(slate));
    EXPECT_TRUE(windows_any_hovers(slate));
    EXPECT_EQ(windows_primary_pointer(slate), zui::PointerAccuracy::Coarse);
    EXPECT_EQ(windows_any_pointer(slate), zui::PointerAccuracy::Fine);

    // A pen hovers, so it is a fine hovering pointer even with no mouse.
    PointerSignals pen;
    pen.mouse_present = false;
    pen.pen_digitizer = true;
    pen.slate_mode = true;
    EXPECT_TRUE(windows_primary_hovers(pen));
    EXPECT_EQ(windows_primary_pointer(pen), zui::PointerAccuracy::Fine);

    // No pointer hardware at all.
    PointerSignals none;
    none.mouse_present = false;
    EXPECT_EQ(windows_any_pointer(none), zui::PointerAccuracy::None);
    EXPECT_EQ(windows_primary_pointer(none), zui::PointerAccuracy::None);
}

TEST(FakeWindow, DiscardPendingInputClearsEdges) {
    FakeWindow window;
    window.inject_pending_edges();
    const zui::backends::PointerContact* mouse =
        window.contact(zui::backends::PointerKind::Mouse);
    ASSERT_NE(mouse, nullptr);
    ASSERT_TRUE(mouse->pressed);
    ASSERT_FALSE(window.text().key_events.empty());

    window.discard_pending_input();

    EXPECT_EQ(window.discard_calls, 1);
    mouse = window.contact(zui::backends::PointerKind::Mouse);
    ASSERT_NE(mouse, nullptr);
    EXPECT_FALSE(mouse->pressed);
    EXPECT_FALSE(mouse->released);
    EXPECT_FLOAT_EQ(window.pointers().wheel_x, 0.f);
    EXPECT_FLOAT_EQ(window.pointers().wheel_y, 0.f);
    EXPECT_TRUE(window.text().text.empty());
    EXPECT_EQ(window.text().backspace, 0);
    EXPECT_FALSE(window.text().enter);
    EXPECT_TRUE(window.text().key_events.empty());
    // Level state (button still down) is not an edge; FakeWindow leaves it alone.
    EXPECT_TRUE(mouse->down);
}

TEST(FakeWindow, TakeDroppedPathsDrainsQueue) {
    FakeWindow window;
    window.queue_dropped_path("a.db");
    window.queue_dropped_path("b.sqlite");
    const std::vector<std::string> first = window.take_dropped_paths();
    ASSERT_EQ(first.size(), 2u);
    EXPECT_EQ(first[0], "a.db");
    EXPECT_EQ(first[1], "b.sqlite");
    EXPECT_TRUE(window.take_dropped_paths().empty());

    window.queue_dropped_path("c.db");
    window.discard_pending_input();
    EXPECT_TRUE(window.take_dropped_paths().empty());
}

TEST(FakeWindow, InMemoryClipboardRoundTrip) {
    FakeWindow window;
    EXPECT_TRUE(window.set_clipboard_text("hello"));
    const zui::ClipboardText got = window.clipboard_text();
    EXPECT_EQ(got.status, zui::ClipboardStatus::Ok);
    EXPECT_EQ(got.text, "hello");

    EXPECT_TRUE(window.set_clipboard_text(""));
    const zui::ClipboardText empty = window.clipboard_text();
    EXPECT_EQ(empty.status, zui::ClipboardStatus::Ok);
    EXPECT_TRUE(empty.text.empty());

    window.clipboard_ok_ = false;
    EXPECT_FALSE(window.set_clipboard_text("x"));
    EXPECT_EQ(window.clipboard_text().status, zui::ClipboardStatus::Unsupported);
}

TEST(IWindow, DefaultClipboardUnsupported) {
    class BareWindow final : public zui::backends::IWindow {
    public:
        bool create(const zui::backends::WindowDesc&) override { return true; }
        void destroy() override {}
        bool poll() override { return true; }
        bool take_close_request() override { return false; }
        void framebuffer_size(int& width, int& height) const override {
            width = 1;
            height = 1;
        }
        const zui::backends::PointerFrame& pointers() const override { return pointers_; }
        const zui::backends::TextInputFrame& text() const override { return text_; }
        void set_ime(bool) override {}
        const char* name() const override { return "bare"; }
        void* gl_proc(const char*) const override { return nullptr; }
        void gl_make_current() override {}
        void gl_swap() override {}
        bool vulkan_instance_extensions(uint32_t* count, const char* const** names) const override {
            if (count != nullptr) {
                *count = 0;
            }
            if (names != nullptr) {
                *names = nullptr;
            }
            return false;
        }
        bool create_vulkan_surface(void*, void**) override { return false; }

        zui::backends::PointerFrame pointers_{};
        zui::backends::TextInputFrame text_{};
    };

    BareWindow window;
    EXPECT_EQ(window.clipboard_text().status, zui::ClipboardStatus::Unsupported);
    EXPECT_TRUE(window.clipboard_text().text.empty());
    EXPECT_FALSE(window.set_clipboard_text("nope"));
}

TEST(AppHost, ClipboardWithoutWindowUnsupported) {
    set_platform_env("noop");
    char arg0[] = "zui_host_clip";
    char* argv[] = {arg0, nullptr};
    zui::App app(1, argv);

    const zui::ClipboardText clip = app.host().clipboard_text();
    EXPECT_EQ(clip.status, zui::ClipboardStatus::Unsupported);
    EXPECT_TRUE(clip.text.empty());
    EXPECT_FALSE(app.host().set_clipboard_text("paste-me"));
}

TEST(WindowFactory, MobileIosAndroidStubs) {
    auto ios = zui::backends::create_window("ios");
    ASSERT_NE(ios, nullptr);
    EXPECT_STREQ(ios->name(), "ios");

    zui::backends::WindowDesc desc;
    desc.title = "ios-stub";
    desc.width = 390;
    desc.height = 844;
    ASSERT_TRUE(ios->create(desc));
    EXPECT_TRUE(ios->poll());
    int fw = 0;
    int fh = 0;
    ios->framebuffer_size(fw, fh);
    EXPECT_EQ(fw, 390);
    EXPECT_EQ(fh, 844);
    EXPECT_FLOAT_EQ(ios->content_scale(), 1.f);
    const zui::backends::DisplayInsets insets = ios->safe_area_insets();
    EXPECT_FLOAT_EQ(insets.top, 0.f);
    EXPECT_FLOAT_EQ(insets.right, 0.f);
    EXPECT_FLOAT_EQ(insets.bottom, 0.f);
    EXPECT_FLOAT_EQ(insets.left, 0.f);
    EXPECT_FLOAT_EQ(ios->keyboard_insets().bottom, 0.f);
    EXPECT_EQ(ios->visibility(), zui::backends::HostVisibility::Visible);
    EXPECT_FALSE(ios->take_visibility_change());
    // The stub has no OS surface yet: it reports no contacts at all (not "a mouse at 0,0").
    EXPECT_TRUE(ios->pointers().contacts.empty());
    ios->destroy();
    EXPECT_FALSE(ios->poll());

    auto android = zui::backends::create_window("android");
    ASSERT_NE(android, nullptr);
    EXPECT_STREQ(android->name(), "android");
    ASSERT_TRUE(android->create(desc));
    EXPECT_TRUE(android->poll());
    android->framebuffer_size(fw, fh);
    EXPECT_EQ(fw, 390);
    EXPECT_EQ(fh, 844);
    EXPECT_FLOAT_EQ(android->safe_area_insets().top, 0.f);
}

TEST(WindowFactory, MobileStubInsetsTitleSizeVisibility) {
    auto ios = zui::backends::create_window("ios");
    ASSERT_NE(ios, nullptr);
    zui::backends::WindowDesc desc;
    desc.title = "before";
    desc.width = 390;
    desc.height = 844;
    ASSERT_TRUE(ios->create(desc));

    zui::backends::DisplayInsets safe{};
    safe.top = 47.f;
    safe.bottom = 34.f;
    safe.left = 0.f;
    safe.right = 0.f;
    ios->set_safe_area_insets_for_test(safe);
    EXPECT_FLOAT_EQ(ios->safe_area_insets().top, 47.f);
    EXPECT_FLOAT_EQ(ios->safe_area_insets().bottom, 34.f);

    ios->set_keyboard_bottom_for_test(291.f);
    EXPECT_FLOAT_EQ(ios->keyboard_insets().bottom, 291.f);
    EXPECT_FLOAT_EQ(ios->keyboard_insets().top, 0.f);

    ios->set_title("after-title");
    ios->set_window_size(400, 800);
    int fw = 0;
    int fh = 0;
    ios->framebuffer_size(fw, fh);
    EXPECT_EQ(fw, 400);
    EXPECT_EQ(fh, 800);

    EXPECT_FALSE(ios->take_visibility_change());
    ios->set_visibility_for_test(zui::backends::HostVisibility::Hidden);
    EXPECT_EQ(ios->visibility(), zui::backends::HostVisibility::Hidden);
    EXPECT_TRUE(ios->take_visibility_change());
    EXPECT_FALSE(ios->take_visibility_change());
    ios->set_visibility_for_test(zui::backends::HostVisibility::Visible);
    EXPECT_TRUE(ios->take_visibility_change());

    // Orientation seam: create derives Portrait from 390×844; setter / resize edge.
    EXPECT_EQ(ios->orientation(), zui::backends::HostOrientation::Portrait);
    EXPECT_FALSE(ios->take_orientation_change());
    ios->set_orientation_for_test(zui::backends::HostOrientation::Landscape);
    EXPECT_EQ(ios->orientation(), zui::backends::HostOrientation::Landscape);
    EXPECT_TRUE(ios->take_orientation_change());
    EXPECT_FALSE(ios->take_orientation_change());
    ios->set_window_size(900, 400);
    EXPECT_EQ(ios->orientation(), zui::backends::HostOrientation::Landscape);
    ios->set_window_size(400, 900);
    EXPECT_EQ(ios->orientation(), zui::backends::HostOrientation::Portrait);
    EXPECT_TRUE(ios->take_orientation_change());
}

TEST(WindowFactory, MobileInjectOpenedPathsToDropQueue) {
    auto ios = zui::backends::create_window("ios");
    ASSERT_NE(ios, nullptr);
    ASSERT_TRUE(ios->create({}));

    EXPECT_TRUE(ios->take_dropped_paths().empty());

    ios->inject_opened_paths({"share-a.md", "share-b.png"});
    const std::vector<std::string> first = ios->take_dropped_paths();
    ASSERT_EQ(first.size(), 2u);
    EXPECT_EQ(first[0], "share-a.md");
    EXPECT_EQ(first[1], "share-b.png");
    EXPECT_TRUE(ios->take_dropped_paths().empty());

    ios->inject_opened_paths({"/open-with/note.txt"});
    ios->discard_pending_input();
    EXPECT_TRUE(ios->take_dropped_paths().empty());

    auto android = zui::backends::create_window("android");
    ASSERT_NE(android, nullptr);
    ASSERT_TRUE(android->create({}));
    android->inject_opened_paths({"intent://file"});
    const std::vector<std::string> and_paths = android->take_dropped_paths();
    ASSERT_EQ(and_paths.size(), 1u);
    EXPECT_EQ(and_paths[0], "intent://file");
}

TEST(PointerFrame, FakeWindowPublishesTheStagedMouseContact) {
    FakeWindow window;
    ASSERT_TRUE(window.create({}));
    ASSERT_TRUE(window.poll());
    ASSERT_EQ(window.pointers().contacts.size(), 1u);
    const zui::backends::PointerContact& mouse = window.pointers().contacts[0];
    EXPECT_EQ(mouse.kind, zui::backends::PointerKind::Mouse);
    EXPECT_EQ(mouse.id, 1);
    EXPECT_TRUE(mouse.primary);
    EXPECT_FALSE(mouse.pressed);
    EXPECT_FLOAT_EQ(window.safe_area_insets().top, 0.f);
}

TEST(AppHost, PumpFrameWithoutSessionReturnsFalse) {
    set_platform_env("noop");
    char arg0[] = "zui_host_pump";
    char* argv[] = {arg0, nullptr};
    zui::App app(1, argv);
    // No run() setup → no loop_window/renderer/interaction session.
    EXPECT_FALSE(app.pump_frame());
}

TEST(SdlWindow, HiddenStaysOneToOneAndVisibleMatchesContentScale) {
    auto hidden = zui::backends::create_window("sdl3");
    if (hidden == nullptr) {
        GTEST_SKIP() << "SDL window backend is not built";
    }
    zui::backends::WindowDesc desc;
    desc.title = "zui hidpi hidden";
    desc.width = 400;
    desc.height = 300;
    desc.hidden = true;
    desc.api = zui::backends::GfxApi::Vulkan;
    ASSERT_TRUE(hidden->create(desc)) << "hidden SDL window did not create";
    int fw = 0;
    int fh = 0;
    hidden->framebuffer_size(fw, fh);
    EXPECT_EQ(fw, 400);
    EXPECT_EQ(fh, 300);
    EXPECT_NEAR(hidden->content_scale(), 1.f, 0.02f);
    hidden->destroy();

    auto shown = zui::backends::create_window("sdl3");
    ASSERT_NE(shown, nullptr);
    desc.title = "zui hidpi";
    desc.hidden = false;
    ASSERT_TRUE(shown->create(desc)) << "visible SDL window did not create";
    const float scale = shown->content_scale();
    shown->framebuffer_size(fw, fh);
    EXPECT_GE(scale, 1.f);
    EXPECT_NEAR(static_cast<float>(fw), 400.f * scale, 2.f);
    EXPECT_NEAR(static_cast<float>(fh), 300.f * scale, 2.f);
    shown->set_window_size(200, 150);
    shown->framebuffer_size(fw, fh);
    const float resized = shown->content_scale();
    EXPECT_NEAR(static_cast<float>(fw), 200.f * resized, 2.f);
    EXPECT_NEAR(static_cast<float>(fh), 150.f * resized, 2.f);
    shown->destroy();
}
