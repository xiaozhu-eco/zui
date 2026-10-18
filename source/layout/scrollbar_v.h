#ifndef ZUI_LAYOUT_SCROLLBAR_V_H
#define ZUI_LAYOUT_SCROLLBAR_V_H

#include "layout/box.h"

#include <algorithm>

namespace zui {
namespace layout {

struct ScrollbarV {
    bool visible = false;
    float track_x = 0;
    float track_y = 0;
    float track_w = 0;
    float track_h = 0;
    float thumb_y = 0;
    float thumb_h = 0;
    float max_scroll = 0;
};

/// Thickness layout reserves and paint draws for this box.
/// `none` reserves nothing, so a reading pane that hides its bar keeps the full client width.
/// `thin` is 10px on Chrome's classic bar (the pinned 15px auto bar leaves a 150px client inside
/// a 160px box). A platform thinner than that stays at the platform value.
inline float scrollbar_used_thickness(const LayoutStyle& style, float platform) {
    if (platform <= 0.f || style.scrollbar_width == ScrollbarWidth::None) {
        return 0.f;
    }
    if (style.scrollbar_width == ScrollbarWidth::Thin) {
        return std::min(platform, 10.f);
    }
    return platform;
}

/// `scrollbar-gutter: stable` reserves a classic bar even when the content fits.
/// `both-edges` reserves that thickness on the opposite edge as well (CSS Overflow).
inline bool scrollbar_gutter_forces_reserve(const LayoutStyle& style) {
    return style.scrollbar_gutter == ScrollbarGutter::Stable ||
           style.scrollbar_gutter == ScrollbarGutter::BothEdges;
}

/// Inline space a reserved block scrollbar takes out of the content box. Zero when this pass
/// is not reserving. `both-edges` takes the thickness twice.
inline float scrollbar_gutter_span(const LayoutStyle& style, float thickness, bool reserve) {
    if (!reserve || thickness <= 0.f) {
        return 0.f;
    }
    return style.scrollbar_gutter == ScrollbarGutter::BothEdges ? thickness * 2.f : thickness;
}

/// How far items shift off the inline-start edge so the opposite gutter stays empty.
inline float scrollbar_gutter_start(const LayoutStyle& style, float thickness, bool reserve) {
    if (!reserve || thickness <= 0.f || style.scrollbar_gutter != ScrollbarGutter::BothEdges) {
        return 0.f;
    }
    return thickness;
}

/// The viewport's vertical bar, in viewport coordinates. The document element is the scroller
/// even when its own overflow is `visible` (CSS Overflow §3.5); the track sits in the gutter
/// layout reserved outside the ICB, not on the root box's border edge.
ScrollbarV viewport_vertical_scrollbar(const Box& root, float viewport_w, float viewport_h,
                                       float thickness);

/// The bar an element scroll container draws on its block axis, in world coordinates.
///
/// `thickness` is the platform parameter LAYOUT section 9 injects - the same value the layout
/// reserved for the bar. The test is against the box's *scrollable extent* (`scrollHeight`), not its
/// used content box: a `white-space: nowrap` sample whose content box is its own width still has a
/// horizontal bar under it.
ScrollbarV vertical_scrollbar(const Box& box, float thickness);

}  // namespace layout
}  // namespace zui

#endif
