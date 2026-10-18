#ifndef ZUI_LAYOUT_SCROLLBAR_METRICS_H
#define ZUI_LAYOUT_SCROLLBAR_METRICS_H

#include <algorithm>
#include <cmath>

namespace zui {
namespace layout {

/// Chrome's classic scrollbar reserves an 18px arrow button at each end of the bar; the thumb
/// travels between them and is never shorter than 17px.
inline constexpr float kScrollbarButtonLength = 18.0f;
inline constexpr float kScrollbarMinThumbLength = 17.0f;

/// How much of the bar the thumb covers, in px, when the bar is `length` long and holds `content`
/// of content in a `length`-long viewport.
///
/// Measured against the pinned browser (`build/thumb_geo_probe.py`, colour profile of the bar plus
/// thumb drags): a 200px bar over 900px of content has an 18px button at each end, a 36px thumb -
/// `round(164 * 200 / 900)` - and therefore a 128px travel, which is what makes an 80px thumb drag
/// land on `round(80 * 700 / 128)` = 438, the offset `thumb-drag`'s golden recorded. A bar too
/// short for two buttons keeps its whole length.
inline float scrollbar_thumb_extent(float length, float content) {
    const float inner = length - 2.0f * kScrollbarButtonLength;
    if (inner <= 0.0f) {
        return length;
    }
    return std::min(inner, std::max(kScrollbarMinThumbLength,
                                    std::round(inner * length / std::max(content, 1.0f))));
}

/// Where the thumb starts along the bar, relative to the track's start, for a scroll fraction `t`.
inline float scrollbar_thumb_offset(float length, float content, float t) {
    const float inner = length - 2.0f * kScrollbarButtonLength;
    if (inner <= 0.0f) {
        return 0.0f;
    }
    return kScrollbarButtonLength +
           t * std::max(0.0f, inner - scrollbar_thumb_extent(length, content));
}

/// The distance the thumb travels between the two ends of its track, in px. A thumb drag is mapped
/// onto the scroll range through this, so it has to be the same number the painted thumb uses.
inline float scrollbar_travel(float length, float content) {
    return std::max(1.0f, scrollbar_thumb_offset(length, content, 1.0f) -
                              scrollbar_thumb_offset(length, content, 0.0f));
}

}  // namespace layout
}  // namespace zui

#endif
