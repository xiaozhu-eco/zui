#include "paint/painter_internal.h"

#include "paint/display_list.h"

#include "perf/perf_budget.h"
#include "css/transition/transition.h"
#include "image/image.h"
#include "log/log.h"
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
#include <vector>

namespace zui {
namespace paint {
namespace painter_detail {

void paint_box_outline(DisplayList& out, const layout::Box& box, const BoxDecorationPaint& d) {
    const Rect& br = d.br;
    const CornerRadii& radii = d.radii;
    const bool paint_self = d.paint_self;
    const float op = d.op;

    // CSS UI 4 section 2: the outline is a ring of `outline-width` whose inner edge sits
    // `outline-offset` outside the border box (a negative offset pulls it inside). It follows the
    // border radius and is not clipped by this box's own overflow, so it paints before that clip
    // goes on. Chrome paints it above the background and border, which is where this sits.
    // Intern keeps solid/dashed/dotted/double. groove/ridge/inset/outset stay on the computed
    // style; outline does not affect layout, so paint reads those four here and returns
    // immediately when the property is missing.
    layout::BorderLineStyle outline_style = box.style().outline_style;
    float outline_width = box.style().outline_width;
    float outline_offset = box.style().outline_offset;
    if (paint_self && outline_style == layout::BorderLineStyle::None) {
        if (const std::string* sv = computed(box, "outline-style")) {
            const std::string kind = lower_ascii(*sv);
            if (kind == "groove") {
                outline_style = layout::BorderLineStyle::Groove;
            } else if (kind == "ridge") {
                outline_style = layout::BorderLineStyle::Ridge;
            } else if (kind == "inset") {
                outline_style = layout::BorderLineStyle::Inset;
            } else if (kind == "outset") {
                outline_style = layout::BorderLineStyle::Outset;
            }
            if (outline_style != layout::BorderLineStyle::None) {
                outline_width = 3.f;
                outline_offset = 0.f;
                if (const std::string* wv = computed(box, "outline-width")) {
                    const std::string width = lower_ascii(*wv);
                    if (width == "thin") {
                        outline_width = 1.f;
                    } else if (width == "thick") {
                        outline_width = 5.f;
                    } else if (width != "medium" && !width.empty()) {
                        char* end = nullptr;
                        const float v = std::strtof(width.c_str(), &end);
                        if (end != nullptr && end != width.c_str()) {
                            const std::string_view unit = trim_sv_local(std::string_view(end));
                            if (unit.empty() || unit == "px") {
                                outline_width = v;
                            } else if (unit == "em" || unit == "rem") {
                                outline_width = v * box.style().font_size;
                            }
                        }
                    }
                }
                if (const std::string* ov = computed(box, "outline-offset")) {
                    char* end = nullptr;
                    const float v = std::strtof(ov->c_str(), &end);
                    if (end != nullptr && end != ov->c_str()) {
                        const std::string_view unit = trim_sv_local(std::string_view(end));
                        if (unit.empty() || unit == "px") {
                            outline_offset = v;
                        } else if (unit == "em" || unit == "rem") {
                            outline_offset = v * box.style().font_size;
                        }
                    }
                }
            }
        }
    }
    if (paint_self && outline_width > 0.5f &&
        outline_style != layout::BorderLineStyle::None) {
        const float grow = outline_offset + outline_width;
        const Rect outer{br.x - grow, br.y - grow, br.w + 2.f * grow, br.h + 2.f * grow};
        const Color outline_color = apply_opacity(
            [&] {
                const std::string* v = computed(box, "outline-color");
                if (v == nullptr || v->empty() || *v == "auto" || *v == "invert") {
                    return paint_color(box, computed(box, "color"));
                }
                return paint_color(box, v);
            }(),
            op);
        if (outer.w > 0.f && outer.h > 0.f && !color_is_transparent(outline_color)) {
            const layout::BorderLineStyle ost = outline_style;
            const CornerRadii outline_radii = inset_radii(radii, -grow);
            // Dashed and dotted follow the outline centerline. Double, groove, ridge, inset,
            // and outset with a radius stay a solid stroke.
            if ((ost == layout::BorderLineStyle::Dashed ||
                 ost == layout::BorderLineStyle::Dotted) &&
                outline_radii.any() &&
                paint_rounded_centerline_stroke(out, outer, outline_radii, outline_width,
                                                outline_color,
                                                ost == layout::BorderLineStyle::Dotted)) {
            } else if (ost == layout::BorderLineStyle::Solid || radii.any()) {
                out.stroke_rect(outer, outline_color, outline_width, outline_radii);
            } else {
                // Sharp dashed, dotted, double, groove, ridge, inset, and outset. `outer` is the
                // ring's outer edge; the stroke sits inside it, same formulas as a border.
                const float t = outline_width;
                paint_border_edge(out, box, true, Rect{outer.x, outer.y, outer.w, t}, outline_color, ost,
                           border_dark_outer_for(ost, true), true);
                paint_border_edge(out, box, true, Rect{outer.x, outer.y + outer.h - t, outer.w, t}, outline_color,
                           ost, border_dark_outer_for(ost, false), false);
                if (ost == layout::BorderLineStyle::Dotted) {
                    paint_border_edge(out, box, false, Rect{outer.x, outer.y, t, outer.h}, outline_color, ost, true,
                               true);
                    paint_border_edge(out, box, false, Rect{outer.x + outer.w - t, outer.y, t, outer.h},
                               outline_color, ost, true, false);
                } else {
                    const float mid_y = outer.y + t;
                    const float mid_h = std::max(0.f, outer.h - 2.f * t);
                    paint_border_edge(out, box, false, Rect{outer.x, mid_y, t, mid_h}, outline_color, ost,
                               border_dark_outer_for(ost, true), true);
                    paint_border_edge(out, box, false, Rect{outer.x + outer.w - t, mid_y, t, mid_h}, outline_color,
                               ost, border_dark_outer_for(ost, false), false);
                }
            }
        }
    }
}

}  // namespace painter_detail
}  // namespace paint
}  // namespace zui
