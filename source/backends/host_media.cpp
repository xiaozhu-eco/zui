#include "backends/host_media.h"
#include "backends/mobile_device.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace zui {
namespace backends {
namespace {

#if !defined(_WIN32)
bool run_quiet(const char* cmd, std::string& out) {
    out.clear();
    FILE* pipe = popen(cmd, "r");
    if (pipe == nullptr) {
        return false;
    }
    char buf[512];
    while (fgets(buf, sizeof(buf), pipe) != nullptr) {
        out.append(buf);
    }
    const int code = pclose(pipe);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) {
        out.pop_back();
    }
    return code == 0;
}
#endif

bool os_prefers_reduced_motion() {
#if (defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__ANDROID__)
    return false;
#elif defined(_WIN32)
    BOOL enabled = TRUE;
    if (SystemParametersInfo(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0)) {
        return enabled == FALSE;
    }
#elif defined(__APPLE__)
    // macOS: reduce motion accessibility preference. iOS is handled by device_apply_host_media.
    std::string out;
    if (run_quiet("defaults read com.apple.universalaccess reduceMotion 2>/dev/null", out)) {
        return out == "1";
    }
#elif defined(__linux__)
    std::string out;
    if (run_quiet("gsettings get org.gnome.desktop.interface enable-animations 2>/dev/null", out) ||
        run_quiet("gsettings get org.gnome.desktop.interface gtk-enable-animations 2>/dev/null",
                  out)) {
        if (gnome_enable_animations_is_off(out)) {
            return true;
        }
    }
    if (run_quiet("kreadconfig6 --file kdeglobals --group KDE --key AnimationDurationFactor "
                  "2>/dev/null",
                  out) ||
        run_quiet("kreadconfig5 --file kdeglobals --group KDE --key AnimationDurationFactor "
                  "2>/dev/null",
                  out)) {
        if (kde_animation_factor_is_off(out)) {
            return true;
        }
    }
#endif
    return false;
}

#if defined(_WIN32)
/// The Windows input signals `hover` / `any-hover` / `pointer` / `any-pointer` read. Split out so
/// the mapping in host_media.h stays a pure function the host tests can drive.
PointerSignals windows_pointer_signals() {
    PointerSignals signals;
    const int digitizer = GetSystemMetrics(SM_DIGITIZER);
    const bool ready = (digitizer & NID_READY) != 0;
    signals.touch_digitizer =
        ready && (digitizer & (NID_INTEGRATED_TOUCH | NID_EXTERNAL_TOUCH)) != 0;
    signals.pen_digitizer = ready && (digitizer & (NID_INTEGRATED_PEN | NID_EXTERNAL_PEN)) != 0;
    signals.mouse_present = GetSystemMetrics(SM_MOUSEPRESENT) != 0;
    signals.slate_mode = GetSystemMetrics(SM_CONVERTIBLESLATEMODE) == 0;
    return signals;
}
#endif

ColorSchemePreference os_color_scheme() {
#if (defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__ANDROID__)
    return ColorSchemePreference::Light;
#elif defined(_WIN32)
    DWORD value = 1;
    DWORD size = sizeof(value);
    const LSTATUS st =
        RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    if (st == ERROR_SUCCESS) {
        return value == 0 ? ColorSchemePreference::Dark : ColorSchemePreference::Light;
    }
#elif defined(__APPLE__)
    // AppleInterfaceStyle is "Dark" when dark mode is on; missing → light.
    std::string out;
    if (run_quiet("defaults read -g AppleInterfaceStyle 2>/dev/null", out)) {
        if (out.find("Dark") != std::string::npos) {
            return ColorSchemePreference::Dark;
        }
    }
#elif defined(__linux__)
    std::string out;
    if (run_quiet("gsettings get org.gnome.desktop.interface color-scheme 2>/dev/null", out)) {
        if (out.find("dark") != std::string::npos) {
            return ColorSchemePreference::Dark;
        }
        if (out.find("default") != std::string::npos || out.find("light") != std::string::npos) {
            return ColorSchemePreference::Light;
        }
    }
    if (run_quiet("gsettings get org.gnome.desktop.interface gtk-theme 2>/dev/null", out)) {
        // Heuristic: theme name containing "-dark" / "Dark".
        for (char& c : out) {
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        if (out.find("dark") != std::string::npos) {
            return ColorSchemePreference::Dark;
        }
    }
#endif
    return ColorSchemePreference::Light;
}

}  // namespace

HostMediaFacts query_host_media() {
    HostMediaFacts facts;
#if (defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE) || defined(__ANDROID__)
    if (device_apply_host_media(facts)) {
        return facts;
    }
    // No device hooks (the desktop-CI stub): a touch platform by definition.
    facts.hover = false;
    facts.any_hover = false;
    facts.pointer = PointerAccuracy::Coarse;
    facts.any_pointer = PointerAccuracy::Coarse;
#elif defined(_WIN32)
    // `pointer` / `any-pointer` / `hover` / `any-hover` come from the mouse and digitizer
    // signals: tablet mode makes the touchscreen primary while an attached mouse still counts as
    // `any-hover: hover` (CSS Media Queries 5).
    const PointerSignals signals = windows_pointer_signals();
    facts.hover = windows_primary_hovers(signals);
    facts.any_hover = windows_any_hovers(signals);
    facts.pointer = windows_primary_pointer(signals);
    facts.any_pointer = windows_any_pointer(signals);
#else
    // Desktop macOS / Linux always have a hovering fine pointer. iOS/Android returned above.
    facts.hover = true;
    facts.any_hover = true;
    facts.pointer = PointerAccuracy::Fine;
    facts.any_pointer = PointerAccuracy::Fine;
#endif
    facts.reduced_motion = os_prefers_reduced_motion();
    facts.color_scheme = os_color_scheme();
    return facts;
}

}  // namespace backends
}  // namespace zui
