#ifndef ZUI_LAYOUT_PLATFORM_METRICS_H
#define ZUI_LAYOUT_PLATFORM_METRICS_H

namespace zui {
namespace layout {

/// Platform parameters that LAYOUT.md section 9 requires to come from the platform layer
/// instead of a compile-time constant, injected once when the engine is built.
///
/// `scrollbar_thickness` is the classic (non-overlay) scrollbar width in CSS pixels. The value
/// below is the one the comparison runs against: Chrome's classic scrollbar at device scale 1
/// on Windows, pinned in TESTPLAN.md section 8. Win32 reports `SM_CXVSCROLL = 17` on the same
/// host, which is exactly the drift the section 9 strategy warns about - the comparison pins
/// the browser's shape, and a build that changes it shows up as L0 drift.
struct PlatformMetrics {
    float scrollbar_thickness = 15.f;
};

}  // namespace layout
}  // namespace zui

#endif
