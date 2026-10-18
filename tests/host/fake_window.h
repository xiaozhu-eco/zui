#ifndef ZUI_TESTS_HOST_FAKE_WINDOW_H
#define ZUI_TESTS_HOST_FAKE_WINDOW_H

#include "backends/window.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace zui::test {

/// Scriptable IWindow for host / app_interact tests.
///
/// Tests stage mouse input on `pointer_` — the one-pointer shape the backends reported before
/// PR-INPUT-1 — and `poll()` folds it into the multi-contact `pointers()` frame the engine reads.
/// A test that needs several contacts at once (mouse plus a finger) injects a whole
/// `backends::PointerFrame` instead.
class FakeWindow final : public backends::IWindow {
public:
    /// The pre-PR-INPUT-1 single-pointer staging shape. Writing the fields directly keeps the
    /// windowed interaction rigs (`tests/app_interact/handler_rig.h`) source-compatible.
    struct StagedPointer {
        float x = 0;
        float y = 0;
        bool left_down = false;
        bool left_pressed = false;
        bool left_released = false;
        bool right_down = false;
        bool right_pressed = false;
        bool right_released = false;
        bool middle_down = false;
        bool middle_pressed = false;
        bool middle_released = false;
        int buttons = 0;
        int click_count = 1;
        float wheel_x = 0;
        float wheel_y = 0;
        bool ctrl = false;
        bool shift = false;
        bool alt = false;
        bool meta = false;
    };

    int discard_calls = 0;
    StagedPointer pointer_{};
    backends::PointerFrame pointers_{};
    /// Polled pads. Level state: tests push a pad and it stays until they clear it.
    backends::GamepadFrame gamepads_{};
    backends::TextInputFrame text_{};
    std::string clipboard_;
    bool clipboard_ok_ = true;

    void inject(backends::TextInputFrame frame) { pending_text_ = std::move(frame); }

    /// Publish one pointer frame for the next poll; it replaces the staged mouse for that frame.
    void inject(backends::PointerFrame frame) { pending_pointers_ = std::move(frame); }

    /// The first contact of @p kind in the published frame, or nullptr.
    const backends::PointerContact* contact(backends::PointerKind kind) const {
        for (const backends::PointerContact& contact : pointers_.contacts) {
            if (contact.kind == kind) {
                return &contact;
            }
        }
        return nullptr;
    }

    void inject_pending_edges() {
        pointer_.left_pressed = true;
        pointer_.left_released = true;
        pointer_.left_down = true;
        pointer_.buttons = 1;
        pointer_.wheel_x = 1.f;
        pointer_.wheel_y = -2.f;
        publish_staged();
        text_.text = "pending";
        text_.backspace = 1;
        text_.enter = true;
        backends::KeyPress press;
        press.key = "a";
        text_.key_events.push_back(press);
    }

    bool create(const backends::WindowDesc&) override { return true; }
    void destroy() override {}

    // One pending TextInputFrame becomes text() for this poll. The next poll with no new
    // inject publishes an empty text frame. Pointer position is kept; edges are one-shot.
    bool poll() override {
        if (pending_pointers_.has_value()) {
            pointers_ = std::move(*pending_pointers_);
            pending_pointers_.reset();
        } else {
            if (!pending_text_.has_value()) {
                pointer_.left_pressed = false;
                pointer_.left_released = false;
                pointer_.right_pressed = false;
                pointer_.right_released = false;
                pointer_.middle_pressed = false;
                pointer_.middle_released = false;
                pointer_.wheel_x = 0.f;
                pointer_.wheel_y = 0.f;
            }
            publish_staged();
        }
        if (pending_text_.has_value()) {
            text_ = std::move(*pending_text_);
            pending_text_.reset();
        } else {
            text_ = {};
        }
        return true;
    }

    /// One close request, then cleared: enough to drive the `beforeunload` dispatch.
    bool close_requested_ = false;
    bool take_close_request() override {
        if (!close_requested_) {
            return false;
        }
        close_requested_ = false;
        return true;
    }
    /// The viewport the borrowed-window seam lays out in. A test that needs the 1100x720 harness
    /// viewport sets these before `Access::attach`. Defaults keep the historical 800x600.
    int width_ = 800;
    int height_ = 600;
    void framebuffer_size(int& width, int& height) const override {
        width = width_;
        height = height_;
    }
    /// The borrowed window is the viewport. A size request has to change the next frame.
    void set_window_size(int w, int h) override {
        if (w > 0 && h > 0) {
            width_ = w;
            height_ = h;
        }
    }
    const backends::PointerFrame& pointers() const override { return pointers_; }
    const backends::GamepadFrame& gamepads() const override { return gamepads_; }
    const backends::TextInputFrame& text() const override { return text_; }
    void set_ime(bool) override {}
    const char* name() const override { return "fake"; }

