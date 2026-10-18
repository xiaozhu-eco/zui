#ifndef ZUI_BACKENDS_HOST_MEDIA_H
#define ZUI_BACKENDS_HOST_MEDIA_H

#include <zui/export.h>
#include <zui/host_surface.h>
#include <zui/host_types.h>

#include <string>
#include <string_view>

namespace zui {
namespace backends {

/// Probe OS media preferences once. App maps the result into `css::MediaState` before cascade.
/// Win32 reads SPI / the personalize registry. macOS reads `defaults`. Linux reads gsettings
/// and the KDE animation factor. iOS / Android builds use the device hooks in mobile_device.h.
ZUI_EXPORT HostMediaFacts query_host_media();

/// The Windows input-capability signals one query reads, split out so the mapping below is a pure
/// function the host tests can drive without a touchscreen.
struct PointerSignals {
    bool mouse_present = true;
    bool touch_digitizer = false;
    bool pen_digitizer = false;
    /// `SM_CONVERTIBLESLATEMODE == 0`: tablet mode, the primary pointer is the screen.
    bool slate_mode = false;
};

/// `hover`: the *primary* pointer can hover. Tablet mode makes the touchscreen primary.
inline bool windows_primary_hovers(const PointerSignals& signals) {
    if (signals.slate_mode && signals.touch_digitizer) {
        return false;
    }
    return signals.mouse_present || signals.pen_digitizer;
}

/// `any-hover`: some attached input can hover (a mouse stays hovering in tablet mode).
inline bool windows_any_hovers(const PointerSignals& signals) {
    return signals.mouse_present || signals.pen_digitizer;
}

/// `pointer`: accuracy of the primary pointer.
inline PointerAccuracy windows_primary_pointer(const PointerSignals& signals) {
    if (windows_primary_hovers(signals)) {
        return PointerAccuracy::Fine;
    }
    return signals.touch_digitizer ? PointerAccuracy::Coarse : PointerAccuracy::None;
}

/// `any-pointer`: accuracy of the most capable pointer attached.
inline PointerAccuracy windows_any_pointer(const PointerSignals& signals) {
    if (signals.mouse_present || signals.pen_digitizer) {
        return PointerAccuracy::Fine;
    }
    return signals.touch_digitizer ? PointerAccuracy::Coarse : PointerAccuracy::None;
}

/// gsettings boolean `enable-animations` / `gtk-enable-animations`: false means reduced motion.
inline bool gnome_enable_animations_is_off(std::string_view raw) {
    std::string value;
    value.reserve(raw.size());
    for (char c : raw) {
        if (c == '\'' || c == '"' || c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            continue;
        }
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
        value.push_back(c);
    }
    return value == "false" || value == "0";
}

/// KDE AnimationDurationFactor. 0 disables animations. Empty or non-numeric is "not off".
inline bool kde_animation_factor_is_off(std::string_view raw) {
    std::size_t begin = 0;
    std::size_t end = raw.size();
    while (begin < end &&
           (raw[begin] == ' ' || raw[begin] == '\n' || raw[begin] == '\r' || raw[begin] == '\t')) {
        ++begin;
    }
    while (end > begin &&
           (raw[end - 1] == ' ' || raw[end - 1] == '\n' || raw[end - 1] == '\r' ||
            raw[end - 1] == '\t')) {
        --end;
    }
    if (begin == end) {
        return false;
    }
    bool saw_digit = false;
    bool nonzero = false;
    for (std::size_t i = begin; i < end; ++i) {
        const char c = raw[i];
        if (c == '.') {
            continue;
        }
        if (c < '0' || c > '9') {
            return false;
        }
        saw_digit = true;
        if (c != '0') {
            nonzero = true;
        }
    }
    return saw_digit && !nonzero;
}

}  // namespace backends
}  // namespace zui

#endif
