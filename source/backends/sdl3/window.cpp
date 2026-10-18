#ifdef ZUI_WITH_SDL
#include "backends/hand_cursor.h"
#include "backends/mobile_device.h"
#include "backends/sdl_finger_test.h"
#include "backends/window.h"
#include "backends/ime_composer.h"
#include "ext/slots.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#ifdef ZUI_HAS_VULKAN
#include <vulkan/vulkan.h>
#include <SDL3/SDL_vulkan.h>
#endif

#include <SDL3/SDL_touch.h>

#include "log/log.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cctype>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace zui {
namespace backends {

namespace {

/// MouseEvent.buttons bit for one SDL mouse button; 0 for buttons this seam does not name.
int mouse_button_bit(Uint8 button) {
    switch (button) {
        case SDL_BUTTON_LEFT:
            return 1;
        case SDL_BUTTON_RIGHT:
            return 2;
        case SDL_BUTTON_MIDDLE:
            return 4;
        default:
            return 0;
    }
}

/// `KeyboardEvent.key` spelling of one SDL keysym; empty when this backend has no name for it,
/// in which case the key is not offered as `onkeydown`. Modifier-resolved characters
/// (Shift+1 -> "!") come through the text channel, not from here.
std::string dom_key_name(SDL_Keycode code, bool shift) {
    switch (code) {
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            return "Enter";
        case SDLK_ESCAPE:
            return "Escape";
        case SDLK_BACKSPACE:
            return "Backspace";
        case SDLK_DELETE:
            return "Delete";
        case SDLK_TAB:
            return "Tab";
        case SDLK_LEFT:
            return "ArrowLeft";
        case SDLK_RIGHT:
            return "ArrowRight";
        case SDLK_UP:
            return "ArrowUp";
        case SDLK_DOWN:
            return "ArrowDown";
        case SDLK_HOME:
            return "Home";
        case SDLK_END:
            return "End";
        case SDLK_PAGEUP:
            return "PageUp";
        case SDLK_PAGEDOWN:
            return "PageDown";
        case SDLK_SPACE:
            return " ";
        default:
            break;
    }
    if (code >= SDLK_A && code <= SDLK_Z) {
        const char lower = static_cast<char>('a' + (code - SDLK_A));
        return std::string(1, shift ? static_cast<char>(std::toupper(lower)) : lower);
    }
    if (code >= SDLK_0 && code <= SDLK_9) {
        return std::string(1, static_cast<char>('0' + (code - SDLK_0)));
    }
    if (code >= SDLK_F1 && code <= SDLK_F12) {
        return "F" + std::to_string(1 + (code - SDLK_F1));
    }
    return {};
}

}  // namespace

class Sdl3Window final : public IWindow {
public:
    /// One live finger contact. `pressed` / `released` / `cancelled` are edges of the current
    /// poll; a track with `ended` set is reported one last time and then dropped.
    struct FingerTrack {
        SDL_TouchID touch = 0;
        SDL_FingerID finger = 0;
        float x = 0;
        float y = 0;
        float pressure = 0.5f;
        bool down = false;
        bool pressed = false;
        bool released = false;
        bool cancelled = false;
        bool ended = false;
    };

    /// The most recent pen contact. Stage PR-INPUT-1 keeps one pen at a time; SDL's pen id is
    /// ephemeral, so a fresh contact replaces the previous one.
    struct PenTrack {
        SDL_PenID pen = 0;
        bool active = false;
        float x = 0;
        float y = 0;
        float pressure = 0.5f;
        int buttons = 0;
        bool down = false;
        bool pressed = false;
        bool released = false;
        bool ended = false;
    };

    ~Sdl3Window() override { destroy(); }

