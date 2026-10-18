// Opt-in platform services, image clipboard, IME caret, child windows, and diagnostics.
// Drives App and zui::plugin. Crash, second-instance, and startup probes exit before gtest.

#include "app/frame_session.h"
#include "backends/ime_composer.h"
#include "backends/renderer.h"
#include "backends/win32/clipboard_image.h"
#include "ext/plugin_export.h"
#include "ext/slots.h"
#include "fake_window.h"
#include "plugins/android_dex.h"
#include "plugins/clipboard/bmp.h"
#include "plugins/association/record.h"
#include "plugins/association/text.h"
#include "plugins/hotkey/chord.h"
#include "plugins/instance/endpoint.h"
#include "plugins/pick/command.h"
#include "plugins/shell/layout.h"
#include "plugins/tray/item.h"
#include "recording_renderer.h"

#include <zui/app.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <imm.h>
#include <shellapi.h>
#endif

namespace fs = std::filesystem;

namespace {

fs::path page_file(const std::string& name, const std::string& html) {
    const fs::path path = fs::temp_directory_path() / name;
    std::ofstream out(path);
    out << html;
    return path;
}

void note_os(const char* what, unsigned long code) {
    const char* env = std::getenv("ZUI_PLUGIN_OS_LOG");
    if (env == nullptr || env[0] == '\0') {
        return;
    }
    std::ofstream out(env, std::ios::app);
    out << what << " os_error=" << code << "\n";
}

zui::HostServices session_services() {
    static zui::App app(0, nullptr);
    return app.host().services();
}

zui::plugin::OpenerSpec opener_query(std::string extension, std::string mime = {}, std::string scheme = {},
                                     std::string prog_id = {}) {
    zui::plugin::OpenerSpec spec;
    spec.extension = std::move(extension);
    spec.mime = std::move(mime);
    spec.scheme = std::move(scheme);
    spec.prog_id = std::move(prog_id);
    return spec;
}

zui::plugin::FileAssociation file_assoc(std::string extension, std::string prog_id, std::string executable) {
    zui::plugin::FileAssociation spec;
    spec.extension = std::move(extension);
    spec.prog_id = std::move(prog_id);
    spec.executable = std::move(executable);
    return spec;
}

zui::plugin::SchemeAssociation scheme_assoc(std::string scheme, std::string prog_id, std::string executable) {
    zui::plugin::SchemeAssociation spec;
    spec.scheme = std::move(scheme);
    spec.prog_id = std::move(prog_id);
    spec.executable = std::move(executable);
    return spec;
}

#if defined(_WIN32)

std::wstring exe_path() {
    wchar_t buffer[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return n > 0 ? std::wstring(buffer, buffer + n) : std::wstring{};
}

std::string narrow(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                      nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n,
                        nullptr, nullptr);
    return out;
}

std::wstring widen(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                      nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n);
    return out;
}

DWORD run_command(const std::wstring& command) {
    STARTUPINFOW start{};
    start.cb = sizeof(start);
    PROCESS_INFORMATION process{};
    std::wstring mutable_command = command;
    if (!CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &start, &process)) {
        return static_cast<DWORD>(-1);
    }
    WaitForSingleObject(process.hProcess, 20000);
    DWORD code = static_cast<DWORD>(-1);
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return code;
}

void pump_messages() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void cancel_file_dialog(const wchar_t* title) {
    for (int i = 0; i < 40; ++i) {
        HWND dialog = FindWindowW(L"#32770", title);
        if (dialog != nullptr) {
            DWORD pid = 0;
            GetWindowThreadProcessId(dialog, &pid);
            if (pid == GetCurrentProcessId()) {
                const HWND cancel = GetDlgItem(dialog, IDCANCEL);
                PostMessageW(dialog, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED),
                             reinterpret_cast<LPARAM>(cancel));
                PostMessageW(dialog, WM_CLOSE, 0, 0);
                return;
            }
        }
        Sleep(50);
    }
}

std::wstring reg_sz(const std::wstring& key) {
    wchar_t buffer[2048];
    DWORD bytes = sizeof(buffer);
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, buffer,
                     &bytes) != ERROR_SUCCESS ||
        bytes < sizeof(wchar_t)) {
        return {};
    }
    return std::wstring(buffer, buffer + (bytes / sizeof(wchar_t)) - 1);
}

bool reg_missing(const std::wstring& key) {
    HKEY hive = nullptr;
    const LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_READ, &hive);
    if (rc == ERROR_SUCCESS) {
        RegCloseKey(hive);
        return false;
    }
    return true;
}

std::wstring reg_named(const std::wstring& key, const std::wstring& name) {
    wchar_t buffer[2048];
    DWORD bytes = sizeof(buffer);
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), name.c_str(), RRF_RT_REG_SZ, nullptr, buffer, &bytes) !=
            ERROR_SUCCESS ||
        bytes < sizeof(wchar_t)) {
        return {};
    }
    return std::wstring(buffer, buffer + (bytes / sizeof(wchar_t)) - 1);
}

bool reg_value_present(const std::wstring& key, const std::wstring& name) {
    HKEY hive = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_READ, &hive) != ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0;
    DWORD bytes = 0;
    const LONG rc = RegQueryValueExW(hive, name.c_str(), nullptr, &type, nullptr, &bytes);
    RegCloseKey(hive);
    return rc == ERROR_SUCCESS;
}

bool reg_missing_value(const std::wstring& key, const std::wstring& name) {
    return !reg_value_present(key, name);
}

struct ProbeArgs {
    ProbeArgs() {
        int argc = 0;
        wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv == nullptr) {
            return;
        }
        if (const char* probe_out = std::getenv("ZUI_OPENER_PROBE_OUT");
            probe_out != nullptr && probe_out[0] != '\0') {
            std::ofstream out(probe_out, std::ios::out | std::ios::trunc);
            for (int i = 0; i < argc; ++i) {
                out << narrow(argv[i]) << "\n";
            }
            LocalFree(argv);
            ExitProcess(out ? 0 : 6);
        }
        for (int i = 1; i + 1 < argc; ++i) {
            if (wcscmp(argv[i], L"--zui-crash-probe") == 0) {
                const std::string path = narrow(argv[i + 1]);
                LocalFree(argv);
                zui::App app(0, nullptr);
                if (!app.enable_diagnostics(path)) {
                    ExitProcess(3);
                }
                volatile int* fault = nullptr;
                *fault = 1;
                ExitProcess(4);
            }
            if (wcscmp(argv[i], L"--zui-second") == 0 && i + 2 < argc) {
                const std::string identity = narrow(argv[i + 1]);
                const std::string args = narrow(argv[i + 2]);
                LocalFree(argv);
                // This constructor runs during static init, which may be before
                // plugin_boot.cpp. Register the instance slot before using it.
                zui::zui_plugin_instance_register(zui::ExtensionRegistry::instance());
                zui::App app(0, nullptr);
                const zui::HostServices services = app.host().services();
                if (services.claim_single_instance(identity)) {
                    ExitProcess(2);
                }
                const bool sent = services.signal_primary(identity, args);
                ExitProcess(sent ? 0 : 5);
            }
            if (wcscmp(argv[i], L"--zui-startup-probe") == 0 && i + 2 < argc) {
                const std::string out_path = narrow(argv[i + 1]);
                const std::string payload = narrow(argv[i + 2]);
                std::vector<std::string> owned;
                owned.reserve(static_cast<std::size_t>(argc));
                for (int j = 0; j < argc; ++j) {
                    owned.push_back(narrow(argv[j]));
                }
                LocalFree(argv);
                std::vector<char*> pointers;
                pointers.reserve(owned.size());
                for (std::string& item : owned) {
                    pointers.push_back(item.data());
                }
                zui::App app(static_cast<int>(pointers.size()), pointers.data());
                std::ofstream out(out_path, std::ios::out | std::ios::trunc);
                out << payload << "\n";
                for (const std::string& item : owned) {
                    out << item << "\n";
                }
                (void)app;
                ExitProcess(out ? 0 : 6);
            }
        }
        LocalFree(argv);
    }
};

ProbeArgs g_probes;

#endif

struct IdTextRows : zui::ListModel {
    std::vector<std::pair<std::string, std::string>> rows;
    std::size_t row_count() const override { return rows.size(); }
    std::string value(std::size_t row, std::string_view key) const override {
        if (key == "id") {
            return rows[row].first;
        }
        if (key == "text") {
            return rows[row].second;
        }
        return {};
    }
    std::string row_key(std::size_t row) const override { return rows[row].first; }
};

}  // namespace

TEST(HostServices, DataAndAriaRoundTrip) {
    const fs::path page = page_file("zui-host-data.html",
                                    "<!DOCTYPE html><html><body><p id=\"n\">n</p></body></html>\n");
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string()));
    ASSERT_TRUE(app.document().set("n", "data-kind", "row"));
    ASSERT_TRUE(app.document().set("n", "aria-label", "Name"));
    EXPECT_EQ(app.document().get("n", "data-kind"), "row");
    EXPECT_EQ(app.document().get("n", "aria-label"), "Name");
    EXPECT_FALSE(app.document().set("n", "style", "color:red"));
    EXPECT_FALSE(app.document().set("n", "title", "nope"));
    EXPECT_FALSE(app.document().set("n", "data-", "x"));
    EXPECT_FALSE(app.document().set("n", "aria-", "x"));
    EXPECT_EQ(app.document().get("n", "text"), "n");
}

