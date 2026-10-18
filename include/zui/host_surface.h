#ifndef ZUI_HOST_SURFACE_H
#define ZUI_HOST_SURFACE_H

// Backend and plugin surface. Application code includes <zui/app.h> only, and
// that header does not include this one.

#include <cstdint>

namespace zui {

/// Opaque window token passed to a dialog or shell plugin.
/// Call get() for the platform handle. Application headers do not mention this type.
class NativeWindowId {
public:
    NativeWindowId() = default;
    explicit NativeWindowId(void* handle) noexcept : handle_(handle) {}

    void* get() const noexcept { return handle_; }
    explicit operator bool() const noexcept { return handle_ != nullptr; }

    friend bool operator==(NativeWindowId lhs, NativeWindowId rhs) noexcept {
        return lhs.handle_ == rhs.handle_;
    }
    friend bool operator!=(NativeWindowId lhs, NativeWindowId rhs) noexcept {
        return !(lhs == rhs);
    }

private:
    void* handle_ = nullptr;
};

/// CSS cursor subset a window backend applies.
enum class Cursor {
    Default,
    Pointer,
    Text,
    Wait,
    NotAllowed,
    EwResize,
    NsResize,
    NwseResize,
    NeswResize,
    Move,       ///< CSS `move` / `all-scroll`
    Crosshair,  ///< CSS `crosshair`
    Grab,       ///< CSS `grab`
    Grabbing,   ///< CSS `grabbing`
};

/// OS / UA preference for `prefers-color-scheme` (CSS Media Queries 5).
enum class ColorSchemePreference { NoPreference, Light, Dark };

/// Primary pointing-device accuracy for `pointer` / `any-pointer` (CSS Media Queries 5).
enum class PointerAccuracy { None, Coarse, Fine };

/// Host-reported media facts filled before cascade. css never queries the OS itself.
/// Safe-area / keyboard insets are window-bound; the host copies them here each frame.
struct HostMediaFacts {
    /// Primary pointing device can conveniently hover (`hover: hover`).
    bool hover = true;
    /// At least one input can hover (`any-hover: hover`); defaults with `hover`.
    bool any_hover = true;
    /// Primary pointing-device accuracy (`pointer: fine|coarse|none`).
    PointerAccuracy pointer = PointerAccuracy::Fine;
    /// Most-capable pointing device (`any-pointer`); defaults with `pointer`.
    PointerAccuracy any_pointer = PointerAccuracy::Fine;
    /// User asked the system to minimize non-essential motion.
    bool reduced_motion = false;
    ColorSchemePreference color_scheme = ColorSchemePreference::Light;
    /// CSS-px safe-area insets (default 0).
    float safe_top = 0;
    float safe_right = 0;
    float safe_bottom = 0;
    float safe_left = 0;
    /// Virtual keyboard height in CSS px (default 0).
    float keyboard_bottom = 0;
};

}  // namespace zui

#endif  // ZUI_HOST_SURFACE_H