    bool create(const WindowDesc& desc) override {
        destroy();
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            ZUI_ERROR("SDL_Init: {}", SDL_GetError());
            return false;
        }
        if (GamepadService* pads = ExtensionRegistry::instance().gamepad()) {
            pads->startup();
        }
        api_ = desc.api;
        // Hidden probe windows stay at pixel density 1 so layout tests are 1:1 CSS px.
        // A visible window asks SDL for a high-density back buffer. On Win32 that flag
        // does not change window units (they stay physical pixels), so the CSS size is
        // applied separately in apply_css_window_size.
        high_density_ = !desc.hidden;
        SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
        if (desc.hidden) {
            flags |= SDL_WINDOW_HIDDEN;
        } else {
            flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
        }
        int win_w = desc.width;
        int win_h = desc.height;
        // Win32 Per-Monitor V2: window units are physical pixels and the display
        // content scale is the CSS factor. Guess it before the window exists so the
        // first size is already CSS * scale. apply_css_window_size then divides by
        // the real pixel density, which keeps macOS (density == display scale) at
        // the requested point size.
        if (high_density_) {
#ifdef _WIN32
            float display = 1.f;
            if (const SDL_DisplayID id = SDL_GetPrimaryDisplay(); id != 0) {
                const float scale = SDL_GetDisplayContentScale(id);
                if (scale > 0.f) {
                    display = scale;
                }
            }
            if (display > 1.01f) {
                win_w = std::max(1, static_cast<int>(std::lround(desc.width * display)));
                win_h = std::max(1, static_cast<int>(std::lround(desc.height * display)));
            }
#endif
        }
        if (desc.api == GfxApi::Vulkan) {
            flags |= SDL_WINDOW_VULKAN;
        } else {
            flags |= SDL_WINDOW_OPENGL;
            if (desc.api == GfxApi::OpenGLES) {
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
            } else {
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
                // 4.3: vector glyphs use a fragment-shader storage buffer. GLES stays at 3.0
                // and does not draw text.
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
            }
            SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
            SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
        }
        window_ = SDL_CreateWindow(desc.title.c_str(), win_w, win_h, flags);
        if (window_ == nullptr) {
            ZUI_ERROR("SDL_CreateWindow: {}", SDL_GetError());
            return false;
        }
        if (desc.api != GfxApi::Vulkan) {
            gl_ = SDL_GL_CreateContext(window_);
            if (gl_ == nullptr) {
                ZUI_ERROR("SDL_GL_CreateContext: {}", SDL_GetError());
                return false;
            }
            SDL_GL_SetSwapInterval(desc.hidden ? 0 : 1);
        }
        // Enabling DROP_FILE also toggles AcceptDragAndDrop on existing windows.
        SDL_SetEventEnabled(SDL_EVENT_DROP_FILE, true);
        apply_css_window_size(desc.width, desc.height);
        note_orientation_from_size(desc.width, desc.height, /*force_edge=*/false);
        // Starting state, not a change. A hidden or minimized window must not report Visible.
        {
            const SDL_WindowFlags shown = SDL_GetWindowFlags(window_);
            if ((shown & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED)) != 0) {
                visibility_ = HostVisibility::Hidden;
            } else {
                visibility_ = HostVisibility::Visible;
            }
            visibility_changed_ = false;
        }
#if (defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__ANDROID__)
        device_install_keyboard_observer();
#endif
        return true;
    }

    void destroy() override {
        if (GamepadService* pads = ExtensionRegistry::instance().gamepad()) {
            pads->shutdown();
        }
        if (cursor_handle_ != nullptr && cursor_handle_ != hand_open_ &&
            cursor_handle_ != hand_closed_) {
            SDL_DestroyCursor(cursor_handle_);
        }
        cursor_handle_ = nullptr;
        if (hand_open_ != nullptr) {
            SDL_DestroyCursor(hand_open_);
            hand_open_ = nullptr;
        }
        if (hand_closed_ != nullptr) {
            SDL_DestroyCursor(hand_closed_);
            hand_closed_ = nullptr;
        }
        if (gl_ != nullptr) {
            SDL_GL_DestroyContext(gl_);
            gl_ = nullptr;
        }
        if (window_ != nullptr) {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }
        frame_ = {};
        fingers_.clear();
        pen_ = PenTrack{};
    }

    bool poll() override {
        if (window_ == nullptr) {
            return false;
        }
        frame_ = {};
        if (GamepadService* pads = ExtensionRegistry::instance().gamepad()) {
            pads->scan();
        }
        for (FingerTrack& track : fingers_) {
            track.pressed = false;
            track.released = false;
            track.cancelled = false;
        }
        pen_.pressed = false;
        pen_.released = false;
        bool mouse_pressed = false;
        bool mouse_released = false;
        int mouse_changed = 0;
        int mouse_clicks = 1;
        text_ = {};
        int win_w = 0;
        int win_h = 0;
        SDL_GetWindowSize(window_, &win_w, &win_h);
        const SDL_WindowID self = SDL_GetWindowID(window_);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            // The queue is process-wide. A window that was already destroyed leaves
            // WINDOW_DESTROYED / CLOSE_REQUESTED behind; those must not end this window.
            if (e.type >= SDL_EVENT_WINDOW_FIRST && e.type <= SDL_EVENT_WINDOW_LAST &&
                e.window.windowID != self) {
                continue;
            }
            // CLOSE_REQUESTED is the title-bar X. DESTROYED covers the case where the HWND is
            // already gone - without it App::run spins on a 0x0 framebuffer forever and never
            // reaches trace flush. A close *request* is not a loop end any more: the app gets to
            // answer it through `<body onbeforeunload>` (EVENT.md section 8).
            if (e.type == SDL_EVENT_WINDOW_DESTROYED) {
                return false;
            }
            if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                close_requested_ = true;
                continue;
            }
            if (e.type == SDL_EVENT_WINDOW_RESIZED || e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                int pw = 0;
                int ph = 0;
                SDL_GetWindowSizeInPixels(window_, &pw, &ph);
                note_orientation_from_size(pw, ph, /*force_edge=*/true);
            }
            if (e.type == SDL_EVENT_WINDOW_FOCUS_GAINED ||
                e.type == SDL_EVENT_WINDOW_RESTORED ||
                e.type == SDL_EVENT_WINDOW_SHOWN) {
                set_visibility(HostVisibility::Visible);
            } else if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST ||
                       e.type == SDL_EVENT_WINDOW_MINIMIZED ||
                       e.type == SDL_EVENT_WINDOW_OCCLUDED ||
                       e.type == SDL_EVENT_WINDOW_HIDDEN) {
                set_visibility(HostVisibility::Hidden);
            }
            if (e.type == SDL_EVENT_WINDOW_MINIMIZED) {
                minimized_ = true;
                maximized_ = false;
            } else if (e.type == SDL_EVENT_WINDOW_MAXIMIZED) {
                maximized_ = true;
                minimized_ = false;
            } else if (e.type == SDL_EVENT_WINDOW_RESTORED) {
                minimized_ = false;
                maximized_ = false;
            } else if (e.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN) {
                fullscreen_ = true;
            } else if (e.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN) {
                fullscreen_ = false;
            }
            // Every finger is its own contact (PR-INPUT-1); the synthesized mouse SDL derives from
            // a touch is dropped below so the same contact does not fire twice.
            if (e.type == SDL_EVENT_FINGER_DOWN || e.type == SDL_EVENT_FINGER_MOTION ||
                e.type == SDL_EVENT_FINGER_UP || e.type == SDL_EVENT_FINGER_CANCELED) {
                const float fx = e.tfinger.x * static_cast<float>(win_w > 0 ? win_w : 1);
                const float fy = e.tfinger.y * static_cast<float>(win_h > 0 ? win_h : 1);
                FingerTrack* track = finger_track(e.tfinger.fingerID);
                if (e.type == SDL_EVENT_FINGER_DOWN && track == nullptr) {
                    FingerTrack fresh;
                    fresh.touch = e.tfinger.touchID;
                    fresh.finger = e.tfinger.fingerID;
                    fresh.x = fx;
                    fresh.y = fy;
                    fresh.pressure = e.tfinger.pressure;
                    fresh.down = true;
                    fresh.pressed = true;
                    fingers_.push_back(fresh);
                    track = &fingers_.back();
                }
                if (track != nullptr) {
                    track->x = fx;
                    track->y = fy;
                    track->pressure = e.tfinger.pressure;
                    if (e.type == SDL_EVENT_FINGER_UP || e.type == SDL_EVENT_FINGER_CANCELED) {
                        track->down = false;
                        track->released = true;
                        track->cancelled = e.type == SDL_EVENT_FINGER_CANCELED;
                        track->ended = true;
                    }
                }
                continue;
            }
            if (e.type == SDL_EVENT_PEN_DOWN || e.type == SDL_EVENT_PEN_UP ||
                e.type == SDL_EVENT_PEN_MOTION || e.type == SDL_EVENT_PEN_BUTTON_DOWN ||
                e.type == SDL_EVENT_PEN_BUTTON_UP || e.type == SDL_EVENT_PEN_AXIS) {
                note_pen_event(e);
                continue;
            }
            if (e.type == SDL_EVENT_GAMEPAD_ADDED) {
                if (GamepadService* pads = ExtensionRegistry::instance().gamepad()) {
                    pads->added(static_cast<std::uint32_t>(e.gdevice.which));
                }
                continue;
            }
            if (e.type == SDL_EVENT_GAMEPAD_REMOVED) {
                if (GamepadService* pads = ExtensionRegistry::instance().gamepad()) {
                    pads->removed(static_cast<std::uint32_t>(e.gdevice.which));
                }
                continue;
            }
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                if (e.button.which == SDL_TOUCH_MOUSEID) {
                    continue;
                }
                mouse_clicks = static_cast<int>(e.button.clicks);
                if (const int bit = mouse_button_bit(e.button.button); bit != 0) {
                    mouse_pressed = true;
                    mouse_changed |= bit;
                }
            }
            if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                if (e.button.which == SDL_TOUCH_MOUSEID) {
                    continue;
                }
                if (const int bit = mouse_button_bit(e.button.button); bit != 0) {
                    mouse_released = true;
                    mouse_changed |= bit;
                }
            }
            if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                frame_.wheel_x += e.wheel.x;
                frame_.wheel_y += e.wheel.y;
            }
            if (e.type == SDL_EVENT_TEXT_INPUT) {
                text_.text += e.text.text;
            }
            if (e.type == SDL_EVENT_TEXT_EDITING) {
                // SDL reports the composition in progress, and an empty text when it ends.
                const bool now = e.edit.text != nullptr && e.edit.text[0] != '\0';
                if (now && !was_composing_) {
                    text_.composition_phase = 1;
                } else if (now) {
                    text_.composition_phase = 2;
                } else if (was_composing_) {
                    text_.composition_phase = 3;
                }
                text_.composing = now;
                text_.composition = now && e.edit.text != nullptr ? e.edit.text : std::string{};
                was_composing_ = now;
            }
            if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
                text_.shift = (e.key.mod & SDL_KMOD_SHIFT) != 0;
                text_.ctrl = (e.key.mod & SDL_KMOD_CTRL) != 0;
                text_.alt = (e.key.mod & SDL_KMOD_ALT) != 0;
                text_.meta = (e.key.mod & SDL_KMOD_GUI) != 0;
                if (std::string name = dom_key_name(e.key.key, text_.shift); !name.empty()) {
                    KeyPress press;
                    press.key = std::move(name);
                    press.ctrl = text_.ctrl;
                    press.shift = text_.shift;
                    press.alt = text_.alt;
                    press.meta = text_.meta;
                    press.composing = text_.composing || was_composing_;
                    if (e.type == SDL_EVENT_KEY_UP) {
                        text_.key_up_events.push_back(std::move(press));
                    } else {
                        text_.key_events.push_back(std::move(press));
                    }
                }
                if (e.type == SDL_EVENT_KEY_UP) {
                    continue;
                }
                switch (e.key.key) {
                    case SDLK_BACKSPACE:
                        ++text_.backspace;
                        break;
                    case SDLK_DELETE:
                        ++text_.del;
                        break;
                    case SDLK_LEFT:
                        ++text_.left;
                        break;
                    case SDLK_RIGHT:
                        ++text_.right;
                        break;
                    case SDLK_HOME:
                        ++text_.home;
                        break;
                    case SDLK_END:
                        ++text_.end;
                        break;
                    case SDLK_RETURN:
                    case SDLK_KP_ENTER:
                        if (!text_.composing && !was_composing_) {
                            text_.enter = true;
                        }
                        break;
                    case SDLK_ESCAPE:
                        text_.escape = true;
                        break;
                    case SDLK_UP:
                        ++text_.up;
                        break;
                    case SDLK_DOWN:
                        ++text_.down;
                        break;
                    case SDLK_PAGEUP:
                        ++text_.page_up;
                        break;
                    case SDLK_PAGEDOWN:
                        ++text_.page_down;
                        break;
                    case SDLK_SPACE:
                        ++text_.space;
                        break;
                    case SDLK_TAB:
                        ++text_.tab;
                        break;
                    default:
                        break;
                }
            }
            if (e.type == SDL_EVENT_DROP_FILE && e.drop.data != nullptr && e.drop.data[0] != '\0') {
                dropped_paths_.emplace_back(e.drop.data);
            }
        }
        // The mouse is one pointer among many. It is always reported (even with no button held) so
        // hover keeps working while a finger or pen is active.
        {
            PointerContact mouse;
            mouse.id = 1;
            mouse.kind = PointerKind::Mouse;
            mouse.primary = true;
            float mx = 0;
            float my = 0;
            const Uint32 buttons = SDL_GetMouseState(&mx, &my);
            // SDL reports window units. Those are CSS px when pixel density equals the
            // content scale (macOS points, hidden probes). Win32 Per-Monitor V2 keeps
            // density at 1 and the display scale at 1.25, so divide here. frame.cpp
            // must not divide again.
            mouse.x = window_to_css(mx);
            mouse.y = window_to_css(my);
            mouse.buttons = ((buttons & SDL_BUTTON_LMASK) != 0 ? 1 : 0) |
                            ((buttons & SDL_BUTTON_RMASK) != 0 ? 2 : 0) |
                            ((buttons & SDL_BUTTON_MMASK) != 0 ? 4 : 0);
            mouse.down = mouse.buttons != 0;
            mouse.pressed = mouse_pressed;
            mouse.released = mouse_released;
            mouse.changed_button = mouse_changed;
            mouse.click_count = mouse_clicks > 0 ? mouse_clicks : 1;
            frame_.contacts.push_back(mouse);
        }
        bool first_touch = true;
        for (const FingerTrack& track : fingers_) {
            PointerContact contact;
            contact.id = static_cast<std::int32_t>(track.finger & 0x7fffffff);
            contact.kind = PointerKind::Touch;
            contact.primary = first_touch;
            first_touch = false;
            contact.x = window_to_css(track.x);
            contact.y = window_to_css(track.y);
            contact.pressure = track.pressure;
            contact.buttons = track.down ? 1 : 0;
            contact.down = track.down;
            contact.pressed = track.pressed;
            contact.released = track.released;
            contact.cancelled = track.cancelled;
            contact.changed_button = (track.pressed || track.released || track.cancelled) ? 1 : 0;
            contact.click_count = 1;
            frame_.contacts.push_back(contact);
        }
        fingers_.erase(std::remove_if(fingers_.begin(), fingers_.end(),
                                      [](const FingerTrack& track) { return track.ended; }),
                       fingers_.end());
        if (pen_.active) {
            PointerContact contact;
            contact.id = static_cast<std::int32_t>(pen_.pen & 0x7fffffff);
            contact.kind = PointerKind::Pen;
            contact.primary = true;
            contact.x = window_to_css(pen_.x);
            contact.y = window_to_css(pen_.y);
            contact.pressure = pen_.pressure;
            contact.buttons = pen_.buttons;
            contact.down = pen_.down;
            contact.pressed = pen_.pressed;
            contact.released = pen_.released;
            contact.changed_button = (pen_.pressed || pen_.released) ? 1 : 0;
            frame_.contacts.push_back(contact);
        }
        if (pen_.ended) {
            pen_ = PenTrack{};
        }
        // Sample held modifiers for wheel (text_ is cleared each poll, so key-down alone is not enough).
        const SDL_Keymod mods = SDL_GetModState();
        frame_.ctrl = (mods & SDL_KMOD_CTRL) != 0;
        frame_.shift = (mods & SDL_KMOD_SHIFT) != 0;
        frame_.alt = (mods & SDL_KMOD_ALT) != 0;
        frame_.meta = (mods & SDL_KMOD_GUI) != 0;
        if (pending_composition_) {
            text_.composition_phase = pending_phase_;
            text_.composing = pending_composing_;
            if (pending_composing_) {
                text_.composition = pending_text_;
            } else if (!pending_text_.empty()) {
                text_.text += pending_text_;
            }
            pending_composition_ = false;
            pending_text_.clear();
        }
        if (GamepadService* pads = ExtensionRegistry::instance().gamepad()) {
            pads->poll();
        }
        return true;
    }

    const PointerFrame& pointers() const override { return frame_; }

    const GamepadFrame& gamepads() const override {
        gamepad_snapshot_.pads.clear();
        if (GamepadService* pads = ExtensionRegistry::instance().gamepad()) {
            gamepad_snapshot_.pads = pads->snapshot();
        }
        return gamepad_snapshot_;
    }

    const TextInputFrame& text() const override { return text_; }

    bool take_close_request() override {
        const bool requested = close_requested_;
        close_requested_ = false;
        return requested;
    }

    void set_title(std::string_view title) override {
        if (window_ == nullptr) {
            return;
        }
        SDL_SetWindowTitle(window_, std::string(title).c_str());
    }

    void set_window_size(int w, int h) override {
        if (window_ == nullptr || w <= 0 || h <= 0) {
            return;
        }
        // App sizes are CSS px. The window unit grows only when the display scale
        // is not already in the pixel density.
        apply_css_window_size(w, h);
        note_orientation_from_size(w, h, /*force_edge=*/true);
    }

    bool set_icon(const WindowIcon& icon) override {
        if (window_ == nullptr || !icon.valid()) {
            return false;
        }
        SDL_Surface* surface =
            SDL_CreateSurface(icon.width, icon.height, SDL_PIXELFORMAT_RGBA32);
        if (surface == nullptr || surface->pixels == nullptr) {
            SDL_DestroySurface(surface);
            return false;
        }
        auto* dst = static_cast<std::uint8_t*>(surface->pixels);
        const int row = icon.width * 4;
        for (int y = 0; y < icon.height; ++y) {
            std::memcpy(dst + static_cast<std::size_t>(y) * static_cast<std::size_t>(surface->pitch),
                        icon.rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(row),
                        static_cast<std::size_t>(row));
        }
        const bool ok = SDL_SetWindowIcon(window_, surface);
        SDL_DestroySurface(surface);
        return ok;
    }

    bool minimize() override {
        if (window_ == nullptr || !SDL_MinimizeWindow(window_)) {
            return false;
        }
        minimized_ = true;
        maximized_ = false;
        // The MINIMIZED event also lands here. Setting it now keeps the edge if that event
        // is still queued when the next poll runs.
        set_visibility(HostVisibility::Hidden);
        return true;
    }

    bool set_maximized(bool on) override {
        if (window_ == nullptr) {
            return false;
        }
        const bool ok = on ? SDL_MaximizeWindow(window_) : SDL_RestoreWindow(window_);
        if (!ok) {
            return false;
        }
        maximized_ = on;
        minimized_ = false;
        return true;
    }

    bool set_fullscreen(bool on) override {
        if (window_ == nullptr || !SDL_SetWindowFullscreen(window_, on)) {
            return false;
        }
        fullscreen_ = on;
        return true;
    }

    bool set_topmost(bool on) override {
        if (window_ == nullptr || !SDL_SetWindowAlwaysOnTop(window_, on)) {
            return false;
        }
        topmost_ = on;
        return true;
    }

    bool minimized() const override { return minimized_; }
    bool maximized() const override { return maximized_; }
    bool fullscreen() const override { return fullscreen_; }
    bool topmost() const override { return topmost_; }

    DisplayIdentity display() const override {
        DisplayIdentity out;
        if (window_ == nullptr) {
            return out;
        }
        const SDL_DisplayID id = SDL_GetDisplayForWindow(window_);
        if (id == 0) {
            return out;
        }
        out.known = true;
        out.id = std::to_string(id);
        out.scale = content_scale();
        SDL_Rect bounds{};
        if (SDL_GetDisplayBounds(id, &bounds)) {
            out.width_px = bounds.w;
            out.height_px = bounds.h;
        }
        int count = 0;
        SDL_DisplayID* list = SDL_GetDisplays(&count);
        if (list != nullptr) {
            for (int i = 0; i < count; ++i) {
                if (list[i] == id) {
                    out.index = i;
                    break;
                }
            }
            SDL_free(list);
        }
        return out;
    }

    HostVisibility visibility() const override { return visibility_; }

    bool take_visibility_change() override {
        if (!visibility_changed_) {
            return false;
        }
        visibility_changed_ = false;
        return true;
    }

    HostOrientation orientation() const override { return orientation_; }

    bool take_orientation_change() override {
        if (!orientation_changed_) {
            return false;
        }
        orientation_changed_ = false;
        return true;
    }

    SDL_Cursor* palm_cursor(bool closed) {
        SDL_Cursor*& slot = closed ? hand_closed_ : hand_open_;
        if (slot != nullptr) {
            return slot;
        }
        const HandCursorImage image = hand_cursor_image(closed);
        SDL_Surface* surface =
            SDL_CreateSurface(image.width, image.height, SDL_PIXELFORMAT_RGBA32);
        if (surface == nullptr) {
            return nullptr;
        }
        const SDL_PixelFormatDetails* details = SDL_GetPixelFormatDetails(surface->format);
        if (details == nullptr) {
            SDL_DestroySurface(surface);
            return nullptr;
        }
        for (int y = 0; y < image.height; ++y) {
            for (int x = 0; x < image.width; ++x) {
                const std::uint8_t* p =
                    image.rgba.data() + static_cast<std::size_t>(y * image.width + x) * 4;
                const SDL_Rect pixel{x, y, 1, 1};
                SDL_FillSurfaceRect(surface, &pixel,
                                    SDL_MapRGBA(details, nullptr, p[0], p[1], p[2], p[3]));
            }
        }
        slot = SDL_CreateColorCursor(surface, image.hot_x, image.hot_y);
        SDL_DestroySurface(surface);
        return slot;
    }

    void set_cursor(Cursor cursor) override {
        if (cursor == cursor_kind_ && cursor_handle_ != nullptr) {
            return;
        }
        SDL_Cursor* next = nullptr;
        if (cursor == Cursor::Grab || cursor == Cursor::Grabbing) {
            next = palm_cursor(cursor == Cursor::Grabbing);
        } else {
            SDL_SystemCursor id = SDL_SYSTEM_CURSOR_DEFAULT;
            switch (cursor) {
                case Cursor::Pointer:
                    id = SDL_SYSTEM_CURSOR_POINTER;
                    break;
                case Cursor::Text:
                    id = SDL_SYSTEM_CURSOR_TEXT;
                    break;
                case Cursor::Wait:
                    id = SDL_SYSTEM_CURSOR_WAIT;
                    break;
                case Cursor::NotAllowed:
                    id = SDL_SYSTEM_CURSOR_NOT_ALLOWED;
                    break;
                case Cursor::EwResize:
                    id = SDL_SYSTEM_CURSOR_EW_RESIZE;
                    break;
                case Cursor::NsResize:
                    id = SDL_SYSTEM_CURSOR_NS_RESIZE;
                    break;
                case Cursor::NwseResize:
                    id = SDL_SYSTEM_CURSOR_NWSE_RESIZE;
                    break;
                case Cursor::NeswResize:
                    id = SDL_SYSTEM_CURSOR_NESW_RESIZE;
                    break;
                case Cursor::Move:
                    id = SDL_SYSTEM_CURSOR_MOVE;
                    break;
                case Cursor::Crosshair:
                    id = SDL_SYSTEM_CURSOR_CROSSHAIR;
                    break;
                case Cursor::Grab:
                case Cursor::Grabbing:
                case Cursor::Default:
                    id = SDL_SYSTEM_CURSOR_DEFAULT;
                    break;
            }
            next = SDL_CreateSystemCursor(id);
        }
        if (next == nullptr) {
            return;
        }
        SDL_SetCursor(next);
        if (cursor_handle_ != nullptr && cursor_handle_ != next && cursor_handle_ != hand_open_ &&
            cursor_handle_ != hand_closed_) {
            SDL_DestroyCursor(cursor_handle_);
        }
        cursor_handle_ = next;
        cursor_kind_ = cursor;
    }

    void set_ime(bool enabled) override {
        if (window_ == nullptr) {
            return;
        }
        if (enabled) {
            SDL_StartTextInput(window_);
        } else {
            SDL_StopTextInput(window_);
        }
    }

    void set_ime_rect(float x, float y, float w, float h) override {
        if (window_ == nullptr) {
            return;
        }
        SDL_Rect area;
        // The caret rect arrives in CSS px. SDL wants window units.
        area.x = static_cast<int>(std::lround(css_to_window(x)));
        area.y = static_cast<int>(std::lround(css_to_window(y)));
        const int iw = static_cast<int>(std::lround(css_to_window(w)));
        const int ih = static_cast<int>(std::lround(css_to_window(h)));
        area.w = iw > 1 ? iw : 1;
        area.h = ih > 1 ? ih : 1;
        // cursor offset is relative to area.x; caret is the left edge of the 1px rect.
        SDL_SetTextInputArea(window_, &area, 0);
#ifdef _WIN32
        ime_set_caret(native_handle().get(), static_cast<float>(area.x), static_cast<float>(area.y),
                      static_cast<float>(area.w), static_cast<float>(area.h));
#endif
    }

    void inject_composition_for_test(int phase, std::string text, bool composing) override {
        pending_composition_ = true;
        pending_phase_ = phase;
        pending_text_ = std::move(text);
        pending_composing_ = composing;
    }

    void framebuffer_size(int& width, int& height) const override {
        width = 0;
        height = 0;
        if (window_ != nullptr) {
            SDL_GetWindowSizeInPixels(window_, &width, &height);
        }
    }

    DisplayInsets safe_area_insets() const override {
        DisplayInsets insets;
        if (window_ == nullptr) {
            return insets;
        }
        // SDL stores the safe rect in the same space as SDL_GetWindowSize.
        // An empty safe rect means "the whole window", which is every desktop.
        SDL_Rect area{};
        if (!SDL_GetWindowSafeArea(window_, &area)) {
            return insets;
        }
        int win_w = 0;
        int win_h = 0;
        SDL_GetWindowSize(window_, &win_w, &win_h);
        if (win_w <= 0 || win_h <= 0) {
            return insets;
        }
        const int right = win_w - (area.x + area.w);
        const int bottom = win_h - (area.y + area.h);
        // Safe rect is in window units, same as SDL_GetWindowSize. CSS insets use
        // the same ratio as the pointer.
        insets.left = area.x > 0 ? window_to_css(static_cast<float>(area.x)) : 0.f;
        insets.top = area.y > 0 ? window_to_css(static_cast<float>(area.y)) : 0.f;
        insets.right = right > 0 ? window_to_css(static_cast<float>(right)) : 0.f;
        insets.bottom = bottom > 0 ? window_to_css(static_cast<float>(bottom)) : 0.f;
        return insets;
    }

    DisplayInsets keyboard_insets() const override {
        DisplayInsets insets;
#if (defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__ANDROID__)
        const float bottom = device_keyboard_bottom();
        insets.bottom = bottom > 0.f ? bottom : 0.f;
#endif
        return insets;
    }

    float content_scale() const override {
        if (window_ == nullptr) {
            return 1.f;
        }
        const float density = pixel_density();
        // A hidden window has no HIGH_PIXEL_DENSITY flag. Returning the display
        // scale here would shrink every probe viewport on a 125% desktop.
        if (!high_density_) {
            return density;
        }
        // SDL_GetWindowDisplayScale is pixel density times the display content
        // scale on Win32 and Cocoa (only Wayland replaces it). That product is
        // the CSS-to-framebuffer factor: 1.25 when Windows density stays 1, and
        // the backing scale when macOS density already matches.
        const float display = SDL_GetWindowDisplayScale(window_);
        if (!(display > 0.f)) {
            return density;
        }
        return display > density ? display : density;
    }

    const char* name() const override { return "sdl3"; }

    NativeWindowId native_handle() const override {
        if (window_ == nullptr) {
            return {};
        }
        const SDL_PropertiesID props = SDL_GetWindowProperties(window_);
#ifdef _WIN32
        return NativeWindowId(SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
#elif defined(__ANDROID__)
        return NativeWindowId(SDL_GetPointerProperty(props, SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr));
#elif defined(__APPLE__)
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
        return NativeWindowId(SDL_GetPointerProperty(props, SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr));
#else
        return NativeWindowId(SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr));
#endif
#elif defined(__linux__)
        // X11 window id stored in the pointer. Wayland has no integer owner.
        const Sint64 xid = SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
        if (xid <= 0) {
            return {};
        }
        return NativeWindowId(reinterpret_cast<void*>(static_cast<uintptr_t>(xid)));
#else
        (void)props;
        return {};
#endif
    }

    void discard_pending_input() override {
        SDL_PumpEvents();
        SDL_FlushEvent(SDL_EVENT_MOUSE_MOTION);
        SDL_FlushEvent(SDL_EVENT_MOUSE_BUTTON_DOWN);
        SDL_FlushEvent(SDL_EVENT_MOUSE_BUTTON_UP);
        SDL_FlushEvent(SDL_EVENT_MOUSE_WHEEL);
        SDL_FlushEvent(SDL_EVENT_KEY_DOWN);
        SDL_FlushEvent(SDL_EVENT_KEY_UP);
        SDL_FlushEvent(SDL_EVENT_TEXT_INPUT);
        SDL_FlushEvent(SDL_EVENT_TEXT_EDITING);
        SDL_FlushEvent(SDL_EVENT_DROP_FILE);
        SDL_FlushEvent(SDL_EVENT_DROP_TEXT);
        SDL_FlushEvent(SDL_EVENT_DROP_BEGIN);
        SDL_FlushEvent(SDL_EVENT_DROP_COMPLETE);
        SDL_FlushEvent(SDL_EVENT_DROP_POSITION);
        SDL_FlushEvent(SDL_EVENT_FINGER_DOWN);
        SDL_FlushEvent(SDL_EVENT_FINGER_UP);
        SDL_FlushEvent(SDL_EVENT_FINGER_MOTION);
        SDL_FlushEvent(SDL_EVENT_FINGER_CANCELED);
        SDL_FlushEvent(SDL_EVENT_PEN_DOWN);
        SDL_FlushEvent(SDL_EVENT_PEN_UP);
        SDL_FlushEvent(SDL_EVENT_PEN_MOTION);
        SDL_FlushEvent(SDL_EVENT_PEN_BUTTON_DOWN);
        SDL_FlushEvent(SDL_EVENT_PEN_BUTTON_UP);
        SDL_FlushEvent(SDL_EVENT_PEN_AXIS);
        frame_ = {};
        fingers_.clear();
        pen_ = PenTrack{};
        text_ = {};
        dropped_paths_.clear();
    }

    std::vector<std::string> take_dropped_paths() override {
        std::vector<std::string> out;
        out.swap(dropped_paths_);
        return out;
    }

    void inject_opened_paths(std::vector<std::string> paths) override {
        for (std::string& path : paths) {
            if (!path.empty()) {
                dropped_paths_.push_back(std::move(path));
            }
        }
    }

    ClipboardImage clipboard_image() override {
        if (ClipboardService* board = ExtensionRegistry::instance().clipboard()) {
            return board->image();
        }
        return {};
    }

    bool set_clipboard_image(const ClipboardImage& image) override {
        if (ClipboardService* board = ExtensionRegistry::instance().clipboard()) {
            return board->set_image(image);
        }
        return false;
    }

    ClipboardText clipboard_text() override {
        if (ClipboardService* board = ExtensionRegistry::instance().clipboard()) {
            return board->text();
        }
        return {};
    }

    bool set_clipboard_text(std::string_view text) override {
        if (ClipboardService* board = ExtensionRegistry::instance().clipboard()) {
            return board->set_text(text);
        }
        return false;
    }

    void* gl_proc(const char* name) const override { return (void*)SDL_GL_GetProcAddress(name); }

    void gl_make_current() override {
        if (window_ && gl_) {
            SDL_GL_MakeCurrent(window_, gl_);
        }
    }

    void gl_swap() override {
        if (window_) {
            SDL_GL_SwapWindow(window_);
        }
    }

    bool vulkan_instance_extensions(uint32_t* count, const char* const** names) const override {
#ifdef ZUI_HAS_VULKAN
        Uint32 n = 0;
        const char* const* ext = SDL_Vulkan_GetInstanceExtensions(&n);
        if (ext == nullptr) {
            return false;
        }
        *count = n;
        *names = ext;
        return true;
#else
        (void)count;
        (void)names;
        return false;
#endif
    }

    bool create_vulkan_surface(void* instance, void** surface) override {
#ifdef ZUI_HAS_VULKAN
        if (window_ == nullptr || instance == nullptr) {
            return false;
        }
        VkSurfaceKHR surf = VK_NULL_HANDLE;
        if (!SDL_Vulkan_CreateSurface(window_, static_cast<VkInstance>(instance), nullptr, &surf)) {
            ZUI_ERROR("SDL_Vulkan_CreateSurface: {}", SDL_GetError());
            return false;
        }
        *surface = reinterpret_cast<void*>(surf);
        return true;
#else
        (void)instance;
        (void)surface;
        return false;
#endif
    }

    /// Test seam: enqueue the same finger event `poll` already samples. Not an application API.
    bool push_finger(bool down, float nx, float ny, SDL_FingerID finger, float pressure) {
        if (window_ == nullptr) {
            return false;
        }
        SDL_SetEventEnabled(SDL_EVENT_FINGER_DOWN, true);
        SDL_SetEventEnabled(SDL_EVENT_FINGER_UP, true);
        SDL_Event event{};
        event.type = down ? SDL_EVENT_FINGER_DOWN : SDL_EVENT_FINGER_UP;
        event.tfinger.touchID = static_cast<SDL_TouchID>(1);
        event.tfinger.fingerID = finger;
        event.tfinger.x = nx;
        event.tfinger.y = ny;
        event.tfinger.pressure = pressure;
        event.tfinger.windowID = SDL_GetWindowID(window_);
        return SDL_PushEvent(&event);
    }

