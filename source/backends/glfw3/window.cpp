#ifdef ZUI_WITH_GLFW
#include "backends/hand_cursor.h"
#include "backends/window.h"
#include "backends/ime_composer.h"
#include "ext/slots.h"

#ifdef ZUI_HAS_VULKAN
#define GLFW_INCLUDE_VULKAN
#else
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

#include "log/log.h"
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <imm.h>
#pragma comment(lib, "Imm32.lib")
#elif defined(__APPLE__)
extern "C" void* glfwGetCocoaWindow(GLFWwindow* window);
#elif defined(__linux__)
extern "C" unsigned long glfwGetX11Window(GLFWwindow* window);
#endif

#include <cmath>
#include <cstdint>
#include <cctype>
#include <memory>
#include <string>
#include <vector>

namespace zui {
namespace backends {

namespace {

/// `KeyboardEvent.key` spelling of one GLFW key; empty when this backend has no name for it,
/// in which case the key is not offered as `onkeydown`. Modifier-resolved characters
/// (Shift+1 -> "!") come through the text channel, not from here.
std::string dom_key_name(int key, bool shift) {
    switch (key) {
        case GLFW_KEY_ENTER:
        case GLFW_KEY_KP_ENTER:
            return "Enter";
        case GLFW_KEY_ESCAPE:
            return "Escape";
        case GLFW_KEY_BACKSPACE:
            return "Backspace";
        case GLFW_KEY_DELETE:
            return "Delete";
        case GLFW_KEY_TAB:
            return "Tab";
        case GLFW_KEY_LEFT:
            return "ArrowLeft";
        case GLFW_KEY_RIGHT:
            return "ArrowRight";
        case GLFW_KEY_UP:
            return "ArrowUp";
        case GLFW_KEY_DOWN:
            return "ArrowDown";
        case GLFW_KEY_HOME:
            return "Home";
        case GLFW_KEY_END:
            return "End";
        case GLFW_KEY_PAGE_UP:
            return "PageUp";
        case GLFW_KEY_PAGE_DOWN:
            return "PageDown";
        case GLFW_KEY_SPACE:
            return " ";
        default:
            break;
    }
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
        const char lower = static_cast<char>('a' + (key - GLFW_KEY_A));
        return std::string(1, shift ? static_cast<char>(std::toupper(lower)) : lower);
    }
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) {
        return std::string(1, static_cast<char>('0' + (key - GLFW_KEY_0)));
    }
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12) {
        return "F" + std::to_string(1 + (key - GLFW_KEY_F1));
    }
    return {};
}

}  // namespace

class Glfw3Window final : public IWindow {
public:
    ~Glfw3Window() override { destroy(); }