TEST(HostServices, WatchFiresForThatNodeOnly) {
    const fs::path page = page_file(
        "zui-host-watch.html",
        "<!DOCTYPE html><html><body><ul id=\"host\"><template><li id=\"{{id}}\">{{text}}</li>"
        "</template></ul></body></html>\n");
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string()));
    IdTextRows model;
    model.rows = {{"a", "one"}, {"b", "two"}};
    ASSERT_TRUE(app.document().set_model("host", model));
    int a_hits = 0;
    int b_hits = 0;
    const zui::Document::WatchId watch_a = app.document().watch("a", [&](std::string_view) { ++a_hits; });
    const zui::Document::WatchId watch_b = app.document().watch("b", [&](std::string_view) { ++b_hits; });
    ASSERT_NE(watch_a, 0u);
    ASSERT_NE(watch_b, 0u);
    ASSERT_TRUE(app.document().set_model("host", model));
    EXPECT_EQ(a_hits, 0);
    EXPECT_EQ(b_hits, 0);
    EXPECT_EQ(app.document().get("a", "text"), "one");
    EXPECT_EQ(app.document().get("b", "text"), "two");

    model.rows[0].second = "one-b";
    ASSERT_TRUE(app.document().set_model("host", model));
    EXPECT_EQ(a_hits, 1);
    EXPECT_EQ(b_hits, 0);
    EXPECT_EQ(app.document().get("a", "text"), "one-b");

    app.document().unwatch(watch_a);
    model.rows[0].second = "one-c";
    ASSERT_TRUE(app.document().set_model("host", model));
    EXPECT_EQ(a_hits, 1);
    EXPECT_EQ(b_hits, 0);

    ASSERT_TRUE(app.document().set("b", "data-mark", "yes"));
    EXPECT_EQ(b_hits, 1);
    EXPECT_EQ(a_hits, 1);
    EXPECT_EQ(app.document().get("b", "data-mark"), "yes");
}

TEST(HostServices, ChildModalTakesInputAndCloseLeavesParent) {
    const fs::path parent = page_file(
        "zui-host-parent.html",
        "<!DOCTYPE html><html><body><button id=\"parent-btn\" onclick=\"on_parent\">P</button>"
        "<p id=\"parent-note\">parent</p></body></html>\n");
    const fs::path child = page_file(
        "zui-host-child.html",
        "<!DOCTYPE html><html><body onbeforeunload=\"on_child_close\">"
        "<button id=\"child-btn\" onclick=\"on_child\">C</button>"
        "<p id=\"child-note\">child</p></body></html>\n");
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(parent.string()));
    ASSERT_TRUE(zui::frame::Access::prepare_interaction(app));
    int parent_clicks = 0;
    int child_clicks = 0;
    bool block_close = true;
    app.document().bind("on_parent", [&](zui::Event& event) {
        if (event.type() == "click") {
            ++parent_clicks;
        }
    });
    app.document().bind("on_child", [&](zui::Event& event) {
        if (event.type() == "click") {
            ++child_clicks;
        }
    });
    app.document().bind("on_child_close", [&](zui::Event& event) {
        if (block_close && event.type() == "beforeunload") {
            event.prevent_default();
        }
    });

    zui::Document& parent_doc = app.document();
    zui::ChildDocument child_doc = app.window().open_document(child.string(), true);
    ASSERT_TRUE(child_doc);
    EXPECT_EQ(child_doc->get("child-note", "text"), "child");
    EXPECT_EQ(parent_doc.get("child-note", "text"), "");
    EXPECT_EQ(parent_doc.get("parent-note", "text"), "parent");
    ASSERT_TRUE(parent_doc.focus("parent-btn"));
    ASSERT_TRUE(child_doc->focus("child-btn"));
    ASSERT_TRUE(zui::frame::Access::key(app, "Enter"));
    ASSERT_TRUE(zui::frame::Access::click_id(app, "child-btn"));
    EXPECT_EQ(child_clicks, 2);
    EXPECT_EQ(parent_clicks, 0);

    EXPECT_FALSE(app.window().close_document(child_doc));
    EXPECT_EQ(child_doc->get("child-note", "text"), "child");
    EXPECT_EQ(parent_doc.get("parent-note", "text"), "parent");
    block_close = false;
    EXPECT_TRUE(app.window().close_document(child_doc));
    child_doc = {};
    EXPECT_EQ(parent_doc.get("child-note", "text"), "");
    EXPECT_EQ(parent_doc.get("parent-note", "text"), "parent");

    zui::ChildDocument other = app.window().open_document(child.string(), false);
    ASSERT_TRUE(other);
    EXPECT_TRUE(app.window().close_document(other));
    EXPECT_EQ(parent_doc.get("parent-note", "text"), "parent");
    (void)app.window().set_window_size(640, 480);

    // pump_frame itself, not frame::Access, must point, key, and paint the modal.
    zui::test::FakeWindow host_window;
    zui::test::RecordingRenderer host_renderer;
    zui::App host(0, nullptr);
    const fs::path host_parent = page_file(
        "zui-host-parent-pump.html",
        "<!DOCTYPE html><html><head><style>"
        "html,body{margin:0}"
        "#parent-btn{position:absolute;left:400px;top:400px;width:120px;height:40px}"
        "</style></head><body>"
        "<button id=\"parent-btn\" onclick=\"on_parent\">P</button>"
        "<p id=\"parent-note\">PARENT-MARK</p></body></html>\n");
    const fs::path host_child = page_file(
        "zui-host-child-pump.html",
        "<!DOCTYPE html><html><head><style>"
        "html,body{margin:0}"
        "[hidden]{display:none}"
        "#child-btn{position:absolute;left:0;top:0;width:160px;height:48px}"
        "#scroller{height:200px;overflow:auto}"
        "#pad{margin:0;height:80px}"
        "#tall{margin:0;height:400px}"
        "</style></head><body>"
        "<button id=\"child-btn\" onclick=\"on_child\">C</button>"
        "<p id=\"child-note\">MODAL-MARK</p>"
        "<div id=\"scroller\"><p id=\"pad\"></p><p id=\"tall\">SCROLL-MARK</p></div>"
        "</body></html>\n");
    ASSERT_TRUE(host.document().load(host_parent.string()));
    int pump_parent = 0;
    int pump_child = 0;
    host.document().bind("on_parent", [&](zui::Event& event) {
        if (event.type() == "click") {
            ++pump_parent;
        }
    });
    host.document().bind("on_child", [&](zui::Event& event) {
        if (event.type() == "click") {
            ++pump_child;
        }
    });
    zui::frame::Session host_session;
    host_session.window = &host_window;
    host_session.renderer = &host_renderer;
    ASSERT_TRUE(zui::frame::Access::attach(host, host_session));
    struct DetachHost {
        zui::App& app;
        ~DetachHost() { zui::frame::Access::detach(app); }
    } detach_host{host};
    auto items_have = [&](const char* needle) {
        for (const auto& item : host_renderer.last_items) {
            if (item.text.find(needle) != std::string::npos) {
                return true;
            }
        }
        return false;
    };
    ASSERT_TRUE(host.pump_frame());
    zui::ChildDocument modal = host.window().open_document(host_child.string(), true);
    ASSERT_TRUE(modal);
    ASSERT_TRUE(host.pump_frame());
    EXPECT_TRUE(items_have("MODAL-MARK"));
    EXPECT_FALSE(items_have("PARENT-MARK"));

    // A child set has to update that page's boxes. The next pump paints the modal, not the root.
    ASSERT_TRUE(modal->set("child-note", "text", "MODAL-NEXT"));
    ASSERT_TRUE(host.pump_frame());
    EXPECT_TRUE(items_have("MODAL-NEXT"));
    EXPECT_FALSE(items_have("MODAL-MARK"));
    EXPECT_FALSE(items_have("PARENT-MARK"));
    EXPECT_EQ(host.document().get("child-note", "text"), "");

    auto text_y = [&](const char* needle) {
        for (const auto& item : host_renderer.last_items) {
            if (item.text.find(needle) != std::string::npos) {
                return item.rect.y;
            }
        }
        return -1.f;
    };
    const float scroll_before = text_y("SCROLL-MARK");
    ASSERT_GT(scroll_before, 0.f);
    ASSERT_TRUE(modal->set("scroller", "scroll_top", "40"));
    ASSERT_TRUE(host.pump_frame());
    const float scroll_after = text_y("SCROLL-MARK");
    EXPECT_GT(scroll_after, 0.f);
    EXPECT_LT(scroll_after, scroll_before - 20.f);

    ASSERT_TRUE(modal->set("child-note", "visible", "false"));
    ASSERT_TRUE(host.pump_frame());
    EXPECT_FALSE(items_have("MODAL-NEXT"));
    EXPECT_TRUE(items_have("SCROLL-MARK"));
    EXPECT_FALSE(items_have("PARENT-MARK"));

    zui::backends::PointerFrame click;
    zui::backends::PointerContact mouse;
    mouse.id = 1;
    mouse.kind = zui::backends::PointerKind::Mouse;
    mouse.primary = true;
    mouse.x = 20.f;
    mouse.y = 20.f;
    mouse.buttons = 1;
    mouse.down = true;
    mouse.pressed = true;
    mouse.released = true;
    mouse.changed_button = 1;
    click.contacts.push_back(mouse);
    host_window.inject(std::move(click));
    ASSERT_TRUE(host.pump_frame());
    EXPECT_EQ(pump_child, 1);
    EXPECT_EQ(pump_parent, 0);

    ASSERT_TRUE(modal->focus("child-btn"));
    zui::backends::TextInputFrame keys;
    zui::backends::KeyPress enter;
    enter.key = "Enter";
    keys.key_events.push_back(enter);
    keys.enter = true;
    host_window.inject(keys);
    ASSERT_TRUE(host.pump_frame());
    EXPECT_EQ(pump_child, 2);
    EXPECT_EQ(pump_parent, 0);

    EXPECT_TRUE(host.window().close_document(modal));
    modal = {};
    ASSERT_TRUE(host.pump_frame());
    EXPECT_TRUE(items_have("PARENT-MARK"));
    EXPECT_FALSE(items_have("MODAL-MARK"));

    zui::backends::PointerFrame parent_click;
    mouse.x = 440.f;
    mouse.y = 420.f;
    parent_click.contacts.push_back(mouse);
    host_window.inject(std::move(parent_click));
    ASSERT_TRUE(host.pump_frame());
    EXPECT_EQ(pump_parent, 1);
    EXPECT_EQ(pump_child, 2);
    EXPECT_EQ(host.document().get("parent-note", "text"), "PARENT-MARK");
}

