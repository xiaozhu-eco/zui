#include "backends/mobile_device.h"
#include "backends/window.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace zui {
namespace backends {
namespace {

/// Headless / compile-seam IWindow for iOS and Android until a real system surface lands.
/// create stores the desc; poll stays true until destroy or request_close; no GPU surface.
/// Share / open-with: host calls inject_opened_paths; App drains take_dropped_paths → ondrop.
class MobileStubWindow final : public IWindow {
public:
    explicit MobileStubWindow(const char* backend_name) : name_(backend_name) {}

    bool create(const WindowDesc& desc) override {
        desc_ = desc;
        alive_ = true;
        close_requested_ = false;
        frame_ = {};
        text_ = {};
        dropped_paths_.clear();
        visibility_ = HostVisibility::Visible;
        visibility_changed_ = false;
        orientation_ = HostOrientation::Unknown;
        orientation_changed_ = false;
        note_orientation_from_size(desc.width, desc.height, /*force_edge=*/false);
        return true;
    }

    void destroy() override {
        alive_ = false;
        close_requested_ = false;
        dropped_paths_.clear();
    }

    bool poll() override {
        if (!alive_) {
            return false;
        }
        // No contacts: this stub has no OS surface yet. Production mobile fills the frame from
        // its touch system (PR-INPUT-1 keeps the seam; the pointer is not "the mouse").
        frame_ = {};
        text_ = {};
        return true;
    }

    bool take_close_request() override {
        if (!close_requested_) {
            return false;
        }
        close_requested_ = false;
        return true;
    }

    void framebuffer_size(int& width, int& height) const override {
        width = desc_.width > 0 ? desc_.width : 1;
        height = desc_.height > 0 ? desc_.height : 1;
    }

    float content_scale() const override { return 1.f; }

    DisplayInsets safe_area_insets() const override { return safe_area_; }

    DisplayInsets keyboard_insets() const override {
        DisplayInsets kb{};
        kb.bottom = keyboard_bottom_;
        return kb;
    }

    void set_title(std::string_view title) override {
        desc_.title.assign(title.begin(), title.end());
    }

    void set_window_size(int w, int h) override {
        if (w > 0) {
            desc_.width = w;
        }
        if (h > 0) {
            desc_.height = h;
        }
        note_orientation_from_size(desc_.width, desc_.height, /*force_edge=*/true);
    }

    bool set_icon(const WindowIcon& icon) override {
        if (!alive_ || !icon.valid()) {
            return false;
        }
        icon_ = icon;
        has_icon_ = true;
        return true;
    }

    bool minimize() override {
        if (!alive_) {
            return false;
        }
        minimized_ = true;
        maximized_ = false;
        return true;
    }

    bool set_maximized(bool on) override {
        if (!alive_) {
            return false;
        }
        maximized_ = on;
        minimized_ = false;
        return true;
    }

    bool set_fullscreen(bool on) override {
        if (!alive_) {
            return false;
        }
        fullscreen_ = on;
        return true;
    }

    bool set_topmost(bool on) override {
        if (!alive_) {
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
        if (!alive_) {
            return out;
        }
        out.known = true;
        out.id = std::string(name_) + "-display";
        out.index = 0;
        out.width_px = desc_.width;
        out.height_px = desc_.height;
        out.scale = 1.f;
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

    void set_safe_area_insets_for_test(DisplayInsets insets) override { safe_area_ = insets; }

    void set_keyboard_bottom_for_test(float bottom_css_px) override {
        keyboard_bottom_ = bottom_css_px > 0.f ? bottom_css_px : 0.f;
    }

    void set_visibility_for_test(HostVisibility v) override {
        if (visibility_ == v) {
            return;
        }
        visibility_ = v;
        visibility_changed_ = true;
    }

    void set_orientation_for_test(HostOrientation o) override {
        if (orientation_ == o) {
            return;
        }
        orientation_ = o;
        orientation_changed_ = true;
    }

    const PointerFrame& pointers() const override { return frame_; }
    const TextInputFrame& text() const override { return text_; }
    void set_ime(bool) override {}
    const char* name() const override { return name_; }

    void discard_pending_input() override {
        frame_ = {};
        text_ = {};
        dropped_paths_.clear();
    }

    std::vector<std::string> take_dropped_paths() override {
        std::vector<std::string> out;
        out.swap(dropped_paths_);
        return out;
    }

    ClipboardText clipboard_text() override {
#if (defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__ANDROID__)
        return device_clipboard_text();
#else
        return {};
#endif
    }

    bool set_clipboard_text(std::string_view text) override {
#if (defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__ANDROID__)
        return device_set_clipboard_text(text);
#else
        (void)text;
        return false;
#endif
    }

    void inject_opened_paths(std::vector<std::string> paths) override {
        for (std::string& path : paths) {
            if (!path.empty()) {
                dropped_paths_.push_back(std::move(path));
            }
        }
    }

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

    /// Test / host helper: arm a close request the next take_close_request() consumes.
    void request_close() { close_requested_ = true; }

private:
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

    const char* name_ = "mobile";
    WindowDesc desc_{};
    bool alive_ = false;
    bool close_requested_ = false;
    PointerFrame frame_{};
    TextInputFrame text_{};
    std::vector<std::string> dropped_paths_;
    DisplayInsets safe_area_{};
    float keyboard_bottom_ = 0.f;
    HostVisibility visibility_ = HostVisibility::Visible;
    bool visibility_changed_ = false;
    HostOrientation orientation_ = HostOrientation::Unknown;
    bool orientation_changed_ = false;
    WindowIcon icon_{};
    bool has_icon_ = false;
    bool minimized_ = false;
    bool maximized_ = false;
    bool fullscreen_ = false;
    bool topmost_ = false;
};

}  // namespace

std::unique_ptr<IWindow> create_ios_window() {
    return std::make_unique<MobileStubWindow>("ios");
}

std::unique_ptr<IWindow> create_android_window() {
    return std::make_unique<MobileStubWindow>("android");
}

}  // namespace backends
}  // namespace zui