    void discard_pending_input() override {
        ++discard_calls;
        pointer_.left_pressed = false;
        pointer_.left_released = false;
        pointer_.right_pressed = false;
        pointer_.right_released = false;
        pointer_.middle_pressed = false;
        pointer_.middle_released = false;
        pointer_.wheel_x = 0;
        pointer_.wheel_y = 0;
        publish_staged();
        text_ = {};
        pending_text_.reset();
        pending_pointers_.reset();
        dropped_paths_.clear();
    }

    std::vector<std::string> take_dropped_paths() override {
        std::vector<std::string> out;
        out.swap(dropped_paths_);
        return out;
    }

    void queue_dropped_path(std::string path) { dropped_paths_.push_back(std::move(path)); }

    ClipboardText clipboard_text() override {
        if (!clipboard_ok_) {
            return {};
        }
        return {ClipboardStatus::Ok, clipboard_};
    }

    bool set_clipboard_text(std::string_view text) override {
        if (!clipboard_ok_) {
            return false;
        }
        clipboard_.assign(text);
        return true;
    }

    WindowIcon icon_{};
    bool has_icon_ = false;
    bool minimized_ = false;
    bool maximized_ = false;
    bool fullscreen_ = false;
    bool topmost_ = false;
    backends::HostVisibility visibility_ = backends::HostVisibility::Visible;
    bool visibility_changed_ = false;
    DisplayIdentity display_{};

    bool set_icon(const WindowIcon& icon) override {
        if (!icon.valid()) {
            return false;
        }
        icon_ = icon;
        has_icon_ = true;
        return true;
    }

    bool minimize() override {
        minimized_ = true;
        maximized_ = false;
        return true;
    }

    bool set_maximized(bool on) override {
        maximized_ = on;
        minimized_ = false;
        return true;
    }

    bool set_fullscreen(bool on) override {
        fullscreen_ = on;
        return true;
    }

    bool set_topmost(bool on) override {
        topmost_ = on;
        return true;
    }

    bool minimized() const override { return minimized_; }
    bool maximized() const override { return maximized_; }
    bool fullscreen() const override { return fullscreen_; }
    bool topmost() const override { return topmost_; }
    /// The host visibility channel the frame loop turns into `visibilitychange`; the desktop
    /// backends fill it from focus / occlusion / minimize, tests inject it here.
    backends::HostVisibility visibility() const override { return visibility_; }
    bool take_visibility_change() override {
        const bool changed = visibility_changed_;
        visibility_changed_ = false;
        return changed;
    }
    void set_visibility_for_test(backends::HostVisibility visibility) override {
        visibility_changed_ = visibility_changed_ || visibility != visibility_;
        visibility_ = visibility;
    }
    DisplayIdentity display() const override { return display_; }

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

private:
    /// Fold the staged single pointer into the frame the engine reads: one mouse contact.
    void publish_staged() {
        pointers_ = {};
        backends::PointerContact mouse;
        mouse.id = 1;
        mouse.kind = backends::PointerKind::Mouse;
        mouse.primary = true;
        mouse.x = pointer_.x;
        mouse.y = pointer_.y;
        mouse.buttons = pointer_.buttons;
        mouse.down = pointer_.left_down || pointer_.right_down || pointer_.middle_down ||
                     pointer_.buttons != 0;
        mouse.pressed = pointer_.left_pressed || pointer_.right_pressed || pointer_.middle_pressed;
        mouse.released =
            pointer_.left_released || pointer_.right_released || pointer_.middle_released;
        mouse.changed_button =
            ((pointer_.left_pressed || pointer_.left_released) ? 1 : 0) |
            ((pointer_.right_pressed || pointer_.right_released) ? 2 : 0) |
            ((pointer_.middle_pressed || pointer_.middle_released) ? 4 : 0);
        mouse.click_count = pointer_.click_count > 0 ? pointer_.click_count : 1;
        pointers_.contacts.push_back(mouse);
        pointers_.wheel_x = pointer_.wheel_x;
        pointers_.wheel_y = pointer_.wheel_y;
        pointers_.ctrl = pointer_.ctrl;
        pointers_.shift = pointer_.shift;
        pointers_.alt = pointer_.alt;
        pointers_.meta = pointer_.meta;
    }

    std::optional<backends::TextInputFrame> pending_text_;
    std::optional<backends::PointerFrame> pending_pointers_;
    std::vector<std::string> dropped_paths_;
};

}  // namespace zui::test

#endif