TEST(HostServices, DiagnosticsLogAndCrashReport) {
    const fs::path log = fs::temp_directory_path() / "zui-host-diag.log";
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.enable_diagnostics(log.string()));
    app.log_line("diag-line");
    {
        std::ifstream in(log);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        EXPECT_NE(text.find("diag-line"), std::string::npos);
    }
#if defined(_WIN32)
    const fs::path crash_log = fs::temp_directory_path() / "zui-host-crash.log";
    const fs::path crash_report = fs::temp_directory_path() / "zui-host-crash.crash";
    std::error_code error;
    fs::remove(crash_report, error);
    const std::wstring command = L"\"" + exe_path() + L"\" --zui-crash-probe \"" +
                                 widen(crash_log.string()) + L"\"";
    const DWORD code = run_command(command);
    EXPECT_EQ(code, 1u);
    std::ifstream in(crash_report);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_NE(text.find("ACCESS_VIOLATION"), std::string::npos) << text;
#else
    GTEST_SKIP() << "crash report is gated on Windows";
#endif
}

TEST(HostServices, ImageClipboardIsSeparateFromText) {
    auto window = zui::backends::create_window("sdl3");
    if (window == nullptr) {
        GTEST_SKIP() << "SDL window backend is not built";
    }
    zui::backends::WindowDesc desc;
    desc.title = "zui clipboard";
    desc.width = 200;
    desc.height = 120;
    desc.hidden = true;
    desc.api = zui::backends::GfxApi::Vulkan;
    ASSERT_TRUE(window->create(desc));
    const fs::path page = page_file("zui-host-clip.html",
                                    "<!DOCTYPE html><html><body><p id=\"n\">n</p></body></html>\n");
    zui::test::RecordingRenderer renderer;
    zui::App app(0, nullptr);
    ASSERT_TRUE(app.document().load(page.string()));
    zui::frame::Session session;
    session.window = window.get();
    session.renderer = &renderer;
    ASSERT_TRUE(zui::frame::Access::attach(app, session));
    struct Detach {
        zui::App& app;
        ~Detach() { zui::frame::Access::detach(app); }
    } detach{app};

    // Another process can hold the clipboard for a short stretch. Retry the real OS call.
    bool text_set = false;
    for (int attempt = 0; attempt < 40 && !text_set; ++attempt) {
        text_set = app.host().set_clipboard_text("plain-text");
        if (!text_set) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    ASSERT_TRUE(text_set);
    const std::uint8_t pixels[] = {1, 2, 3, 255, 4, 5, 6, 128, 7, 8, 9, 255, 10, 11, 12, 64};
    zui::ClipboardImage owned;
    owned.width = 2;
    owned.height = 2;
    owned.rgba.assign(pixels, pixels + sizeof(pixels));
    bool image_set = false;
    for (int attempt = 0; attempt < 40 && !image_set; ++attempt) {
        image_set = app.host().set_clipboard_image(owned);
        if (!image_set) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    ASSERT_TRUE(image_set);
    const zui::ClipboardImage image = app.host().clipboard_image();
    EXPECT_EQ(image.status, zui::ClipboardStatus::Ok);
    EXPECT_EQ(image.width, 2);
    EXPECT_EQ(image.height, 2);
    ASSERT_EQ(image.rgba.size(), sizeof(pixels));
    EXPECT_EQ(image.rgba, std::vector<std::uint8_t>(pixels, pixels + sizeof(pixels)));
    const zui::ClipboardText text = app.host().clipboard_text();
    EXPECT_EQ(text.status, zui::ClipboardStatus::Ok);
    EXPECT_EQ(text.text, "plain-text");
    window->destroy();
}

TEST(HostServices, ImeCaretAndSdlComposition) {
    auto window = zui::backends::create_window("sdl3");
    if (window == nullptr) {
        GTEST_SKIP() << "SDL window backend is not built";
    }
    zui::backends::WindowDesc desc;
    desc.title = "zui ime";
    desc.width = 200;
    desc.height = 120;
    desc.hidden = true;
    desc.api = zui::backends::GfxApi::Vulkan;
    ASSERT_TRUE(window->create(desc));
    window->set_ime_rect(10.f, 20.f, 4.f, 18.f);
    long left = 0;
    long top = 0;
    long right = 0;
    long bottom = 0;
    ASSERT_TRUE(zui::backends::ime_read_caret(window->native_handle().get(), left, top, right, bottom));
    EXPECT_GT(right, left);
    EXPECT_GT(bottom, top);
    if (std::fabs(window->content_scale() - 1.f) < 0.05f) {
        EXPECT_EQ(left, 10);
        EXPECT_EQ(top, 20);
        EXPECT_EQ(right, 14);
        EXPECT_EQ(bottom, 38);
    }

    window->inject_composition_for_test(1, "ni", true);
    ASSERT_TRUE(window->poll());
    EXPECT_TRUE(window->text().composing);
    EXPECT_EQ(window->text().composition_phase, 1);
    EXPECT_TRUE(window->text().text.empty());
    EXPECT_EQ(window->text().composition, "ni");
    window->inject_composition_for_test(3, "你", false);
    ASSERT_TRUE(window->poll());
    EXPECT_FALSE(window->text().composing);
    EXPECT_EQ(window->text().composition_phase, 3);
    EXPECT_EQ(window->text().text, "你");
    EXPECT_TRUE(window->text().composition.empty());
    window->destroy();
}

#if defined(_WIN32)
int g_ime_keys = 0;
int g_ime_chars = 0;
int g_ime_msgs = 0;

void pump_hwnd_messages() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

bool extended_key(UINT vk) {
    switch (vk) {
        case VK_MEDIA_NEXT_TRACK:
        case VK_MEDIA_PREV_TRACK:
        case VK_MEDIA_PLAY_PAUSE:
        case VK_MEDIA_STOP:
            return true;
        default:
            return false;
    }
}

void key_event(UINT vk, bool up) {
    DWORD flags = up ? KEYEVENTF_KEYUP : 0;
    if (extended_key(vk)) {
        flags |= KEYEVENTF_EXTENDEDKEY;
    }
    const BYTE scan = static_cast<BYTE>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    keybd_event(static_cast<BYTE>(vk), scan, flags, 0);
}

void tap_key(WORD vk) {
    key_event(vk, false);
    key_event(vk, true);
}

bool claim_foreground_once(HWND hwnd) {
    HWND fg = GetForegroundWindow();
    const DWORD from = fg != nullptr ? GetWindowThreadProcessId(fg, nullptr) : 0;
    const DWORD me = GetCurrentThreadId();
    const BOOL attached = (from != 0 && from != me) ? AttachThreadInput(from, me, TRUE) : FALSE;
    // ALT while attached is what lets a later window become foreground after
    // another window in this process (the SDL IME caret window) has released it.
    key_event(VK_MENU, false);
    ShowWindow(hwnd, SW_RESTORE);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(hwnd);
    key_event(VK_MENU, true);
    if (attached) {
        AttachThreadInput(from, me, FALSE);
    }
    return GetForegroundWindow() == hwnd;
}

bool take_foreground(HWND hwnd) {
    // A tool popup cannot become foreground. An overlapped window shown on
    // screen can, which is what lets the IME and RegisterHotKey see the keys.
    for (int attempt = 0; attempt < 15; ++attempt) {
        if (claim_foreground_once(hwnd)) {
            return true;
        }
        Sleep(20);
        pump_hwnd_messages();
    }
    return GetForegroundWindow() == hwnd;
}

void tap_registered_key(UINT modifiers, UINT vk) {
    // The OS posts WM_HOTKEY only for a real chord. Hold the modifiers, tap the
    // virtual key that RegisterHotKey recorded, and pump before the key-up.
    if ((modifiers & MOD_CONTROL) != 0) {
        key_event(VK_CONTROL, false);
    }
    if ((modifiers & MOD_ALT) != 0) {
        key_event(VK_MENU, false);
    }
    if ((modifiers & MOD_SHIFT) != 0) {
        key_event(VK_SHIFT, false);
    }
    key_event(vk, false);
    Sleep(40);
    pump_messages();
    key_event(vk, true);
    if ((modifiers & MOD_SHIFT) != 0) {
        key_event(VK_SHIFT, true);
    }
    if ((modifiers & MOD_ALT) != 0) {
        key_event(VK_MENU, true);
    }
    if ((modifiers & MOD_CONTROL) != 0) {
        key_event(VK_CONTROL, true);
    }
    Sleep(40);
    pump_messages();
}
#endif

TEST(HostServices, GlfwImeCompositionThroughPoll) {
#if !defined(_WIN32)
    GTEST_SKIP() << "Win32 IME composition is gated on Windows";
#else
    // The string has to come from ImmGetCompositionString on a real window.
    // Glfw3Window::poll calls ImeComposer::poll_hwnd on its HWND; when that
    // backend is not in this build, the test drives that same function.
    auto glfw = zui::backends::create_window("glfw3");
    WNDCLASSW wc{};
    wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
        if (msg == WM_KEYDOWN) {
            ++g_ime_keys;
        } else if (msg == WM_CHAR) {
            ++g_ime_chars;
        } else if (msg == WM_IME_COMPOSITION || msg == WM_IME_STARTCOMPOSITION) {
            ++g_ime_msgs;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    };
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"ZuiImePoll";
    RegisterClassW(&wc);
    HWND raw = nullptr;
    if (glfw == nullptr) {
        raw = CreateWindowExW(0, wc.lpszClassName, L"zui-ime", WS_OVERLAPPEDWINDOW, 120, 120, 240,
                              100, nullptr, nullptr, wc.hInstance, nullptr);
        ASSERT_NE(raw, nullptr);
    }
    struct ImeScope {
        std::unique_ptr<zui::backends::IWindow> glfw;
        HWND raw = nullptr;
        HWND previous = nullptr;
        zui::backends::ImeComposer composer;
        ~ImeScope() {
            HWND hwnd = native();
            if (hwnd != nullptr) {
                HIMC imc = ImmGetContext(hwnd);
                if (imc != nullptr) {
                    ImmNotifyIME(imc, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
                    ImmReleaseContext(hwnd, imc);
                }
            }
            if (glfw != nullptr) {
                glfw->destroy();
            }
            if (raw != nullptr) {
                DestroyWindow(raw);
            }
            if (previous != nullptr && IsWindow(previous)) {
                SetForegroundWindow(previous);
            }
        }
        HWND native() const {
            if (glfw != nullptr) {
                return static_cast<HWND>(glfw->native_handle().get());
            }
            return raw;
        }
    } scope;
    scope.glfw = std::move(glfw);
    scope.raw = raw;
    scope.previous = GetForegroundWindow();
    if (scope.glfw != nullptr) {
        zui::backends::WindowDesc desc;
        desc.title = "zui glfw ime";
        desc.width = 40;
        desc.height = 40;
        desc.hidden = true;
        desc.api = zui::backends::GfxApi::Vulkan;
        ASSERT_TRUE(scope.glfw->create(desc));
    }
    HWND hwnd = scope.native();
    ASSERT_NE(hwnd, nullptr);
    ASSERT_TRUE(take_foreground(hwnd)) << "foreground=" << GetForegroundWindow();

    // pump_frame / apply_editing must see the composition, not only window->text().
    zui::test::FakeWindow edit_window;
    zui::test::RecordingRenderer edit_renderer;
    zui::App edit_app(0, nullptr);
    const fs::path edit_page = page_file(
        "zui-host-ime-edit.html",
        "<!DOCTYPE html><html><body><input id=\"ime-field\"></body></html>\n");
    ASSERT_TRUE(edit_app.document().load(edit_page.string()));
    zui::frame::Session edit_session;
    edit_session.window = &edit_window;
    edit_session.renderer = &edit_renderer;
    ASSERT_TRUE(zui::frame::Access::attach(edit_app, edit_session));
    struct DetachEdit {
        zui::App& app;
        ~DetachEdit() { zui::frame::Access::detach(app); }
    } detach_edit{edit_app};
    ASSERT_TRUE(edit_app.document().focus("ime-field"));
    ASSERT_TRUE(edit_app.pump_frame());

    auto read_os = [&](zui::backends::TextInputFrame& frame) {
        frame = {};
        if (scope.glfw != nullptr) {
            EXPECT_TRUE(scope.glfw->poll());
            frame = scope.glfw->text();
            return;
        }
        pump_hwnd_messages();
        scope.composer.poll_hwnd(hwnd);
        scope.composer.apply(frame);
    };
    auto publish = [&](zui::backends::TextInputFrame& frame) {
        read_os(frame);
        edit_window.inject(frame);
        EXPECT_TRUE(edit_app.pump_frame());
    };

    auto arm_ime = [&]() {
        HIMC imc = ImmGetContext(hwnd);
        if (imc == nullptr) {
            return;
        }
        ImmSetOpenStatus(imc, TRUE);
        DWORD conversion = 0;
        DWORD sentence = 0;
        ImmGetConversionStatus(imc, &conversion, &sentence);
        ImmSetConversionStatus(imc, conversion | IME_CMODE_NATIVE, sentence);
        ImmReleaseContext(hwnd, imc);
    };
    auto toggle_ime = [&](int which) {
        // Microsoft Pinyin is TSF. ImmSetConversionStatus can report native while
        // the mode that produces WM_CHAR is still English. Shift and the IME
        // non-IME hotkey are the switches that actually change that mode.
        if (which % 2 == 0) {
            ImmSimulateHotKey(hwnd, IME_CHOTKEY_IME_NONIME_TOGGLE);
            ImmSimulateHotKey(hwnd, IME_THOTKEY_IME_NONIME_TOGGLE);
            key_event(VK_SHIFT, false);
            Sleep(20);
            pump_hwnd_messages();
            key_event(VK_SHIFT, true);
        } else {
            key_event(VK_CONTROL, false);
            key_event(VK_SPACE, false);
            Sleep(40);
            pump_hwnd_messages();
            key_event(VK_SPACE, true);
            key_event(VK_CONTROL, true);
        }
        pump_hwnd_messages();
        Sleep(40);
    };

    zui::backends::TextInputFrame frame;
    read_os(frame);
    arm_ime();
    // Hold the key down so the IME's preedit is visible before key-up commits it.
    // Other host tests can leave the system IME in direct input; Ctrl+Space is the
    // switch back to composition, tried once in each direction.
    bool saw_preedit = false;
    int preedit_phase = 0;
    struct ReleaseN {
        bool down = false;
        ~ReleaseN() {
            if (down) {
                key_event('N', true);
            }
        }
    } release_n;
    for (int toggle = 0; toggle < 6 && !saw_preedit; ++toggle) {
        if (toggle != 0) {
            toggle_ime(toggle);
            arm_ime();
        }
        key_event('N', false);
        release_n.down = true;
        for (int i = 0; i < 12 && !saw_preedit; ++i) {
            read_os(frame);
            saw_preedit = frame.composing && !frame.composition.empty() && frame.text.empty();
            if (!saw_preedit) {
                Sleep(15);
            }
        }
        if (saw_preedit) {
            preedit_phase = frame.composition_phase;
            break;
        }
        key_event('N', true);
        release_n.down = false;
        pump_hwnd_messages();
    }
    HIMC diag = ImmGetContext(hwnd);
    BOOL ime_open = FALSE;
    DWORD conversion = 0;
    DWORD sentence = 0;
    if (diag != nullptr) {
        ime_open = ImmGetOpenStatus(diag);
        ImmGetConversionStatus(diag, &conversion, &sentence);
        ImmReleaseContext(hwnd, diag);
    }
    ASSERT_TRUE(saw_preedit) << "composing=" << frame.composing << " composition=" << frame.composition
                             << " text=" << frame.text << " value=" << edit_app.document().get("ime-field", "value")
                             << " focus=" << GetFocus() << " fg=" << GetForegroundWindow()
                             << " open=" << ime_open << " conv=" << conversion
                             << " keys=" << g_ime_keys << " chars=" << g_ime_chars
                             << " ime=" << g_ime_msgs;
    EXPECT_TRUE(preedit_phase == 1 || preedit_phase == 2) << "phase=" << preedit_phase;
    const std::string preedit = frame.composition;
    EXPECT_EQ(preedit.find('\n'), std::string::npos);
    edit_window.inject(frame);
    ASSERT_TRUE(edit_app.pump_frame());
    EXPECT_EQ(edit_app.document().get("ime-field", "value"), preedit);
    // Second poll while the key is still down, so GCS_COMPSTR is still this preedit.
    read_os(frame);
    edit_window.inject(frame);
    ASSERT_TRUE(edit_app.pump_frame());
    EXPECT_EQ(edit_app.document().get("ime-field", "value"), preedit);

    if (scope.glfw != nullptr) {
        tap_key(VK_RETURN);
        publish(frame);
        EXPECT_FALSE(frame.enter);
        EXPECT_EQ(frame.text, preedit);
    } else {
        // Key callback records Enter, the char callback would append CR, then poll_hwnd.
        scope.composer.note_enter();
        frame = {};
        frame.text = "\r";
        pump_hwnd_messages();
        scope.composer.poll_hwnd(hwnd);
        scope.composer.apply(frame);
        EXPECT_FALSE(frame.composing);
        EXPECT_EQ(frame.composition_phase, 3);
        EXPECT_EQ(frame.text, preedit);
    }
    EXPECT_EQ(frame.text.find('\r'), std::string::npos);
    EXPECT_EQ(frame.text.find('\n'), std::string::npos);
    edit_window.inject(frame);
    ASSERT_TRUE(edit_app.pump_frame());
    EXPECT_EQ(edit_app.document().get("ime-field", "value"), preedit);

    // The next frame polls again. COMPSTR is still open. Do not cancel it first.
    frame = {};
    if (scope.glfw != nullptr) {
        EXPECT_TRUE(scope.glfw->poll());
        frame = scope.glfw->text();
    } else {
        pump_hwnd_messages();
        scope.composer.poll_hwnd(hwnd);
        scope.composer.apply(frame);
    }
    edit_window.inject(frame);
    ASSERT_TRUE(edit_app.pump_frame());
    EXPECT_EQ(edit_app.document().get("ime-field", "value"), preedit);
    EXPECT_EQ(edit_app.document().get("ime-field", "value").find('\n'), std::string::npos);
    EXPECT_EQ(edit_app.document().get("ime-field", "value").find('\r'), std::string::npos);
    key_event('N', true);
    release_n.down = false;
#endif
}

TEST(HostServices, NotifyTrayBadgeHotkeyAndMedia) {
#if !defined(_WIN32)
    GTEST_SKIP() << "plugins are gated on Windows";
#else
    const zui::HostServices services = session_services();
    const zui::plugin::OsResult toast = services.notify("ZuiTitle", "ZuiBody");
    if (!toast.ok) {
        note_os("notify", toast.os_error);
    }
    ASSERT_TRUE(toast.ok) << "notify os_error=" << toast.os_error;
    EXPECT_EQ(toast.title, "ZuiTitle");
    EXPECT_EQ(toast.body, "ZuiBody");

    int tray_hits = 0;
    const zui::plugin::OsResult tray = services.tray_item("ZuiTip", [&] { ++tray_hits; });
    if (!tray.ok) {
        note_os("tray", tray.os_error);
    }
    ASSERT_TRUE(tray.ok) << "tray os_error=" << tray.os_error;
    HWND tray_hwnd = FindWindowW(L"ZuiTray", L"ZuiTray");
    ASSERT_NE(tray_hwnd, nullptr);
    // Shell activates the icon with the callback message registered on the tray,
    // not with a raw button-up posted at the window.
    SendMessageW(tray_hwnd, WM_APP + 1, 7, NIN_SELECT);
    EXPECT_EQ(tray_hits, 1);

    const zui::plugin::OsResult badge = services.tray_badge(4);
    if (!badge.ok) {
        note_os("badge", badge.os_error);
    }
    ASSERT_TRUE(badge.ok) << "badge os_error=" << badge.os_error;
    EXPECT_EQ(services.tray_badge_count(), 4);
    EXPECT_EQ(badge.title, "4");

    int hot_hits = 0;
    zui::plugin::Hotkey hotkey;
    hotkey.key = "F24";
    hotkey.control = true;
    hotkey.alt = true;
    hotkey.shift = true;
    const zui::plugin::OsResult hot = services.global_hotkey(hotkey, [&] { ++hot_hits; });
    if (!hot.ok) {
        note_os("hotkey", hot.os_error);
    }
    ASSERT_TRUE(hot.ok) << "hotkey os_error=" << hot.os_error << " " << hot.body;
    EXPECT_NE(hot.title.find("F24"), std::string::npos);
    WNDCLASSW focus_class{};
    focus_class.lpfnWndProc = DefWindowProcW;
    focus_class.hInstance = GetModuleHandleW(nullptr);
    focus_class.lpszClassName = L"ZuiHotkeyFocus";
    RegisterClassW(&focus_class);
    struct FocusScope {
        HWND hwnd = nullptr;
        HWND previous = nullptr;
        ~FocusScope() {
            HWND hot = FindWindowW(L"ZuiHotkey", L"ZuiHotkey");
            if (hot != nullptr) {
                for (int id = 1; id <= 32; ++id) {
                    UnregisterHotKey(hot, id);
                }
            }
            if (hwnd != nullptr) {
                DestroyWindow(hwnd);
            }
            if (previous != nullptr && IsWindow(previous)) {
                SetForegroundWindow(previous);
            }
        }
    } focus_scope;
    focus_scope.previous = GetForegroundWindow();
    focus_scope.hwnd = CreateWindowExW(0, focus_class.lpszClassName, L"zui-hotkey-focus",
                                       WS_OVERLAPPEDWINDOW, 120, 120, 240, 100, nullptr, nullptr,
                                       focus_class.hInstance, nullptr);
    ASSERT_NE(focus_scope.hwnd, nullptr);
    ASSERT_TRUE(take_foreground(focus_scope.hwnd)) << "foreground=" << GetForegroundWindow();
    tap_registered_key(MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_F24);
    for (int i = 0; i < 50 && hot_hits == 0; ++i) {
        pump_messages();
        Sleep(10);
    }
    EXPECT_EQ(hot_hits, 1);

    int other_hits = 0;
    zui::plugin::Hotkey other;
    other.key = "F23";
    other.control = true;
    const zui::plugin::OsResult other_hot = services.global_hotkey(other, [&] { ++other_hits; });
    ASSERT_TRUE(other_hot.ok) << other_hot.os_error << " " << other_hot.body;
    EXPECT_NE(other_hot.title.find("F23"), std::string::npos);
    EXPECT_EQ(other_hot.title.find("F24"), std::string::npos);
    EXPECT_NE(other_hot.title, hot.title);
    tap_registered_key(MOD_CONTROL, VK_F23);
    for (int i = 0; i < 50 && other_hits == 0; ++i) {
        pump_messages();
        Sleep(10);
    }
    EXPECT_EQ(other_hits, 1);
    EXPECT_EQ(hot_hits, 1);

    const char* keys[] = {"play", "next", "previous"};
    const zui::plugin::MediaKey actions[] = {
        zui::plugin::MediaKey::Play, zui::plugin::MediaKey::Next, zui::plugin::MediaKey::Previous};
    const UINT virtual_keys[] = {VK_MEDIA_PLAY_PAUSE, VK_MEDIA_NEXT_TRACK, VK_MEDIA_PREV_TRACK};
    for (int i = 0; i < 3; ++i) {
        int hits = 0;
        const zui::plugin::OsResult media = services.media_key(actions[i], [&] { ++hits; });
        if (!media.ok) {
            note_os(keys[i], media.os_error);
        }
        ASSERT_TRUE(media.ok) << keys[i] << " os_error=" << media.os_error;
        EXPECT_EQ(media.title, keys[i]);
        tap_registered_key(0, virtual_keys[i]);
        for (int attempt = 0; attempt < 50 && hits == 0; ++attempt) {
            pump_messages();
            Sleep(10);
        }
        EXPECT_EQ(hits, 1) << keys[i];
    }
#endif
}

TEST(HostServices, SingleInstanceFileUrlAndLocalUpdate) {
#if !defined(_WIN32)
    GTEST_SKIP() << "plugins are gated on Windows";
#else
    const zui::HostServices services = session_services();
    const std::string identity = "zui-goal-" + std::to_string(GetCurrentProcessId());
    ASSERT_TRUE(services.claim_single_instance(identity));
    EXPECT_TRUE(services.take_instance_args("not-" + identity).empty());
    const std::wstring command = L"\"" + exe_path() + L"\" --zui-second " + widen(identity) +
                                 L" second-args";
    EXPECT_EQ(run_command(command), 0u);
    std::string args;
    for (int i = 0; i < 50 && args.empty(); ++i) {
        args = services.take_instance_args(identity);
        if (args.empty()) {
            Sleep(20);
        }
    }
    EXPECT_EQ(args, "second-args");

    const std::string ext = ".zgh" + std::to_string(GetCurrentProcessId());
    const std::string scheme = "zgh" + std::to_string(GetCurrentProcessId());
    const std::string file_prog = "ZuiGoalFile" + std::to_string(GetCurrentProcessId());
    const std::string url_prog = "ZuiGoalUrl" + std::to_string(GetCurrentProcessId());
    const std::string self = narrow(exe_path());
    struct Cleanup {
        std::string file_prog;
        std::string ext;
        std::string url_prog;
        std::string scheme;
        ~Cleanup() {
            const zui::HostServices services = session_services();
            (void)services.unregister_file_extension(file_assoc(ext, file_prog, {}));
            (void)services.unregister_url_scheme(scheme_assoc(scheme, url_prog, {}));
        }
    } cleanup{file_prog, ext, url_prog, scheme};
    ASSERT_TRUE(services.register_file_extension(file_assoc(ext, file_prog, self)));
    ASSERT_TRUE(services.register_url_scheme(scheme_assoc(scheme, url_prog, self)));
    const std::wstring registered = L"\"" + widen(self) + L"\" \"%1\"";
    EXPECT_EQ(reg_sz(L"Software\\Classes\\" + widen(file_prog) + L"\\shell\\open\\command"),
              registered);
    EXPECT_EQ(reg_sz(L"Software\\Classes\\" + widen(url_prog) + L"\\shell\\open\\command"),
              registered);

    const fs::path file_out = fs::temp_directory_path() / "zui-host-file-arg.txt";
    const fs::path url_out = fs::temp_directory_path() / "zui-host-url-arg.txt";
    const std::string file_arg = (fs::temp_directory_path() / ("note" + ext)).string();
    const std::string url_arg = scheme + "://open";
    const DWORD file_code = run_command(L"\"" + exe_path() + L"\" --zui-startup-probe \"" +
                                        widen(file_out.string()) + L"\" \"" + widen(file_arg) +
                                        L"\"");
    const DWORD url_code = run_command(L"\"" + exe_path() + L"\" --zui-startup-probe \"" +
                                       widen(url_out.string()) + L"\" \"" + widen(url_arg) + L"\"");
    EXPECT_EQ(file_code, 0u);
    EXPECT_EQ(url_code, 0u);
    {
        std::ifstream in(file_out);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        EXPECT_NE(text.find(file_arg), std::string::npos) << text;
    }
    {
        std::ifstream in(url_out);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        EXPECT_NE(text.find(url_arg), std::string::npos) << text;
    }
    EXPECT_TRUE(services.unregister_file_extension(file_assoc(ext, file_prog, self)));
    EXPECT_TRUE(services.unregister_url_scheme(scheme_assoc(scheme, url_prog, self)));
    EXPECT_TRUE(reg_missing(L"Software\\Classes\\" + widen(file_prog)));
    EXPECT_TRUE(reg_missing(L"Software\\Classes\\" + widen(ext)));
    EXPECT_TRUE(reg_missing(L"Software\\Classes\\" + widen(url_prog)));
    EXPECT_TRUE(reg_missing(L"Software\\Classes\\" + widen(scheme)));
#endif
    const fs::path dir = fs::temp_directory_path() / "zui-host-update";
    fs::create_directories(dir);
    const fs::path payload = dir / "payload.bin";
    const fs::path manifest = dir / "manifest.txt";
    {
        std::ofstream out(payload, std::ios::binary);
        out << "NEW";
    }
    {
        std::ofstream out(manifest);
        out << "version=2\npayload=payload.bin\n";
    }
    const fs::path dest = dir / "installed";
    const zui::HostServices update_services = session_services();
    const zui::plugin::UpdateReport report =
        update_services.apply_local_update(manifest.string(), "1", dest.string());
    EXPECT_TRUE(report.newer);
    EXPECT_TRUE(report.applied);
    EXPECT_EQ(report.version, "2");
    {
        std::ifstream in(dest / "payload.bin");
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        EXPECT_EQ(text, "NEW");
    }
    const zui::plugin::UpdateReport same =
        update_services.apply_local_update(manifest.string(), "2", dest.string());
    EXPECT_FALSE(same.newer);
    EXPECT_FALSE(same.applied);
}

TEST(HostServices, ChoosableOpener) {
#if !defined(_WIN32)
    GTEST_SKIP() << "HKCU opener registration is gated on Windows";
#else
    const std::string pid = std::to_string(GetCurrentProcessId());
    const std::string app_name = "\xe5\xb0\x8f\xe7\xab\xb9\xe7\x9b\xb8\xe5\x86\x8c";
    const std::string ext = ".zop" + pid;
    const std::string scheme = "zop" + pid;
    const std::string mime = "application/x-zui-opener-" + pid;
    const std::string prog = "ZuiOpener" + pid;
    const std::string self = narrow(exe_path());
    const std::string any_prog = "ZuiAnyOpener" + pid;
    const std::string any_ext = ".zna" + pid;
    const std::string any_mime = "application/x-zui-any-" + pid;
    const std::string any_exe = "D:\\apps\\zui_album.exe";
    const zui::HostServices services = session_services();

    struct Cleanup {
        std::string prog;
        std::string ext;
        std::string scheme;
        std::string mime;
        std::string any_prog;
        std::string any_ext;
        ~Cleanup() {
            const zui::HostServices services = session_services();
            (void)services.unregister_opener(opener_query(ext, mime, scheme, prog));
            (void)services.unregister_opener(opener_query(any_ext, {}, {}, any_prog));
            _putenv_s("ZUI_OPENER_PROBE_OUT", "");
        }
    } cleanup{prog, ext, scheme, mime, any_prog, any_ext};

    zui::plugin::OpenerSpec any;
    any.app_name = "Not Album";
    any.prog_id = any_prog;
    any.executable = any_exe;
    any.extension = any_ext;
    any.mime = any_mime;
    const zui::plugin::OpenerReport any_report = services.register_opener(any);
    ASSERT_TRUE(any_report.ran);
    ASSERT_TRUE(any_report.ok);
    const zui::plugin::OpenerReport any_query = services.query_opener(opener_query(any_ext));
    EXPECT_TRUE(any_query.ok);
    EXPECT_EQ(any_query.executable, any_exe);
    EXPECT_TRUE(services.unregister_opener(opener_query(any_ext, any_mime, {}, any_prog)).ok);
    EXPECT_TRUE(reg_missing(L"Software\\Classes\\" + widen(any_prog)));
    EXPECT_TRUE(reg_missing(L"Software\\Classes\\" + widen(any_ext)));

    zui::plugin::OpenerSpec spec;
    spec.app_name = app_name;
    spec.prog_id = prog;
    spec.executable = self;
    spec.extension = ext;
    spec.mime = mime;
    spec.scheme = scheme;
    const zui::plugin::OpenerReport report = services.register_opener(spec);
    ASSERT_TRUE(report.ran) << report.os_error;
    ASSERT_TRUE(report.ok) << report.os_error;
    const std::wstring command = widen(report.command);
    EXPECT_EQ(command, L"\"" + widen(self) + L"\" \"%1\"");
    EXPECT_EQ(reg_sz(L"Software\\Classes\\" + widen(prog) + L"\\shell\\open\\command"), command);
    EXPECT_EQ(reg_sz(L"Software\\Classes\\" + widen(prog)), widen(app_name));
    EXPECT_EQ(reg_named(L"Software\\Classes\\" + widen(prog) + L"\\Application", L"ApplicationName"),
              widen(app_name));
    EXPECT_TRUE(reg_value_present(L"Software\\Classes\\" + widen(ext) + L"\\OpenWithProgids", widen(prog)));
    EXPECT_EQ(reg_named(L"Software\\Classes\\" + widen(prog) + L"\\Capabilities\\FileAssociations", widen(ext)),
              widen(prog));
    EXPECT_EQ(reg_named(L"Software\\RegisteredApplications", widen(prog)),
              L"Software\\Classes\\" + widen(prog) + L"\\Capabilities");
    EXPECT_EQ(reg_named(L"Software\\Classes\\" + widen(prog) + L"\\Capabilities\\UrlAssociations", widen(scheme)),
              widen(prog));
    EXPECT_TRUE(reg_sz(L"Software\\Classes\\" + widen(ext)).empty());
    EXPECT_TRUE(reg_missing(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\" + widen(ext) +
                            L"\\UserChoice"));

    EXPECT_NE(report.desktop_entry.find("Name=" + app_name), std::string::npos);
    EXPECT_NE(report.desktop_entry.find("Exec=\"" + self + "\" %f"), std::string::npos);
    EXPECT_NE(report.desktop_entry.find("MimeType=" + mime + ";"), std::string::npos);
    EXPECT_NE(report.desktop_entry.find("x-scheme-handler/" + scheme), std::string::npos);
    EXPECT_NE(report.mimeapps.find("[Default Applications]"), std::string::npos);
    EXPECT_NE(report.mimeapps.find(mime + "=" + report.desktop_name), std::string::npos);
    EXPECT_NE(report.mimeapps.find("x-scheme-handler/" + scheme + "=" + report.desktop_name), std::string::npos);

    const zui::plugin::OpenerReport by_ext = services.query_opener(opener_query(ext));
    EXPECT_TRUE(by_ext.ok);
    EXPECT_EQ(by_ext.executable, self);
    EXPECT_EQ(by_ext.app_name, app_name);
    const zui::plugin::OpenerReport by_mime = services.query_opener(opener_query({}, mime));
    EXPECT_TRUE(by_mime.ok);
    EXPECT_EQ(by_mime.executable, self);
    const zui::plugin::OpenerReport by_scheme = services.query_opener(opener_query({}, {}, scheme));
    EXPECT_TRUE(by_scheme.ok);
    EXPECT_EQ(by_scheme.executable, self);

    const fs::path probe_out = fs::temp_directory_path() / ("zui-opener-probe-" + pid + ".txt");
    const std::string file_arg = (fs::temp_directory_path() / ("picture-" + pid + ".png")).string();
    fs::remove(probe_out);
    {
        struct ProbeEnv {
            explicit ProbeEnv(const fs::path& path) { _putenv_s("ZUI_OPENER_PROBE_OUT", path.string().c_str()); }
            ~ProbeEnv() { _putenv_s("ZUI_OPENER_PROBE_OUT", ""); }
        } probe_env(probe_out);
        const zui::plugin::OpenerReport launched = services.launch_opener(spec, file_arg);
        ASSERT_TRUE(launched.ran);
        ASSERT_TRUE(launched.ok) << launched.os_error;
    }
    std::string captured;
    for (int i = 0; i < 100 && captured.find(file_arg) == std::string::npos; ++i) {
        Sleep(50);
        std::ifstream in(probe_out);
        captured.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    EXPECT_NE(captured.find(file_arg), std::string::npos) << captured;
    EXPECT_NE(captured.find(self), std::string::npos) << captured;

    fs::remove(probe_out);
    zui::App app(0, nullptr);
    EXPECT_FALSE(app.host().open_uri("javascript:alert(1)"));
    EXPECT_FALSE(app.host().open_uri("vbscript:msgbox"));
    EXPECT_FALSE(app.host().open_uri(""));
    EXPECT_FALSE(app.host().open_uri(scheme + "://open"));
    EXPECT_FALSE(fs::exists(probe_out));

    const zui::plugin::OpenerReport removed = services.unregister_opener(spec);
    EXPECT_TRUE(removed.ran);
    EXPECT_TRUE(removed.ok);
    EXPECT_TRUE(reg_missing(L"Software\\Classes\\" + widen(prog)));
    EXPECT_TRUE(reg_missing(L"Software\\Classes\\" + widen(ext)));
    EXPECT_TRUE(reg_missing(L"Software\\Classes\\" + widen(scheme)));
    EXPECT_TRUE(reg_missing(L"Software\\Classes\\MIME\\Database\\Content Type\\" + widen(mime)));
    EXPECT_TRUE(reg_missing_value(L"Software\\RegisteredApplications", widen(prog)));
    const zui::plugin::OpenerReport after = services.query_opener(opener_query(ext));
    EXPECT_TRUE(after.ran);
    EXPECT_FALSE(after.ok);
    fs::remove(probe_out);
#endif
}

TEST(PluginPlatform, HelpersAndWindowsServices) {
    zui::shell_layout::Roots roots;
    roots.home = "/home/ada";
    EXPECT_EQ(zui::shell_layout::linux_user_data(roots, "demo"), "/home/ada/.local/share/zui/demo");
    EXPECT_EQ(zui::shell_layout::linux_cache(roots, "demo"), "/home/ada/.cache/zui/demo");
    roots.xdg_data_home = "/xdg/data";
    roots.xdg_cache_home = "/xdg/cache";
    EXPECT_EQ(zui::shell_layout::linux_user_data(roots, "demo"), "/xdg/data/zui/demo");
    EXPECT_EQ(zui::shell_layout::linux_cache(roots, "demo"), "/xdg/cache/zui/demo");
    roots.home = "/Users/ada";
    EXPECT_EQ(zui::shell_layout::macos_user_data(roots, "demo"),
              "/Users/ada/Library/Application Support/zui/demo");
    EXPECT_EQ(zui::shell_layout::macos_cache(roots, "demo"), "/Users/ada/Library/Caches/zui/demo");
    roots.ios_container = "/var/mobile/Containers/Data/Application/ABC";
    EXPECT_EQ(zui::shell_layout::ios_user_data(roots, "demo"),
              "/var/mobile/Containers/Data/Application/ABC/Library/Application Support/zui/demo");
    EXPECT_EQ(zui::shell_layout::ios_cache(roots, "demo"),
              "/var/mobile/Containers/Data/Application/ABC/Library/Caches/zui/demo");
    roots.android_files = "/data/user/0/com.zui/files";
    roots.android_cache = "/data/user/0/com.zui/cache";
    EXPECT_EQ(zui::shell_layout::android_user_data(roots, "demo"), "/data/user/0/com.zui/files/zui/demo");
    EXPECT_EQ(zui::shell_layout::android_cache(roots, "demo"), "/data/user/0/com.zui/cache/zui/demo");
    EXPECT_EQ(zui::shell_layout::linux_user_data(roots, ""), "/xdg/data/zui/app");
    EXPECT_STREQ(zui::shell_layout::ios_desktop_data_leaf(), "zui-ios-data");
    EXPECT_STREQ(zui::shell_layout::ios_desktop_cache_leaf(), "zui-ios-cache");
    EXPECT_STREQ(zui::shell_layout::android_desktop_data_leaf(), "zui-android-data");
    EXPECT_STREQ(zui::shell_layout::android_desktop_cache_leaf(), "zui-android-cache");

    zui::FileFilter markdown;
    markdown.label = "Markdown";
    markdown.patterns = "md;txt";
    const std::vector<zui::FileFilter> filters{markdown};
    EXPECT_EQ(zui::pick_detail::DialogCommand::zenity_filters(filters),
              " --file-filter='Markdown | *.md *.txt'");
    EXPECT_EQ(zui::pick_detail::DialogCommand::kdialog_filter(filters), "Markdown|*.md *.txt");
    EXPECT_EQ(zui::pick_detail::DialogCommand::win32_patterns("md;txt"), "*.md;*.txt");
    EXPECT_EQ(zui::pick_detail::DialogCommand::win32_patterns("*"), "*.*");
    EXPECT_EQ(zui::pick_detail::DialogCommand::win32_patterns(""), "*.");
    zui::FileDialogOptions save;
    save.title = "Save";
    save.filters.push_back(zui::FileFilter{"", "md"});
    save.multi = false;
    EXPECT_EQ(zui::pick_detail::DialogCommand::applescript_open(save),
              "POSIX path of (choose file with prompt \"Save\" of type {\"md\"})");
    EXPECT_EQ(zui::pick_detail::DialogCommand::uti_for("png"), "public.png");
    EXPECT_EQ(zui::pick_detail::DialogCommand::uti_for("md"), "public.plain-text");
    EXPECT_EQ(zui::pick_detail::DialogCommand::mime_for("png"), "image/png");

    const std::uint8_t pixels[] = {1, 2, 3, 255, 4, 5, 6, 128, 7, 8, 9, 255, 10, 11, 12, 64};
    const std::vector<std::uint8_t> bmp = zui::clipboard_bmp::encode(2, 2, pixels, sizeof(pixels));
    const zui::ClipboardImage decoded = zui::clipboard_bmp::decode(bmp.data(), bmp.size());
    EXPECT_EQ(decoded.status, zui::ClipboardStatus::Ok);
    EXPECT_EQ(decoded.width, 2);
    EXPECT_EQ(decoded.height, 2);
    EXPECT_EQ(decoded.rgba, std::vector<std::uint8_t>(pixels, pixels + sizeof(pixels)));

    std::size_t media_dex_size = 0;
    const std::uint8_t* media_dex = zui::media_key_android::callback_dex(media_dex_size);
    EXPECT_TRUE(zui::android_dex::payload_is_dex(media_dex, media_dex_size));
    EXPECT_TRUE(zui::android_dex::payload_contains(media_dex, media_dex_size, "Lzui/ZuiMediaCallback;"));
    EXPECT_TRUE(zui::android_dex::payload_contains(media_dex, media_dex_size, "nativeOnPlay"));
    EXPECT_TRUE(zui::android_dex::payload_contains(media_dex, media_dex_size, "nativeOnNext"));
    EXPECT_TRUE(zui::android_dex::payload_contains(media_dex, media_dex_size, "nativeOnPrevious"));
    EXPECT_TRUE(zui::android_dex::payload_contains(media_dex, media_dex_size, "onSkipToNext"));
    EXPECT_TRUE(zui::android_dex::payload_contains(media_dex, media_dex_size, "onSkipToPrevious"));
    std::size_t hotkey_dex_size = 0;
    const std::uint8_t* hotkey_dex = zui::hotkey_android::callback_dex(hotkey_dex_size);
    EXPECT_TRUE(zui::android_dex::payload_is_dex(hotkey_dex, hotkey_dex_size));
    EXPECT_TRUE(zui::android_dex::payload_contains(hotkey_dex, hotkey_dex_size, "Lzui/ZuiHotkeyInvoke;"));
    EXPECT_TRUE(zui::android_dex::payload_contains(hotkey_dex, hotkey_dex_size, "Ljava/lang/reflect/InvocationHandler;"));
    EXPECT_TRUE(zui::android_dex::payload_contains(hotkey_dex, hotkey_dex_size, "nativeInvoke"));
    EXPECT_TRUE(zui::android_dex::payload_contains(hotkey_dex, hotkey_dex_size, "Ljava/lang/reflect/Method;"));

    const std::string plist = zui::association_detail::AssociationText::ios_plist("png", "image/png", "zui");
    EXPECT_NE(plist.find("CFBundleDocumentTypes"), std::string::npos);
    EXPECT_NE(plist.find("CFBundleURLSchemes"), std::string::npos);
    EXPECT_NE(plist.find("png"), std::string::npos);
    EXPECT_NE(plist.find("zui"), std::string::npos);
    const std::string manifest = zui::association_detail::AssociationText::android_manifest("png", "image/png", "zui");
    EXPECT_NE(manifest.find("android.intent.action.VIEW"), std::string::npos);
    EXPECT_NE(manifest.find("android:scheme=\"zui\""), std::string::npos);

    zui::plugin::OpenerSpec spec;
    spec.app_name = "Zui";
    spec.prog_id = "Zui.Opener";
    spec.executable = "C:/app/zui.exe";
    spec.extension = "png";
    spec.mime = "image/png";
    spec.scheme = "zui";
    const zui::association_detail::Record record = zui::association_detail::build_record(spec);
    EXPECT_NE(record.desktop_entry.find("[Desktop Entry]"), std::string::npos);
    EXPECT_NE(record.desktop_entry.find("Name=Zui"), std::string::npos);
    EXPECT_NE(record.desktop_entry.find("Exec=\"C:/app/zui.exe\" %f"), std::string::npos);
    EXPECT_NE(record.desktop_entry.find("MimeType=image/png;x-scheme-handler/zui;"), std::string::npos);
    EXPECT_NE(record.desktop_entry.find("X-Zui-Extension=.png"), std::string::npos);
    EXPECT_EQ(record.desktop_name, "Zui.Opener.desktop");

    zui::plugin::OpenerSpec missing_exe = spec;
    missing_exe.executable = {};
    const zui::association_detail::Prepared no_exe = zui::association_detail::prepare(missing_exe);
    EXPECT_FALSE(no_exe.ready);
    EXPECT_TRUE(no_exe.report.ran);
    zui::plugin::OpenerSpec missing_prog = spec;
    missing_prog.prog_id = {};
    const zui::association_detail::Prepared no_prog = zui::association_detail::prepare(missing_prog);
    EXPECT_FALSE(no_prog.ready);
    EXPECT_TRUE(no_prog.report.ran);
    zui::plugin::OpenerSpec missing_both = spec;
    missing_both.extension = {};
    missing_both.scheme = {};
    const zui::association_detail::Prepared no_type = zui::association_detail::prepare(missing_both);
    EXPECT_FALSE(no_type.ready);
    EXPECT_TRUE(no_type.report.ran);
    const zui::association_detail::Prepared ready = zui::association_detail::prepare(spec);
    EXPECT_TRUE(ready.ready);
    EXPECT_FALSE(ready.report.ran);

    EXPECT_EQ(zui::instance_detail::win32_mutex_name("demo"), "Local\\zui-instance-demo");
    EXPECT_EQ(zui::instance_detail::win32_pipe_name("demo"), "\\\\.\\pipe\\zui-instance-demo");
    EXPECT_EQ(zui::instance_detail::linux_socket_path("", "demo"), "/tmp/zui-instance-demo.sock");
    EXPECT_EQ(zui::instance_detail::linux_socket_path("/run/user/1000", "demo"),
              "/run/user/1000/zui-instance-demo.sock");
    EXPECT_EQ(zui::instance_detail::macos_socket_path("/Users/ada", "demo"),
              "/Users/ada/Library/Caches/zui-instance-demo.sock");
    EXPECT_EQ(zui::instance_detail::ios_socket_path("/var/mobile/Containers/Data/Application/ABC", "demo"),
              "/var/mobile/Containers/Data/Application/ABC/tmp/zui-instance-demo.sock");
    EXPECT_EQ(zui::instance_detail::android_socket_path("/data/user/0/com.zui/files", "demo"),
              "/data/user/0/com.zui/files/zui-instance-demo.sock");
    EXPECT_EQ(zui::instance_detail::ios_notification_name("demo"), "zui.instance.demo");

    zui::plugin::Hotkey chord_a;
    chord_a.key = "F24";
    chord_a.control = true;
    chord_a.alt = true;
    chord_a.shift = true;
    zui::plugin::Hotkey chord_b;
    chord_b.key = "K";
    chord_b.meta = true;
    const zui::hotkey_chord::Chord encoded_a = zui::hotkey_chord::encode(chord_a);
    const zui::hotkey_chord::Chord encoded_b = zui::hotkey_chord::encode(chord_b);
    EXPECT_TRUE(encoded_a.win32);
    EXPECT_TRUE(encoded_a.linux);
    EXPECT_FALSE(encoded_a.macos);
    EXPECT_FALSE(encoded_a.android);
    EXPECT_TRUE(encoded_b.win32);
    EXPECT_TRUE(encoded_b.linux);
    EXPECT_TRUE(encoded_b.macos);
    EXPECT_TRUE(encoded_b.android);
    EXPECT_TRUE(encoded_b.ios_input);
    EXPECT_FALSE(zui::hotkey_chord::same(encoded_a, encoded_b));
    EXPECT_NE(encoded_a.label.find("F24"), std::string::npos);
    EXPECT_NE(encoded_a.label.find("ctrl+"), std::string::npos);
    EXPECT_NE(encoded_b.label.find("K"), std::string::npos);
    EXPECT_NE(encoded_b.label.find("meta+"), std::string::npos);
    EXPECT_NE(encoded_a.linux_keysym, encoded_b.linux_keysym);
    EXPECT_NE(encoded_a.win32_vk, encoded_b.win32_vk);
    EXPECT_NE(encoded_a.linux_mods, encoded_b.linux_mods);
    EXPECT_EQ(encoded_a.linux_keysym, 0xffd5u);
    EXPECT_EQ(encoded_b.linux_keysym, static_cast<unsigned>('k'));
    EXPECT_EQ(encoded_b.macos_keycode, 0x28u);
    EXPECT_EQ(encoded_b.android_keycode, 29 + ('K' - 'A'));
    zui::plugin::Hotkey chord_same = chord_b;
    chord_same.key = "k";
    EXPECT_TRUE(zui::hotkey_chord::same(encoded_b, zui::hotkey_chord::encode(chord_same)));
    zui::plugin::Hotkey chord_unknown;
    chord_unknown.key = "not-a-key";
    const zui::hotkey_chord::Chord unknown = zui::hotkey_chord::encode(chord_unknown);
    EXPECT_FALSE(unknown.win32);
    EXPECT_FALSE(unknown.linux);
    EXPECT_FALSE(unknown.macos);
    EXPECT_FALSE(unknown.android);
    EXPECT_FALSE(unknown.reason.empty());
    EXPECT_NE(unknown.label.find("not-a-key"), std::string::npos);
    EXPECT_FALSE(zui::hotkey_chord::same(unknown, encoded_a));
    EXPECT_FALSE(zui::hotkey_chord::same(unknown, encoded_b));

    const zui::tray_detail::Item tray_item = zui::tray_detail::prepare("ZuiTip");
    EXPECT_TRUE(tray_item.ready);
    EXPECT_EQ(tray_item.tip, "ZuiTip");
    EXPECT_NE(tray_item.id.find("ZuiTip"), std::string::npos);
    const zui::tray_detail::Item tray_other = zui::tray_detail::prepare("Other");
    EXPECT_TRUE(tray_other.ready);
    EXPECT_NE(tray_item.id, tray_other.id);
    const zui::tray_detail::Item tray_empty = zui::tray_detail::prepare("");
    EXPECT_FALSE(tray_empty.ready);
    EXPECT_FALSE(tray_empty.reason.empty());

#if defined(_WIN32)
    const zui::HostServices services = session_services();
    const zui::plugin::OsResult toast = services.notify("PlatTitle", "PlatBody");
    ASSERT_TRUE(toast.ok) << toast.os_error;
    EXPECT_EQ(toast.title, "PlatTitle");
    EXPECT_EQ(toast.body, "PlatBody");

    const zui::plugin::OsResult badge = services.tray_badge(6);
    ASSERT_TRUE(badge.ok) << badge.os_error;
    EXPECT_EQ(services.tray_badge_count(), 6);
    EXPECT_EQ(badge.title, "6");

    zui::App dirs(0, nullptr);
    const std::string dir_id = "zui-goal-dirs-" + std::to_string(GetCurrentProcessId());
    dirs.host().set_app_id(dir_id);
    const std::string data_dir = dirs.host().user_data_dir();
    const std::string cache_dir = dirs.host().cache_dir();
    EXPECT_NE(data_dir.find(dir_id), std::string::npos) << data_dir;
    EXPECT_NE(cache_dir.find(dir_id), std::string::npos) << cache_dir;
    EXPECT_NE(data_dir, cache_dir);
    EXPECT_TRUE(fs::is_directory(data_dir));
    EXPECT_TRUE(fs::is_directory(cache_dir));

    std::thread canceler([] { cancel_file_dialog(L"zui-goal-dialog-cancel"); });
    zui::FileDialogOptions dialog;
    dialog.title = "zui-goal-dialog-cancel";
    const zui::PickPaths picked = dirs.host().pick_open(dialog);
    canceler.join();
    EXPECT_NE(picked.status, zui::PickStatus::Ok);
    EXPECT_TRUE(picked.paths.empty());

    const std::string identity = "zui-plat-" + std::to_string(GetCurrentProcessId());
    ASSERT_TRUE(services.claim_single_instance(identity));
    const DWORD sent = run_command(L"\"" + exe_path() + L"\" --zui-second " + widen(identity) + L" platform-args");
    EXPECT_EQ(sent, 0u);
    std::string args;
    for (int i = 0; i < 50 && args.empty(); ++i) {
        args = services.take_instance_args(identity);
        if (args.empty()) {
            Sleep(20);
        }
    }
    EXPECT_EQ(args, "platform-args");

    const std::string pid = std::to_string(GetCurrentProcessId());
    const std::string prog = "ZuiPlat" + pid;
    const std::string ext = ".zpp" + pid;
    const std::string scheme = "zpp" + pid;
    const std::string self = narrow(exe_path());
    struct PlatCleanup {
        std::string prog;
        std::string ext;
        std::string scheme;
        ~PlatCleanup() {
            (void)session_services().unregister_opener(opener_query(ext, "image/png", scheme, prog));
        }
    } cleanup{prog, ext, scheme};
    zui::plugin::OpenerSpec live;
    live.app_name = "Zui";
    live.prog_id = prog;
    live.executable = self;
    live.extension = ext;
    live.mime = "image/png";
    live.scheme = scheme;
    const zui::plugin::OpenerReport report = services.register_opener(live);
    ASSERT_TRUE(report.ran) << report.os_error;
    ASSERT_TRUE(report.ok) << report.os_error;
    EXPECT_EQ(report.app_name, "Zui");
    EXPECT_NE(report.desktop_entry.find("Name=Zui"), std::string::npos);
    EXPECT_EQ(report.desktop_name, prog + ".desktop");
    const zui::plugin::OpenerReport queried = services.query_opener(opener_query(ext));
    EXPECT_TRUE(queried.ok);
    EXPECT_EQ(queried.executable, live.executable);

    bool text_set = false;
    for (int attempt = 0; attempt < 40 && !text_set; ++attempt) {
        text_set = zui::backends::set_clipboard_text_win32("plugin-platform-text");
        if (!text_set) {
            Sleep(20);
        }
    }
    ASSERT_TRUE(text_set);
    const zui::ClipboardText clip = zui::backends::clipboard_text_win32();
    EXPECT_EQ(clip.status, zui::ClipboardStatus::Ok);
    EXPECT_EQ(clip.text, "plugin-platform-text");

    bool image_set = false;
    for (int attempt = 0; attempt < 40 && !image_set; ++attempt) {
        image_set = zui::backends::set_clipboard_image_win32(2, 2, pixels, sizeof(pixels));
        if (!image_set) {
            Sleep(20);
        }
    }
    ASSERT_TRUE(image_set);
    const zui::ClipboardImage stored = zui::backends::clipboard_image_win32();
    EXPECT_EQ(stored.status, zui::ClipboardStatus::Ok);
    EXPECT_EQ(stored.width, 2);
    EXPECT_EQ(stored.height, 2);
    EXPECT_EQ(stored.rgba, std::vector<std::uint8_t>(pixels, pixels + sizeof(pixels)));
    const zui::ClipboardText text_after_image = zui::backends::clipboard_text_win32();
    EXPECT_EQ(text_after_image.status, zui::ClipboardStatus::Ok);
    EXPECT_EQ(text_after_image.text, "plugin-platform-text");
#endif
}
