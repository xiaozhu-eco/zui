#ifndef ZUI_BACKENDS_WINDOW_H
#define ZUI_BACKENDS_WINDOW_H

#include <zui/host_surface.h>
#include <zui/host_types.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace zui {
namespace backends {

enum class GfxApi { Vulkan, OpenGL, OpenGLES };

struct WindowDesc {
    std::string title = "zui";
    int width = 800;
    int height = 600;
    GfxApi api = GfxApi::Vulkan;
    /// Create the window without showing it. The probe uses one as a GL context host and
    /// draws into an offscreen target. Product windows leave this false.
    bool hidden = false;
};

/// Which device produced a pointer contact. Devices are **not** exclusive: a desktop can hold a
/// mouse, a touchscreen and a pen at once, and a phone can have a mouse attached. Each contact is
/// reported on its own; the mouse is one pointer among them (`PointerKind::Mouse`), not a base
/// device that the others stand in for.
enum class PointerKind : std::uint8_t { Mouse, Pen, Touch };

/// One pointer contact of this frame, in CSS logical pixels.
///
/// `id` is stable while the contact lives (the mouse keeps 1; fingers and pens carry an id from
/// the OS). `primary` is Pointer Events' `isPrimary`: the first contact of its kind in the frame.
/// `pressed` / `released` / `cancelled` are one-frame edges; `down` is "contact active" (a button
/// held for the mouse, finger or pen tip on the surface).
struct PointerContact {
    std::int32_t id = 1;
    PointerKind kind = PointerKind::Mouse;
    bool primary = true;
    float x = 0;
    float y = 0;
    /// 0..1; a device without a pressure axis reports 0.5 (the Pointer Events default).
    float pressure = 0.5f;
    /// MouseEvent.buttons bitmask: 1=left, 2=right, 4=middle. Touch/pen use bit 1 for "in contact".
    int buttons = 0;
    bool down = false;
    bool pressed = false;
    bool released = false;
    bool cancelled = false;
    /// Which bit of `buttons` changed on `pressed` / `released` (1=left, 2=right, 4=middle, 0=none).
    int changed_button = 0;
    /// Meaningful on the frame a *pressed edge is set (1 = single, 2 = double, …).
    int click_count = 1;
};

/// Every pointer the window saw this frame plus the wheel delta and keyboard modifiers.
///
/// The wheel belongs to the mouse (or a trackpad / touch surface that reports it), so it stays
/// frame-level instead of being smeared onto every finger.
struct PointerFrame {
    std::vector<PointerContact> contacts;
    float wheel_x = 0;
    float wheel_y = 0;
    /// Keyboard modifiers sampled each poll (for pointer / wheel payloads).
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
    bool meta = false;
};

/// Every gamepad the window knows this frame, indexed the way `App::gamepad(index)` reads them.
/// Gamepads are a polling channel (the W3C Gamepad API is polled too), not a second event stream.
struct GamepadFrame {
    std::vector<GamepadState> pads;
};

/// CSS-px insets (notch / home indicator / system bars). Zero until a mobile host reports them.
struct DisplayInsets {
    float top = 0;
    float right = 0;
    float bottom = 0;
    float left = 0;
};

/// Host window visibility for lifecycle (`visibilitychange`). Desktop maps focus/occluded/
/// minimized; mobile hosts inject via `set_visibility_for_test`.
enum class HostVisibility { Visible, Hidden };

/// Screen / window orientation for `orientationchange`. Desktop may derive from framebuffer
/// aspect; mobile hosts inject via `set_orientation_for_test` until OS wiring lands.
enum class HostOrientation { Unknown, Portrait, Landscape };

/// One key-down of a frame, in arrival order, named the way `KeyboardEvent.key` spells it
/// ("Enter" / "Escape" / "ArrowDown" / "s" / " "). Keys the backend cannot name are absent:
/// a `onkeydown` firing is only possible for a key the backend can spell.
struct KeyPress {
    std::string key;
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
    bool meta = false;
    bool composing = false;
};

struct TextInputFrame {
    std::string text;
    int backspace = 0;
    int del = 0;
    int left = 0;
    int right = 0;
    int home = 0;
    int end = 0;
    bool enter = false;
    bool escape = false;
    int up = 0;
    int down = 0;
    int page_up = 0;
    int page_down = 0;
    int space = 0;
    int tab = 0;
    /// Modifier state of the last key-down of this frame; the counters above drive the editing
    /// defaults, this list drives `onkeydown`.
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
    bool meta = false;
    /// True while an IME composition is in progress (SDL text-editing events, or the shared
    /// composer the GLFW backend feeds). Enter during composition must not insert a newline.
    bool composing = false;
    /// 0 none, 1 compositionstart, 2 compositionupdate, 3 compositionend. One phase per frame.
    int composition_phase = 0;
    /// Live preedit. Not an insert: the editor replaces its open composition range with this
    /// string. `text` stays the committed characters and is inserted once.
    std::string composition;
    std::vector<KeyPress> key_events;
    /// Key-ups of this frame, same naming as key_events; drives `onkeyup` (no editing defaults).
    std::vector<KeyPress> key_up_events;
};

class IWindow {
public:
    virtual ~IWindow() = default;