    bool create(const WindowDesc& desc) override {
        destroy();
        if (!glfwInit()) {
            ZUI_ERROR("glfwInit failed");
            return false;
        }
        api_ = desc.api;
        if (desc.api == GfxApi::Vulkan) {
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        } else if (desc.api == GfxApi::OpenGLES) {
            glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
        } else {
            glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
            // 4.3: vector glyphs use a fragment-shader storage buffer. GLES stays at 3.0
            // and does not draw text.
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        }
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        glfwWindowHint(GLFW_VISIBLE, desc.hidden ? GLFW_FALSE : GLFW_TRUE);
        window_ = glfwCreateWindow(desc.width, desc.height, desc.title.c_str(), nullptr, nullptr);
        glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
        if (window_ == nullptr) {
            ZUI_ERROR("glfwCreateWindow failed");
            return false;
        }
        if (desc.api != GfxApi::Vulkan) {
            glfwMakeContextCurrent(window_);
            glfwSwapInterval(desc.hidden ? 0 : 1);
        }
        glfwSetWindowUserPointer(window_, this);
        glfwSetWindowFocusCallback(window_, [](GLFWwindow* w, int focused) {
            auto* self = static_cast<Glfw3Window*>(glfwGetWindowUserPointer(w));
            if (self != nullptr) {
                self->set_visibility(focused ? HostVisibility::Visible : HostVisibility::Hidden);
            }
        });
        glfwSetWindowIconifyCallback(window_, [](GLFWwindow* w, int iconified) {
            auto* self = static_cast<Glfw3Window*>(glfwGetWindowUserPointer(w));
            if (self != nullptr) {
                self->minimized_ = iconified != 0;
                if (iconified != 0) {
                    self->maximized_ = false;
                }
                self->set_visibility(iconified ? HostVisibility::Hidden : HostVisibility::Visible);
            }
        });
        glfwSetWindowMaximizeCallback(window_, [](GLFWwindow* w, int maximized) {
            auto* self = static_cast<Glfw3Window*>(glfwGetWindowUserPointer(w));
            if (self != nullptr) {
                self->maximized_ = maximized != 0;
                if (maximized != 0) {
                    self->minimized_ = false;
                }
            }
        });
        glfwSetFramebufferSizeCallback(window_, [](GLFWwindow* w, int width, int height) {
            auto* self = static_cast<Glfw3Window*>(glfwGetWindowUserPointer(w));
            if (self != nullptr) {
                self->note_orientation_from_size(width, height, /*force_edge=*/true);
            }
        });
        note_orientation_from_size(desc.width, desc.height, /*force_edge=*/false);
        glfwSetScrollCallback(window_, [](GLFWwindow* w, double x, double y) {
            auto* self = static_cast<Glfw3Window*>(glfwGetWindowUserPointer(w));
            if (self != nullptr) {
                self->frame_.wheel_x += static_cast<float>(x);
                self->frame_.wheel_y += static_cast<float>(y);
            }
        });
        glfwSetCharCallback(window_, [](GLFWwindow* w, unsigned int cp) {
            auto* self = static_cast<Glfw3Window*>(glfwGetWindowUserPointer(w));
            if (self == nullptr) {
                return;
            }
            char buf[5] = {};
            int n = 0;
            if (cp < 0x80) {
                buf[n++] = static_cast<char>(cp);
            } else if (cp < 0x800) {
                buf[n++] = static_cast<char>(0xc0 | (cp >> 6));
                buf[n++] = static_cast<char>(0x80 | (cp & 0x3f));
            } else if (cp < 0x10000) {
                buf[n++] = static_cast<char>(0xe0 | (cp >> 12));
                buf[n++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
                buf[n++] = static_cast<char>(0x80 | (cp & 0x3f));
            } else {
                buf[n++] = static_cast<char>(0xf0 | (cp >> 18));
                buf[n++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
                buf[n++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
                buf[n++] = static_cast<char>(0x80 | (cp & 0x3f));
            }
            self->text_.text.append(buf, static_cast<size_t>(n));
        });
        glfwSetKeyCallback(window_, [](GLFWwindow* w, int key, int /*sc*/, int action, int mods) {
            auto* self = static_cast<Glfw3Window*>(glfwGetWindowUserPointer(w));
            if (self == nullptr) {
                return;
            }
            if (action != GLFW_PRESS && action != GLFW_REPEAT && action != GLFW_RELEASE) {
                return;
            }
            self->text_.shift = (mods & GLFW_MOD_SHIFT) != 0;
            self->text_.ctrl = (mods & GLFW_MOD_CONTROL) != 0;
            self->text_.alt = (mods & GLFW_MOD_ALT) != 0;
            self->text_.meta = (mods & GLFW_MOD_SUPER) != 0;
            if (std::string name = dom_key_name(key, self->text_.shift); !name.empty()) {
                KeyPress press;
                press.key = std::move(name);
                press.ctrl = self->text_.ctrl;
                press.shift = self->text_.shift;
                press.alt = self->text_.alt;
                press.meta = self->text_.meta;
                const bool is_enter = key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER;
                bool swallow_enter = false;
                if (action != GLFW_RELEASE && is_enter && self->ime_.composing()) {
                    // poll_hwnd observes this Enter with the OS string still open.
                    self->ime_.note_enter();
                    press.composing = true;
                    swallow_enter = true;
                } else {
                    press.composing = self->ime_.composing();
                }
                if (action == GLFW_RELEASE) {
                    self->text_.key_up_events.push_back(std::move(press));
                    return;
                }
                self->text_.key_events.push_back(press);
                if (swallow_enter) {
                    return;
                }
            } else if (action == GLFW_RELEASE) {
                return;
            }
            switch (key) {
                case GLFW_KEY_BACKSPACE:
                    ++self->text_.backspace;
                    break;
                case GLFW_KEY_DELETE:
                    ++self->text_.del;
                    break;
                case GLFW_KEY_LEFT:
                    ++self->text_.left;
                    break;
                case GLFW_KEY_RIGHT:
                    ++self->text_.right;
                    break;
                case GLFW_KEY_HOME:
                    ++self->text_.home;
                    break;
                case GLFW_KEY_END:
                    ++self->text_.end;
                    break;
                case GLFW_KEY_ENTER:
                case GLFW_KEY_KP_ENTER:
                    self->text_.enter = true;
                    break;
                case GLFW_KEY_ESCAPE:
                    self->text_.escape = true;
                    break;
                case GLFW_KEY_UP:
                    ++self->text_.up;
                    break;
                case GLFW_KEY_DOWN:
                    ++self->text_.down;
                    break;
                case GLFW_KEY_PAGE_UP:
                    ++self->text_.page_up;
                    break;
                case GLFW_KEY_PAGE_DOWN:
                    ++self->text_.page_down;
                    break;
                case GLFW_KEY_SPACE:
                    ++self->text_.space;
                    break;
                case GLFW_KEY_TAB:
                    ++self->text_.tab;
                    break;
                default:
                    break;
            }
        });
        glfwSetDropCallback(window_, [](GLFWwindow* w, int count, const char** paths) {
            auto* self = static_cast<Glfw3Window*>(glfwGetWindowUserPointer(w));
            if (self == nullptr || paths == nullptr || count <= 0) {
                return;
            }
            for (int i = 0; i < count; ++i) {
                if (paths[i] != nullptr && paths[i][0] != '\0') {
                    self->dropped_paths_.emplace_back(paths[i]);
                }
            }
        });
        return true;
    }

    void destroy() override {
        if (cursor_handle_ != nullptr && cursor_handle_ != hand_open_ &&
            cursor_handle_ != hand_closed_) {
            glfwDestroyCursor(cursor_handle_);
        }
        cursor_handle_ = nullptr;
        if (hand_open_ != nullptr) {
            glfwDestroyCursor(hand_open_);
            hand_open_ = nullptr;
        }
        if (hand_closed_ != nullptr) {
            glfwDestroyCursor(hand_closed_);
            hand_closed_ = nullptr;
        }
        if (window_ != nullptr) {
            glfwDestroyWindow(window_);
            window_ = nullptr;
        }
    }

    bool poll() override {
        if (window_ == nullptr) {
            return false;
        }
        frame_ = {};
        text_ = {};
        glfwPollEvents();
        double mx = 0, my = 0;
        glfwGetCursorPos(window_, &mx, &my);
        // GLFW cursor is window/logical CSS px; layout and hit-test share that space.
        const float x = static_cast<float>(mx);
        const float y = static_cast<float>(my);
        bool pressed = false;
        bool released = false;
        int changed = 0;
        int click_count = 1;
        auto edge = [&](int glfw_button, bool& down, int bit, bool track_click) {
            const int is_down = glfwGetMouseButton(window_, glfw_button) == GLFW_PRESS;
            if (is_down && !down) {
                pressed = true;
                changed |= bit;
                if (track_click) {
                    const double now = glfwGetTime();
                    const float dx = x - last_click_x_;
                    const float dy = y - last_click_y_;
                    constexpr double kDoubleClickSec = 0.4;
                    constexpr float kDoubleClickSlop = 4.f;
                    if (now - last_click_time_ <= kDoubleClickSec &&
                        dx * dx + dy * dy <= kDoubleClickSlop * kDoubleClickSlop) {
                        ++click_streak_;
                    } else {
                        click_streak_ = 1;
                    }
                    last_click_time_ = now;
                    last_click_x_ = x;
                    last_click_y_ = y;
                    click_count = click_streak_ > 0 ? click_streak_ : 1;
                }
            }
            if (!is_down && down) {
                released = true;
                changed |= bit;
            }
            down = is_down != 0;
        };
        edge(GLFW_MOUSE_BUTTON_LEFT, mouse_left_down_, 1, true);
        edge(GLFW_MOUSE_BUTTON_RIGHT, mouse_right_down_, 2, false);
        edge(GLFW_MOUSE_BUTTON_MIDDLE, mouse_middle_down_, 4, false);
        PointerContact mouse;
        mouse.id = 1;
        mouse.kind = PointerKind::Mouse;
        mouse.primary = true;
        mouse.x = x;
        mouse.y = y;
        mouse.buttons =
            (mouse_left_down_ ? 1 : 0) | (mouse_right_down_ ? 2 : 0) | (mouse_middle_down_ ? 4 : 0);
        mouse.down = mouse.buttons != 0;
        mouse.pressed = pressed;
        mouse.released = released;
        mouse.changed_button = changed;
        mouse.click_count = click_count;
        frame_.contacts.push_back(mouse);
        // Sample held modifiers for wheel (text_ is cleared each poll, so key-down alone is not enough).
        frame_.ctrl = glfwGetKey(window_, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                      glfwGetKey(window_, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
        frame_.shift = glfwGetKey(window_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                       glfwGetKey(window_, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
        frame_.alt = glfwGetKey(window_, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
                     glfwGetKey(window_, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
        frame_.meta = glfwGetKey(window_, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS ||
                      glfwGetKey(window_, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS;
#ifdef _WIN32
        ime_.poll_hwnd(glfwGetWin32Window(window_));
#endif
        ime_.apply(text_);
        return true;
    }

    const PointerFrame& pointers() const override { return frame_; }

    const TextInputFrame& text() const override { return text_; }

    bool take_close_request() override {
        if (window_ == nullptr || !glfwWindowShouldClose(window_)) {
            return false;
        }
        // One request is reported once. The app answers it through `<body onbeforeunload>`
        // (EVENT.md section 8); the flag is cleared here so a prevented close does not repeat.
        glfwSetWindowShouldClose(window_, GLFW_FALSE);
        return true;
    }

    void set_title(std::string_view title) override {
        if (window_ == nullptr) {
            return;
        }
        glfwSetWindowTitle(window_, std::string(title).c_str());
    }

    void set_window_size(int w, int h) override {
        if (window_ == nullptr || w <= 0 || h <= 0) {
            return;
        }
        glfwSetWindowSize(window_, w, h);
        note_orientation_from_size(w, h, /*force_edge=*/true);
    }

    bool set_icon(const WindowIcon& icon) override {
        if (window_ == nullptr || !icon.valid()) {
            return false;
        }
        GLFWimage image;
        image.width = icon.width;
        image.height = icon.height;
        image.pixels = const_cast<unsigned char*>(icon.rgba.data());
        glfwSetWindowIcon(window_, 1, &image);
        return true;
    }

    bool minimize() override {
        if (window_ == nullptr) {
            return false;
        }
        glfwIconifyWindow(window_);
        minimized_ = true;
        maximized_ = false;
        return true;
    }

    bool set_maximized(bool on) override {
        if (window_ == nullptr) {
            return false;
        }
        if (on) {
            glfwMaximizeWindow(window_);
            maximized_ = true;
        } else {
            glfwRestoreWindow(window_);
            maximized_ = false;
        }
        minimized_ = false;
        return true;
    }

    bool set_fullscreen(bool on) override {
        if (window_ == nullptr || on == fullscreen_) {
            return window_ != nullptr;
        }
        if (on) {
            glfwGetWindowPos(window_, &restore_x_, &restore_y_);
            glfwGetWindowSize(window_, &restore_w_, &restore_h_);
            GLFWmonitor* monitor = glfwGetPrimaryMonitor();
            const GLFWvidmode* mode = monitor != nullptr ? glfwGetVideoMode(monitor) : nullptr;
            if (monitor == nullptr || mode == nullptr) {
                return false;
            }
            glfwSetWindowMonitor(window_, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        } else {
            glfwSetWindowMonitor(window_, nullptr, restore_x_, restore_y_, restore_w_, restore_h_,
                                 GLFW_DONT_CARE);
        }
        fullscreen_ = on;
        return true;
    }

    bool set_topmost(bool on) override {
        if (window_ == nullptr) {
            return false;
        }
        glfwSetWindowAttrib(window_, GLFW_FLOATING, on ? GLFW_TRUE : GLFW_FALSE);
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
        int count = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&count);
        if (monitors == nullptr || count <= 0) {
            return out;
        }
        GLFWmonitor* hit = glfwGetWindowMonitor(window_);
        int index = 0;
        if (hit == nullptr) {
            int wx = 0;
            int wy = 0;
            int ww = 0;
            int wh = 0;
            glfwGetWindowPos(window_, &wx, &wy);
            glfwGetWindowSize(window_, &ww, &wh);
            const int cx = wx + ww / 2;
            const int cy = wy + wh / 2;
            hit = monitors[0];
            for (int i = 0; i < count; ++i) {
                int mx = 0;
                int my = 0;
                glfwGetMonitorPos(monitors[i], &mx, &my);
                const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
                if (mode == nullptr) {
                    continue;
                }
                if (cx >= mx && cy >= my && cx < mx + mode->width && cy < my + mode->height) {
                    hit = monitors[i];
                    index = i;
                    break;
                }
            }
        } else {
            for (int i = 0; i < count; ++i) {
                if (monitors[i] == hit) {
                    index = i;
                    break;
                }
            }
        }
        const GLFWvidmode* mode = glfwGetVideoMode(hit);
        const char* name = glfwGetMonitorName(hit);
        out.known = true;
        out.index = index;
        if (name != nullptr && name[0] != '\0') {
            out.id = name;
        } else {
            out.id = "monitor-" + std::to_string(index);
        }
        if (mode != nullptr) {
            out.width_px = mode->width;
            out.height_px = mode->height;
        }
        float xscale = 1.f;
        float yscale = 1.f;
        glfwGetMonitorContentScale(hit, &xscale, &yscale);
        out.scale = xscale > 0.f ? xscale : 1.f;
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

    GLFWcursor* palm_cursor(bool closed) {
        GLFWcursor*& slot = closed ? hand_closed_ : hand_open_;
        if (slot != nullptr) {
            return slot;
        }
        HandCursorImage image = hand_cursor_image(closed);
        GLFWimage glfw_image;
        glfw_image.width = image.width;
        glfw_image.height = image.height;
        glfw_image.pixels = image.rgba.data();
        slot = glfwCreateCursor(&glfw_image, image.hot_x, image.hot_y);
        return slot;
    }

    void set_cursor(Cursor cursor) override {
        if (window_ == nullptr) {
            return;
        }
        if (cursor == cursor_kind_ && cursor_handle_ != nullptr) {
            return;
        }
        GLFWcursor* next = nullptr;
        if (cursor == Cursor::Grab || cursor == Cursor::Grabbing) {
            next = palm_cursor(cursor == Cursor::Grabbing);
        } else {
            int shape = GLFW_ARROW_CURSOR;
            switch (cursor) {
                case Cursor::Pointer:
                    shape = GLFW_POINTING_HAND_CURSOR;
                    break;
                case Cursor::Text:
                    shape = GLFW_IBEAM_CURSOR;
                    break;
                case Cursor::Wait:
                    // GLFW has no wait cursor; keep the arrow.
                    shape = GLFW_ARROW_CURSOR;
                    break;
                case Cursor::NotAllowed:
                    shape = GLFW_NOT_ALLOWED_CURSOR;
                    break;
                case Cursor::EwResize:
                    shape = GLFW_RESIZE_EW_CURSOR;
                    break;
                case Cursor::NsResize:
                    shape = GLFW_RESIZE_NS_CURSOR;
                    break;
                case Cursor::NwseResize:
                    shape = GLFW_RESIZE_NWSE_CURSOR;
                    break;
                case Cursor::NeswResize:
                    shape = GLFW_RESIZE_NESW_CURSOR;
                    break;
                case Cursor::Move:
                    shape = GLFW_RESIZE_ALL_CURSOR;
                    break;
                case Cursor::Crosshair:
                    shape = GLFW_CROSSHAIR_CURSOR;
                    break;
                case Cursor::Grab:
                case Cursor::Grabbing:
                case Cursor::Default:
                    shape = GLFW_ARROW_CURSOR;
                    break;
            }
            next = glfwCreateStandardCursor(shape);
            if (next == nullptr && shape != GLFW_ARROW_CURSOR) {
                next = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
            }
        }
        if (next == nullptr) {
            return;
        }
        glfwSetCursor(window_, next);
        if (cursor_handle_ != nullptr && cursor_handle_ != next && cursor_handle_ != hand_open_ &&
            cursor_handle_ != hand_closed_) {
            glfwDestroyCursor(cursor_handle_);
        }
        cursor_handle_ = next;
        cursor_kind_ = cursor;
    }

    void set_ime(bool enabled) override {
#ifdef _WIN32
        if (window_ == nullptr) {
            return;
        }
        HWND hwnd = glfwGetWin32Window(window_);
        if (hwnd == nullptr) {
            return;
        }
        if (enabled) {
            if (saved_imc_ != nullptr) {
                ImmAssociateContext(hwnd, saved_imc_);
                saved_imc_ = nullptr;
            }
            return;
        }
        if (saved_imc_ == nullptr) {
            saved_imc_ = ImmAssociateContext(hwnd, nullptr);
        }
#else
        (void)enabled;
#endif
    }

    void set_ime_rect(float x, float y, float w, float h) override {
#ifdef _WIN32
        if (window_ == nullptr) {
            return;
        }
        HWND hwnd = glfwGetWin32Window(window_);
        if (hwnd == nullptr) {
            return;
        }
        ime_set_caret(hwnd, x, y, w, h);
#else
        (void)x;
        (void)y;
        (void)w;
        (void)h;
#endif
    }

    void framebuffer_size(int& width, int& height) const override {
        width = 0;
        height = 0;
        if (window_) {
            glfwGetFramebufferSize(window_, &width, &height);
        }
    }

    float content_scale() const override {
        if (window_ == nullptr) {
            return 1.f;
        }
        float x = 1.f, y = 1.f;
        glfwGetWindowContentScale(window_, &x, &y);
        const float from_content = (x > y ? x : y);
        if (from_content > 0.f) {
            return from_content;
        }
        int win_w = 0, win_h = 0, fb_w = 0, fb_h = 0;
        glfwGetWindowSize(window_, &win_w, &win_h);
        glfwGetFramebufferSize(window_, &fb_w, &fb_h);
        if (win_w <= 0) {
            return 1.f;
        }
        return static_cast<float>(fb_w) / static_cast<float>(win_w);
    }

    const char* name() const override { return "glfw3"; }

    NativeWindowId native_handle() const override {
        if (window_ == nullptr) {
            return {};
        }
#ifdef _WIN32
        return NativeWindowId(glfwGetWin32Window(window_));
#elif defined(__APPLE__)
        return NativeWindowId(glfwGetCocoaWindow(window_));
#elif defined(__linux__)
        const unsigned long xid = glfwGetX11Window(window_);
        if (xid == 0) {
            return {};
        }
        return NativeWindowId(reinterpret_cast<void*>(static_cast<uintptr_t>(xid)));
#else
        return {};
#endif
    }

    void discard_pending_input() override {
        // Drain OS messages into callback caches, then drop edges so a nested
        // modal return cannot synthesize a ghost click/key.
        if (window_ != nullptr) {
            glfwPollEvents();
        }
        frame_ = {};
        if (window_ != nullptr) {
            double mx = 0, my = 0;
            glfwGetCursorPos(window_, &mx, &my);
            mouse_left_down_ = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            mouse_right_down_ = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
            mouse_middle_down_ =
                glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
            PointerContact mouse;
            mouse.id = 1;
            mouse.kind = PointerKind::Mouse;
            mouse.primary = true;
            mouse.x = static_cast<float>(mx);
            mouse.y = static_cast<float>(my);
            mouse.buttons = (mouse_left_down_ ? 1 : 0) | (mouse_right_down_ ? 2 : 0) |
                            (mouse_middle_down_ ? 4 : 0);
            mouse.down = mouse.buttons != 0;
            frame_.contacts.push_back(mouse);
        }
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

    void* gl_proc(const char* name) const override {
        return reinterpret_cast<void*>(glfwGetProcAddress(name));
    }

    void gl_make_current() override {
        if (window_) {
            glfwMakeContextCurrent(window_);
        }
    }

    void gl_swap() override {
        if (window_) {
            glfwSwapBuffers(window_);
        }
    }

    bool vulkan_instance_extensions(uint32_t* count, const char* const** names) const override {
#ifdef ZUI_HAS_VULKAN
        uint32_t n = 0;
        const char** ext = glfwGetRequiredInstanceExtensions(&n);
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
        VkResult r = glfwCreateWindowSurface(static_cast<VkInstance>(instance), window_, nullptr, &surf);
        if (r != VK_SUCCESS) {
            ZUI_ERROR("glfwCreateWindowSurface failed");
            return false;
        }
        *surface = static_cast<void*>(surf);
        return true;
#else
        (void)instance;
        (void)surface;
        return false;
#endif
    }

private:
    void set_visibility(HostVisibility next) {
        if (visibility_ == next) {
            return;
        }
        visibility_ = next;
        visibility_changed_ = true;
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

    GLFWwindow* window_ = nullptr;
    GfxApi api_ = GfxApi::Vulkan;
    /// The frame GLFW reports: one mouse contact (GLFW has no touch API), plus wheel/modifiers.
    PointerFrame frame_;
    bool mouse_left_down_ = false;
    bool mouse_right_down_ = false;
    bool mouse_middle_down_ = false;
    TextInputFrame text_;
    ImeComposer ime_;
    std::vector<std::string> dropped_paths_;
    double last_click_time_ = -1.0;
    float last_click_x_ = 0;
    float last_click_y_ = 0;
    int click_streak_ = 0;
    GLFWcursor* cursor_handle_ = nullptr;
    GLFWcursor* hand_open_ = nullptr;
    GLFWcursor* hand_closed_ = nullptr;
    Cursor cursor_kind_ = Cursor::Default;
#ifdef _WIN32
    HIMC saved_imc_ = nullptr;
#endif
    HostVisibility visibility_ = HostVisibility::Visible;
    bool visibility_changed_ = false;
    HostOrientation orientation_ = HostOrientation::Unknown;
    bool orientation_changed_ = false;
    bool minimized_ = false;
    bool maximized_ = false;
    bool fullscreen_ = false;
    bool topmost_ = false;
    /// Windowed position restored when leaving fullscreen. Not an App geometry setter.
    int restore_x_ = 0;
    int restore_y_ = 0;
    int restore_w_ = 800;
    int restore_h_ = 600;
};

std::unique_ptr<IWindow> create_glfw3_window() {
    return std::make_unique<Glfw3Window>();
}

}  // namespace backends
}  // namespace zui

#endif  // ZUI_WITH_GLFW
