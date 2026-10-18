#include "paint/painter_internal.h"

#include "paint/display_list.h"

#include "perf/perf_budget.h"
#include "css/transition/transition.h"
#include "image/image.h"
#include "layout/clip_path.h"
#include "layout/overflow_clip.h"
#include "layout/paint_map.h"
#include "layout/paint_transform.h"
#include "layout/scrollbar_h.h"
#include "layout/scrollbar_v.h"
#include "layout/stacking.h"

#include "text/font_chain.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zui {
namespace paint {
namespace painter_detail {
float run_advance_px(const layout::TextRun& run) {
    if (!run.glyph_advances.empty()) {
        float w = 0.f;
        for (float a : run.glyph_advances) {
            w += a;
        }
        return std::max(0.f, w);
    }
    // Fallback when advances were not copied: rough em-width estimate.
    return static_cast<float>(run.text.size()) * 0.5f * 16.f;
}

/// CSS 2.1 section 17.6.1 "empty": a table cell is empty when it has no in-flow content. Comments
/// and collapsed whitespace never become boxes at all, floats and out-of-flow children are not in
/// flow, while an empty `<br>` or `<span>` is a box and therefore content.
bool cell_has_flow_content(const layout::Box& box) {
    if (!box.text.empty()) {
        return true;
    }
    for (const layout::Box* child : box.children) {
        if (child == nullptr || child->style_ptr == nullptr) {
            continue;
        }
        const layout::LayoutStyle& style = child->style();
        if (style.floating || style.position_absolute || style.position_fixed) {
            continue;
        }
        return true;
    }
    return false;
}

// The classic scrollbar occupies the end of the padding box. Content is clipped to the
// client area so glyphs and children do not paint into that slot.
Rect scrollport_rect(const layout::Box& box, float dx, float dy, float thickness) {
    Rect clip = padding_rect(box, dx, dy);
    const layout::ScrollbarV vertical = layout::vertical_scrollbar(box, thickness);
    const layout::ScrollbarH horizontal = layout::horizontal_scrollbar(box, thickness);
    if (vertical.visible) {
        clip.w = std::max(0.0f, clip.w - vertical.track_w);
    }
    if (horizontal.visible) {
        clip.h = std::max(0.0f, clip.h - horizontal.track_h);
    }
    return clip;
}

/// The clip a *lifted* descendant is painted through.
///
/// Appendix E hands a positioned / stacking-context child to the nearest ancestor context, so a
/// `position: relative` child of an `overflow: scroll` box is walked by that ancestor and not by
/// its own parent. Every scroll container between the walker and the child still clips it: without
/// this the child was painted after, and outside, the scrollport - and therefore also covered the
/// scrollbars the container had just painted. `writing-mode-vertical-scroll` measured it: `#wide`
/// (relative) covered x -44..156 while the scrollport is x 16..81, so the whole gutter plus the
/// bars showed the content colour instead of the track/thumb.
///
/// Returns true when it narrowed the cull rectangle. The rectangle does not reach the raster:
/// radii live on the clips `push_lifted_overflow_clips` emits around the lifted child.
/// `position: fixed` is taken out of ancestor overflow (the select's `overflow: clip`, a dialog's
/// `overflow: auto`). A transform or `contain: paint` still forms the containing block and clips.
bool fixed_escapes_overflow(const layout::Box& child, const layout::Box& ancestor) {
    if (child.style_ptr == nullptr || !child.style().position_fixed) {
        return false;
    }
    if (ancestor.paint_transformed) {
        return false;
    }
    if (ancestor.style_ptr != nullptr && ancestor.style().contain_paint) {
        return false;
    }
    return true;
}

bool apply_lifted_clip(const layout::Box& child, const layout::Box& walker, float dx, float dy,
                       float thickness, Rect& clip, bool& has_clip) {
    bool narrowed = false;
    for (const layout::Box* p = child.parent; p != nullptr && p != &walker; p = p->parent) {
        if (p->style_ptr == nullptr || !layout::clips_overflow(*p)) {
            continue;
        }
        if (fixed_escapes_overflow(child, *p)) {
            continue;
        }
        const Rect port = scrollport_rect(*p, dx, dy, thickness);
        clip = has_clip ? intersect_rect(clip, port) : port;
        has_clip = true;
        narrowed = true;
    }
    return narrowed;
}

void push_overflow_clip(DisplayList& out, const layout::Box& box, float dx, float dy,
                        float thickness) {
    const Rect br = border_rect(box, dx, dy);
    const CornerRadii radii = resolve_border_radii(box.style(), br.w, br.h);
    const float bw = std::max({box.geo.border.top, box.geo.border.right, box.geo.border.bottom,
                               box.geo.border.left});
    const CornerRadii clip_r = inset_radii(radii, bw);
    if (box.paint_transformed) {
        const Rect world_pad = scrollport_rect(box, dx, dy, thickness);
        const Rect local_pad{world_pad.x - box.geo.world_x, world_pad.y - box.geo.world_y,
                             world_pad.w, world_pad.h};
        out.push_clip_xform(local_pad, clip_r, box.paint_a, box.paint_b, box.paint_c, box.paint_d,
                            box.paint_e, box.paint_f);
        return;
    }
    out.push_clip(scrollport_rect(box, dx, dy, thickness), clip_r);
}

int push_lifted_overflow_clips(DisplayList& out, const layout::Box& child,
                               const layout::Box& walker, float dx, float dy, float thickness) {
    int count = 0;
    for (const layout::Box* p = child.parent; p != nullptr && p != &walker; p = p->parent) {
        if (p->style_ptr != nullptr && layout::clips_overflow(*p) &&
            !fixed_escapes_overflow(child, *p)) {
            ++count;
        }
    }
    for (int index = count - 1; index >= 0; --index) {
        int seen = 0;
        for (const layout::Box* p = child.parent; p != nullptr && p != &walker; p = p->parent) {
            if (p->style_ptr == nullptr || !layout::clips_overflow(*p) ||
                fixed_escapes_overflow(child, *p)) {
                continue;
            }
            if (seen == index) {
                push_overflow_clip(out, *p, dx, dy, thickness);
                break;
            }
            ++seen;
        }
    }
    return count;
}

}  // namespace painter_detail
}  // namespace paint
}  // namespace zui
