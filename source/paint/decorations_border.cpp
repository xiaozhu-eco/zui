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

void paint_box_border(DisplayList& out, const layout::Box& box, const BoxDecorationPaint& d,
                      const std::string& resource_base) {
    const Rect& br = d.br;
    const CornerRadii& radii = d.radii;
    const float bt = d.bt;
    const float brw = d.brw;
    const float bb = d.bb;
    const float bl = d.bl;
    const Color& ct = d.ct;
    const Color& cr = d.cr;
    const Color& cb = d.cb;
    const Color& cl = d.cl;
    const Color& bg = d.bg;
    const bool paint_self = d.paint_self;
    const bool empty_cell_hidden = d.empty_cell_hidden;
    const bool has_paintable_bg = d.has_paintable_bg;
    const BgBoxKind clip_kind = d.clip_kind;
    const float dx = d.dx;
    const float dy = d.dy;
    const float op = d.op;
    const layout::BorderLineStyle st_t = box.style().border_top_style;
    const layout::BorderLineStyle st_r = box.style().border_right_style;
    const layout::BorderLineStyle st_b = box.style().border_bottom_style;
    const layout::BorderLineStyle st_l = box.style().border_left_style;
    const bool any_border =
        paint_self && !empty_cell_hidden && (bt > 0.5f || brw > 0.5f || bb > 0.5f || bl > 0.5f);
    const bool widths_equal =
        std::fabs(bt - brw) < 0.5f && std::fabs(bt - bb) < 0.5f && std::fabs(bt - bl) < 0.5f;
    const bool colors_equal = ct.r == cr.r && ct.g == cr.g && ct.b == cr.b && ct.a == cr.a &&
                              ct.r == cb.r && ct.g == cb.g && ct.b == cb.b && ct.a == cb.a &&
                              ct.r == cl.r && ct.g == cl.g && ct.b == cl.b && ct.a == cl.a;
    const bool styles_solid =
        (st_t == layout::BorderLineStyle::Solid || st_t == layout::BorderLineStyle::None ||
         st_t == layout::BorderLineStyle::Hidden) &&
        (st_r == layout::BorderLineStyle::Solid || st_r == layout::BorderLineStyle::None ||
         st_r == layout::BorderLineStyle::Hidden) &&
        (st_b == layout::BorderLineStyle::Solid || st_b == layout::BorderLineStyle::None ||
         st_b == layout::BorderLineStyle::Hidden) &&
        (st_l == layout::BorderLineStyle::Solid || st_l == layout::BorderLineStyle::None ||
         st_l == layout::BorderLineStyle::Hidden);

    // CSS Backgrounds 3 §5: a paintable border-image replaces the border stroke. The border
    // width still occupies layout. Inset shadows have already been emitted underneath.
    bool replaced_border = false;
    if (paint_self && !empty_cell_hidden && !box.paint_transformed) {
        replaced_border = paint_border_image(out, box, br, op, resource_base);
    }

    // Rounded borders keep the outer arcs (CSS Backgrounds 3 §5.3 / §6). Equal-width same-colour
    // solid uses one stroke. Unequal or mixed sides: opaque border-box background punches the
    // padding box with asymmetric inset radii; otherwise same-colour sides emit a soft-raster
    // ring (outer − hole). border-*-style none/hidden are skipped via used width 0 / paint_edge.
    // Rounded inset/outset uses the two-tone rim. Dashed and dotted follow the centerline.
    // Double, groove, and ridge with a radius still fall back to a solid stroke.
    if (!replaced_border && any_border && radii.any()) {
        const Rect pad = padding_rect(box, dx, dy);
        const CornerRadii pad_radii = inset_radii_sides(radii, bt, brw, bb, bl);
        const bool has_bg_image = has_paintable_bg;
        const bool can_punch_pad = pad.w > 0.5f && pad.h > 0.5f && !color_is_transparent(bg) &&
                                   !has_bg_image && clip_kind == BgBoxKind::Border && styles_solid;
        const bool can_ring = pad.w > 0.5f && pad.h > 0.5f && styles_solid;
        auto side_clip_h = [&]() { return std::max(bt, std::max(radii.tl_y, radii.tr_y)); };
        auto side_clip_b = [&]() { return std::max(bb, std::max(radii.bl_y, radii.br_y)); };
        auto side_clip_l = [&]() { return std::max(bl, std::max(radii.tl, radii.bl)); };
        auto side_clip_r = [&]() { return std::max(brw, std::max(radii.tr, radii.br)); };
        const bool uniform_dash =
            widths_equal && colors_equal && bt > 0.5f && !color_is_transparent(ct) &&
            !box.paint_transformed && st_t == st_r && st_t == st_b && st_t == st_l &&
            (st_t == layout::BorderLineStyle::Dashed ||
             st_t == layout::BorderLineStyle::Dotted);
        if (uniform_dash &&
            paint_rounded_centerline_stroke(out, br, radii, bt, ct,
                                            st_t == layout::BorderLineStyle::Dotted)) {
            // Centerline dash or dot. A solid stroke here would fill the gaps.
        } else if (widths_equal && colors_equal && styles_solid && bt > 0.5f &&
                   !color_is_transparent(ct)) {
            out.stroke_rect(br, ct, bt, radii);
        } else if (can_punch_pad && colors_equal && !color_is_transparent(ct)) {
            out.fill_rect(br, ct, radii);
            out.fill_rect(pad, bg, pad_radii);
        } else if (can_punch_pad) {
            auto fill_side = [&](const Color& c, float width, Rect clip) {
                if (width <= 0.5f || color_is_transparent(c) || clip.w <= 0.f || clip.h <= 0.f) {
                    return;
                }
                out.push_clip(clip);
                out.fill_rect(br, c, radii);
                out.pop_clip();
            };
            fill_side(ct, bt, Rect{br.x, br.y, br.w, side_clip_h()});
            fill_side(cr, brw, Rect{br.x + br.w - side_clip_r(), br.y, side_clip_r(), br.h});
            fill_side(cb, bb, Rect{br.x, br.y + br.h - side_clip_b(), br.w, side_clip_b()});
            fill_side(cl, bl, Rect{br.x, br.y, side_clip_l(), br.h});
            out.fill_rect(pad, bg, pad_radii);
        } else if (can_ring && colors_equal && !color_is_transparent(ct)) {
            // Transparent / image / padding-clip backgrounds: true ring (no pad overpaint).
            out.stroke_ring(br, radii, pad, pad_radii, ct);
        } else if (widths_equal && colors_equal && bt > 0.5f && !color_is_transparent(ct) &&
                   st_t == st_r && st_t == st_b && st_t == st_l &&
                   (st_t == layout::BorderLineStyle::Inset ||
                    st_t == layout::BorderLineStyle::Outset)) {
            // Rounded inset/outset keeps Blink's two tones. A solid stroke of the specified
            // color paints a black push-button rim; the lit edge of outset #000 is 168 and the
            // shadow edge is 84 (Light / Light.Light of black).
            const bool lit_on_top = st_t == layout::BorderLineStyle::Outset;
            const Color lit = border_inset_outset_tone(ct, false);
            const Color shadow = border_inset_outset_tone(ct, true);
            const Color across = lit_on_top ? lit : shadow;
            const Color down = lit_on_top ? shadow : lit;
            auto stroke_side = [&](const Color& c, float width, Rect clip) {
                if (width <= 0.5f || color_is_transparent(c) || clip.w <= 0.f || clip.h <= 0.f) {
                    return;
                }
                out.push_clip(clip);
                out.stroke_rect(br, c, width, radii);
                out.pop_clip();
            };
            stroke_side(across, bt, Rect{br.x, br.y, br.w, side_clip_h()});
            stroke_side(down, brw, Rect{br.x + br.w - side_clip_r(), br.y, side_clip_r(), br.h});
            stroke_side(down, bb, Rect{br.x, br.y + br.h - side_clip_b(), br.w, side_clip_b()});
            stroke_side(across, bl, Rect{br.x, br.y, side_clip_l(), br.h});
        } else if (widths_equal && colors_equal && bt > 0.5f && !color_is_transparent(ct)) {
            // Double, groove, and ridge with a radius stay a solid stroke.
            out.stroke_rect(br, ct, bt, radii);
        } else if (can_ring) {
            // Mixed colours without an opaque pad restore: per-side clips over a ring keep arcs.
            auto ring_side = [&](const Color& c, float width, Rect clip) {
                if (width <= 0.5f || color_is_transparent(c) || clip.w <= 0.f || clip.h <= 0.f) {
                    return;
                }
                out.push_clip(clip);
                out.stroke_ring(br, radii, pad, pad_radii, c);
                out.pop_clip();
            };
            ring_side(ct, bt, Rect{br.x, br.y, br.w, side_clip_h()});
            ring_side(cr, brw, Rect{br.x + br.w - side_clip_r(), br.y, side_clip_r(), br.h});
            ring_side(cb, bb, Rect{br.x, br.y + br.h - side_clip_b(), br.w, side_clip_b()});
            ring_side(cl, bl, Rect{br.x, br.y, side_clip_l(), br.h});
        } else {
            auto stroke_side = [&](const Color& c, float width, Rect clip) {
                if (width <= 0.5f || color_is_transparent(c) || clip.w <= 0.f || clip.h <= 0.f) {
                    return;
                }
                out.push_clip(clip);
                out.stroke_rect(br, c, width, radii);
                out.pop_clip();
            };
            stroke_side(ct, bt, Rect{br.x, br.y, br.w, side_clip_h()});
            stroke_side(cr, brw, Rect{br.x + br.w - side_clip_r(), br.y, side_clip_r(), br.h});
            stroke_side(cb, bb, Rect{br.x, br.y + br.h - side_clip_b(), br.w, side_clip_b()});
            stroke_side(cl, bl, Rect{br.x, br.y, side_clip_l(), br.h});
        }
    } else if (!replaced_border && any_border && widths_equal && bt > 0.5f &&
               !color_is_transparent(ct) &&
               colors_equal && styles_solid) {
        if (box.paint_transformed && !radii.any()) {
            // Sharp Aff2 border: four local strips (GPU/soft FillRect xform).
            const Rect local{br.x - box.geo.world_x, br.y - box.geo.world_y, br.w, br.h};
            const float t = bt;
            auto xf = [&](Rect strip) {
                out.fill_rect_xform(strip, ct, CornerRadii{}, box.paint_a, box.paint_b, box.paint_c,
                                    box.paint_d, box.paint_e, box.paint_f);
            };
            xf(Rect{local.x, local.y, local.w, t});
            xf(Rect{local.x, local.y + local.h - t, local.w, t});
            xf(Rect{local.x, local.y, t, local.h});
            xf(Rect{local.x + local.w - t, local.y, t, local.h});
        } else {
            out.stroke_rect(br, ct, bt, radii);
        }
    } else if (!replaced_border && any_border) {
        const float mid_y = br.y + bt;
        const float mid_h = std::max(0.0f, br.h - bt - bb);
        // Dotted: paint every side full length so end-dot packing matches the border-box edge
        // length Blink strokes; opaque corner overdraw is acceptable.
        const bool dotted_box =
            st_t == layout::BorderLineStyle::Dotted && st_b == layout::BorderLineStyle::Dotted &&
            st_l == layout::BorderLineStyle::Dotted && st_r == layout::BorderLineStyle::Dotted;
        if (dotted_box) {
            paint_border_edge(out, box, true, Rect{br.x, br.y, br.w, bt}, ct, st_t, true, true);
            paint_border_edge(out, box, true, Rect{br.x, br.y + br.h - bb, br.w, bb}, cb, st_b, true, false);
            paint_border_edge(out, box, false, Rect{br.x, br.y, bl, br.h}, cl, st_l, true, true);
            paint_border_edge(out, box, false, Rect{br.x + br.w - brw, br.y, brw, br.h}, cr, st_r, true, false);
        } else {
            paint_border_edge(out, box, true, Rect{br.x, br.y, br.w, bt}, ct, st_t, border_dark_outer_for(st_t, true),
                       true);
            paint_border_edge(out, box, true, Rect{br.x, br.y + br.h - bb, br.w, bb}, cb, st_b,
                       border_dark_outer_for(st_b, false), false);
            paint_border_edge(out, box, false, Rect{br.x, mid_y, bl, mid_h}, cl, st_l, border_dark_outer_for(st_l, true),
                       true);
            paint_border_edge(out, box, false, Rect{br.x + br.w - brw, mid_y, brw, mid_h}, cr, st_r,
                       border_dark_outer_for(st_r, false), false);
        }
    }
}

}  // namespace painter_detail
}  // namespace paint
}  // namespace zui