private:
    void set_visibility(HostVisibility next) {
        if (visibility_ == next) {
            return;
        }
        visibility_ = next;
        visibility_changed_ = true;
    }

    float pixel_density() const {
        if (window_ == nullptr) {
            return 1.f;
        }
        const float density = SDL_GetWindowPixelDensity(window_);
        return density > 0.f ? density : 1.f;
    }

    /// Window units to CSS px. Identity when density and content scale match.
    float window_to_css(float v) const {
        const float scale = content_scale();
        const float density = pixel_density();
        if (!(scale > 0.f) || !(density > 0.f)) {
            return v;
        }
        return v * (density / scale);
    }

    /// CSS px to window units. Inverse of window_to_css.
    float css_to_window(float v) const {
        const float scale = content_scale();
        const float density = pixel_density();
        if (!(scale > 0.f) || !(density > 0.f)) {
            return v;
        }
        return v * (scale / density);
    }

    /// Resize so the framebuffer is CSS * content_scale. `ratio` is 1 when the
    /// window unit is already a CSS px.
    void apply_css_window_size(int css_w, int css_h) {
        if (window_ == nullptr || css_w <= 0 || css_h <= 0) {
            return;
        }
        float ratio = 1.f;
        if (high_density_) {
            const float density = pixel_density();
            const float scale = SDL_GetWindowDisplayScale(window_);
            if (density > 0.f && scale > 0.f) {
                ratio = scale / density;
            }
        }
        const int win_w = std::max(1, static_cast<int>(std::lround(css_w * ratio)));
        const int win_h = std::max(1, static_cast<int>(std::lround(css_h * ratio)));
        int cur_w = 0;
        int cur_h = 0;
        SDL_GetWindowSize(window_, &cur_w, &cur_h);
        if (cur_w != win_w || cur_h != win_h) {
            SDL_SetWindowSize(window_, win_w, win_h);
        }
    }

    void note_orientation_from_size(int w, int h, bool force_edge) {
        HostOrientation next = HostOrientation::Unknown;
        if (w > 0 && h > 0) {
            next = (w >= h) ? HostOrientation::Landscape : HostOrientation::Portrait;
        }
        if (next == orientation_) {
            return;
        }
        orientation_ = next;
        if (force_edge) {
            orientation_changed_ = true;
        }
    }

    FingerTrack* finger_track(SDL_FingerID finger) {
        for (FingerTrack& track : fingers_) {
            if (track.finger == finger) {
                return &track;
            }
        }
        return nullptr;
    }

    /// Fold one SDL pen event into the single pen contact. SDL identifies pens with an ephemeral
    /// id, so the seam keeps the most recent one and reports it as the primary pen (PR-INPUT-1).
    void note_pen_event(const SDL_Event& e) {
        switch (e.type) {
            case SDL_EVENT_PEN_DOWN:
                pen_.active = true;
                pen_.pen = e.ptouch.which;
                pen_.x = e.ptouch.x;
                pen_.y = e.ptouch.y;
                pen_.down = true;
                pen_.pressed = true;
                pen_.buttons |= 1;
                break;
            case SDL_EVENT_PEN_UP:
                pen_.active = true;
                pen_.pen = e.ptouch.which;
                pen_.x = e.ptouch.x;
                pen_.y = e.ptouch.y;
                pen_.down = false;
                pen_.released = true;
                pen_.buttons &= ~1;
                pen_.ended = true;
                break;
            case SDL_EVENT_PEN_MOTION:
                pen_.active = true;
                pen_.pen = e.pmotion.which;
                pen_.x = e.pmotion.x;
                pen_.y = e.pmotion.y;
                break;
            case SDL_EVENT_PEN_BUTTON_DOWN:
                pen_.active = true;
                pen_.pen = e.pbutton.which;
                pen_.x = e.pbutton.x;
                pen_.y = e.pbutton.y;
                pen_.buttons |= 2;
                break;
            case SDL_EVENT_PEN_BUTTON_UP:
                pen_.active = true;
                pen_.pen = e.pbutton.which;
                pen_.x = e.pbutton.x;
                pen_.y = e.pbutton.y;
                pen_.buttons &= ~2;
                break;
            case SDL_EVENT_PEN_AXIS:
                pen_.active = true;
                pen_.pen = e.paxis.which;
                pen_.x = e.paxis.x;
                pen_.y = e.paxis.y;
                if (e.paxis.axis == SDL_PEN_AXIS_PRESSURE) {
                    pen_.pressure = std::min(1.f, std::max(0.f, e.paxis.value));
                }
                break;
            default:
                break;
        }
    }

    SDL_Window* window_ = nullptr;
    /// True when the window was created with SDL_WINDOW_HIGH_PIXEL_DENSITY.
    bool high_density_ = false;
    SDL_GLContext gl_ = nullptr;
    GfxApi api_ = GfxApi::Vulkan;
    /// Set by SDL_EVENT_QUIT / WINDOW_CLOSE_REQUESTED, consumed by take_close_request().
    bool close_requested_ = false;
    /// Every pointer this poll saw: mouse (always), fingers, and the current pen.
    PointerFrame frame_;
    std::vector<FingerTrack> fingers_;
    PenTrack pen_;
    TextInputFrame text_;
    bool was_composing_ = false;
    bool pending_composition_ = false;
    int pending_phase_ = 0;
    bool pending_composing_ = false;
    std::string pending_text_;
    std::vector<std::string> dropped_paths_;
    mutable GamepadFrame gamepad_snapshot_{};
    SDL_Cursor* cursor_handle_ = nullptr;
    SDL_Cursor* hand_open_ = nullptr;
    SDL_Cursor* hand_closed_ = nullptr;
    Cursor cursor_kind_ = Cursor::Default;
    HostVisibility visibility_ = HostVisibility::Visible;
    bool visibility_changed_ = false;
    HostOrientation orientation_ = HostOrientation::Unknown;
    bool orientation_changed_ = false;
    bool minimized_ = false;
    bool maximized_ = false;
    bool fullscreen_ = false;
    bool topmost_ = false;
};

std::unique_ptr<IWindow> create_sdl3_window() {
    return std::make_unique<Sdl3Window>();
}

bool push_finger_for_test(IWindow& window, bool down, float nx, float ny, std::int64_t finger,
                          float pressure) {
    auto* sdl = dynamic_cast<Sdl3Window*>(&window);
    if (sdl == nullptr) {
        return false;
    }
    return sdl->push_finger(down, nx, ny, static_cast<SDL_FingerID>(finger), pressure);
}

}  // namespace backends
}  // namespace zui

#else

#include "backends/sdl_finger_test.h"

namespace zui {
namespace backends {

bool push_finger_for_test(IWindow&, bool, float, float, std::int64_t, float) {
    return false;
}

}  // namespace backends
}  // namespace zui

#endif  // ZUI_WITH_SDL