    virtual bool create(const WindowDesc& desc) = 0;
    virtual void destroy() = 0;
    /// Pump OS events. @return false when the window can no longer be drawn to.
    virtual bool poll() = 0;
    /// Consumes the user's request to close the window (title-bar X, SDL_EVENT_QUIT). The
    /// request is reported once and not again until the user asks again. Closing is a dispatch,
    /// not a loop end: the app decides, through `<body onbeforeunload>`, whether to quit.
    virtual bool take_close_request() = 0;
    virtual void framebuffer_size(int& width, int& height) const = 0;
    /// Device pixels per CSS px. 1.f when framebuffer size equals window size.
    virtual float content_scale() const { return 1.f; }
    /// Safe-area / display cutout insets in CSS px. Default zeros (desktop / stubs).
    virtual DisplayInsets safe_area_insets() const { return {}; }
    /// Virtual keyboard insets in CSS px. Default zeros (desktop; mobile reports bottom).
    virtual DisplayInsets keyboard_insets() const { return {}; }
    /// OS / host window title (UTF-8). Default no-op.
    virtual void set_title(std::string_view) {}
    /// Resize client area in logical CSS px. Backends convert with content_scale if needed.
    /// This is the only window-geometry setter. Chrome calls below are state, not a second size API.
    virtual void set_window_size(int /*w*/, int /*h*/) {}
    /// Runtime window icon (RGBA8888). Default rejects. Not a second close path:
    /// closing stays `take_close_request`.
    virtual bool set_icon(const WindowIcon& icon) {
        (void)icon;
        return false;
    }
    /// Minimize the window. Default rejects.
    virtual bool minimize() { return false; }
    /// Maximize, or restore when @p on is false. Default rejects.
    virtual bool set_maximized(bool on) {
        (void)on;
        return false;
    }
    /// Enter or leave fullscreen. Default rejects. Not a geometry setter.
    virtual bool set_fullscreen(bool on) {
        (void)on;
        return false;
    }
    /// Keep the window above others, or clear that. Default rejects.
    virtual bool set_topmost(bool on) {
        (void)on;
        return false;
    }
    virtual bool minimized() const { return false; }
    virtual bool maximized() const { return false; }
    virtual bool fullscreen() const { return false; }
    virtual bool topmost() const { return false; }
    /// Display the window is on. Default is unknown.
    virtual DisplayIdentity display() const { return {}; }
    /// Current host visibility. Default Visible.
    virtual HostVisibility visibility() const { return HostVisibility::Visible; }
    /// Edge: true once when visibility changed since the last poll consume.
    virtual bool take_visibility_change() { return false; }
    /// Current orientation. Default Unknown.
    virtual HostOrientation orientation() const { return HostOrientation::Unknown; }
    /// Edge: true once when orientation changed since the last poll consume.
    virtual bool take_orientation_change() { return false; }
    /// Test / embedder inject for mobile stubs. Default no-op.
    virtual void set_safe_area_insets_for_test(DisplayInsets) {}
    virtual void set_keyboard_bottom_for_test(float) {}
    virtual void set_visibility_for_test(HostVisibility) {}
    virtual void set_orientation_for_test(HostOrientation) {}
    virtual const PointerFrame& pointers() const = 0;
    /// Polled gamepads for this frame. Default: none (GLFW and the stubs have no gamepad channel).
    virtual const GamepadFrame& gamepads() const { return empty_gamepads_; }
    virtual const TextInputFrame& text() const = 0;
    virtual void set_ime(bool enabled) = 0;
    /// IME candidate/caret rect in window/CSS px. Default no-op (GLFW / hosts without IME area).
    virtual void set_ime_rect(float x, float y, float w, float h) {
        (void)x;
        (void)y;
        (void)w;
        (void)h;
    }
    virtual const char* name() const = 0;

    /// After a nested OS modal pump returns: flush queued input and clear edge
    /// caches (pressed/released/wheel/key_events). Wired in PR-2.
    virtual void discard_pending_input() {}
    /// Opaque native owner token for IPlatform::pick_* (HWND on Win32).
    virtual NativeWindowId native_handle() const { return {}; }

    /// UTF-8 text clipboard. Default is Unsupported (no host clipboard).
    virtual ClipboardText clipboard_text() { return {}; }
    /// @return false when unsupported or the platform write failed.
    virtual bool set_clipboard_text(std::string_view) { return false; }

    /// Image clipboard. Default is Unsupported. A successful write leaves the text clipboard
    /// as it was.
    virtual ClipboardImage clipboard_image() { return {}; }
    virtual bool set_clipboard_image(const ClipboardImage& /*image*/) { return false; }

    /// Test seam: the next poll() publishes this composition after it clears the frame,
    /// so a composition is not wiped by that clear. Default no-op.
    virtual void inject_composition_for_test(int /*phase*/, std::string /*text*/, bool /*composing*/) {}

    /// System cursor from the CSS `cursor` subset in host_types.h. Default no-op.
    virtual void set_cursor(Cursor) {}

    /// Drain queued UTF-8 file paths from OS drops. App turns this into one `ondrop` fire
    /// (HOST PR-12); not an A-tier API. Default is empty. Tests may override.
    virtual std::vector<std::string> take_dropped_paths() { return {}; }

    /// Queue paths from OS share / open-with (mobile). Same storage as drops; drained by
    /// App via take_dropped_paths → ondrop. Default no-op. Mobile hosts call this when the
    /// OS delivers a file; desktop backends may ignore.
    virtual void inject_opened_paths(std::vector<std::string> paths) { (void)paths; }

    virtual void* gl_proc(const char* name) const = 0;
    virtual void gl_make_current() = 0;
    virtual void gl_swap() = 0;

    virtual bool vulkan_instance_extensions(uint32_t* count, const char* const** names) const = 0;
    virtual bool create_vulkan_surface(void* instance, void** surface) = 0;

private:
    /// What `gamepads()` returns for a backend with no gamepad channel.
    GamepadFrame empty_gamepads_{};
};

}  // namespace backends
}  // namespace zui

#endif
