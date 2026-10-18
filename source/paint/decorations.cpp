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

using namespace painter_detail;

void Painter::paint_box_decorations(const layout::Box& box, DisplayList& out, const Rect& br,
                                    const CornerRadii& radii, float radius, float op, float dx,
                                    float dy, bool paint_self) const {
    (void)radius;
    Color bg = Color::transparent();
    if (box.node != nullptr && (box.node->used.flags & html::VisualUsed::kBg)) {
        bg = overlay_rgba(box.node->used.bg_r, box.node->used.bg_g, box.node->used.bg_b,
                          box.node->used.bg_a);
    } else if (const std::string* v = computed(box, "background-color")) {
        // `currentcolor` has to be resolved against the box's own colour: the reader draws its
        // plus glyph and cover spine as `background: currentColor` strips.
        bg = paint_color(box, v);
    }
    bg = apply_opacity(bg, op);
    const float bt = box.geo.border.top;
    const float brw = box.geo.border.right;
    const float bb = box.geo.border.bottom;
    const float bl = box.geo.border.left;
    // CSS 2.1 section 17.6.1: `empty-cells: hide` drops the background and the borders of a cell
    // that has no in-flow content. Comments and collapsed whitespace never reach the box tree, a
    // float or an out-of-flow child is not in flow, and an empty `<br>` or `<span>` is.
    bool empty_cell_hidden = false;
    if (box.style().table_part == layout::TablePart::Cell) {
        const std::string* empty_cells = computed(box, "empty-cells");
        empty_cell_hidden =
            empty_cells != nullptr && *empty_cells == "hide" && !cell_has_flow_content(box);
    }
    const html::VisualUsed* border_overlay = box.node != nullptr ? &box.node->used : nullptr;
    // The key has to be a literal here. A lambda parameter would not fold the slot index, and
    // the lambda itself is a call under /Ob0.
#define ZUI_BORDER_SIDE(out, key_literal, overlay_flag, rgba)                                      \
    do {                                                                                           \
        Color zui_c_{0, 0, 0, 255};                                                                \
        if (border_overlay != nullptr && (border_overlay->flags & (overlay_flag)) != 0) {         \
            zui_c_ = overlay_rgba((rgba)[0], (rgba)[1], (rgba)[2], (rgba)[3]);                    \
        } else if (const std::string* zui_v_ = computed(box, key_literal)) {                      \
            zui_c_ = paint_color(box, zui_v_);                                                     \
        } else if (const std::string* zui_v_ = computed(box, "color")) {                          \
            zui_c_ = paint_color(box, zui_v_);                                                     \
        }                                                                                          \
        (out) = apply_opacity(zui_c_, op);                                                        \
    } while (0)
    std::uint8_t top_rgba[4] = {0, 0, 0, 255};
    std::uint8_t right_rgba[4] = {0, 0, 0, 255};
    std::uint8_t bottom_rgba[4] = {0, 0, 0, 255};
    std::uint8_t left_rgba[4] = {0, 0, 0, 255};
    if (border_overlay != nullptr) {
        top_rgba[0] = border_overlay->border_top_r;
        top_rgba[1] = border_overlay->border_top_g;
        top_rgba[2] = border_overlay->border_top_b;
        top_rgba[3] = border_overlay->border_top_a;
        right_rgba[0] = border_overlay->border_right_r;
        right_rgba[1] = border_overlay->border_right_g;
        right_rgba[2] = border_overlay->border_right_b;
        right_rgba[3] = border_overlay->border_right_a;
        bottom_rgba[0] = border_overlay->border_bottom_r;
        bottom_rgba[1] = border_overlay->border_bottom_g;
        bottom_rgba[2] = border_overlay->border_bottom_b;
        bottom_rgba[3] = border_overlay->border_bottom_a;
        left_rgba[0] = border_overlay->border_left_r;
        left_rgba[1] = border_overlay->border_left_g;
        left_rgba[2] = border_overlay->border_left_b;
        left_rgba[3] = border_overlay->border_left_a;
    }
    Color ct;
    Color cr;
    Color cb;
    Color cl;
    ZUI_BORDER_SIDE(ct, "border-top-color", html::VisualUsed::kBorderTop, top_rgba);
    ZUI_BORDER_SIDE(cr, "border-right-color", html::VisualUsed::kBorderRight, right_rgba);
    ZUI_BORDER_SIDE(cb, "border-bottom-color", html::VisualUsed::kBorderBottom, bottom_rgba);
    ZUI_BORDER_SIDE(cl, "border-left-color", html::VisualUsed::kBorderLeft, left_rgba);
#undef ZUI_BORDER_SIDE
    const std::string* clip_v = computed(box, "background-clip");
    const std::string* origin_v = computed(box, "background-origin");
    const std::string* image_v = computed(box, "background-image");
    const std::string* pos_v = computed(box, "background-position");
    const std::string* size_v = computed(box, "background-size");
    const std::string* repeat_v = computed(box, "background-repeat");
    const std::string* attach_v = computed(box, "background-attachment");
    // CSS Backgrounds 3 §2.1: the first image is on top. The background colour is clipped to the
    // bottom-most image layer's background-clip. A missing image keeps the single-value clip.
    std::vector<BgPaintLayer> bg_layers =
        background_layers(image_v, pos_v, size_v, repeat_v, attach_v, clip_v, origin_v);
    bool has_paintable_bg = false;
    for (const BgPaintLayer& layer : bg_layers) {
        if (is_paintable_gradient_image(layer.image) || is_paintable_bg_url(layer.image)) {
            has_paintable_bg = true;
            break;
        }
    }
    if (has_paintable_bg) {
        apply_background_blend(bg_layers, computed(box, "background-blend-mode"));
    }
    const BgBoxKind clip_kind = bg_layers.empty() ? parse_bg_clip(clip_v) : bg_layers.back().clip;
    Rect bg_rect = bg_box_rect(box, dx, dy, clip_kind);
    CornerRadii bg_radii = bg_box_radii(box, radii, clip_kind);
    // CSS 2.1 section 17.4: the table background paints the grid box. The caption is a
    // separate box and stays transparent, so the panel shows through the caption text.
    if (box.style_ptr != nullptr && box.style().table_part == layout::TablePart::Table) {
        float top = br.y;
        float bot = br.y + br.h;
        bool caption = false;
        for (const layout::Box* child : box.children) {
            if (child == nullptr || child->style_ptr == nullptr ||
                child->style().table_part != layout::TablePart::Caption) {
                continue;
            }
            caption = true;
            const float y0 = child->geo.world_y + dy;
            const float y1 = y0 + child->geo.height;
            if (child->style().caption_below) {
                bot = std::min(bot, y0);
            } else {
                top = std::max(top, y1);
            }
        }
        if (caption) {
            bg_rect.y = top;
            bg_rect.h = std::max(0.f, bot - top);
            bg_radii = CornerRadii{};
        }
    }
    // CSS Backgrounds 3 §7.1: outer box-shadows paint under the border box (before background).
    // First listed shadow is on top → emit later layers first.
    std::vector<BoxShadowLayer> box_shadows;
    if (paint_self && !empty_cell_hidden) {
        if (const std::string* shadow_v = computed(box, "box-shadow")) {
            box_shadows = parse_box_shadows(*shadow_v, box);
        }
        for (std::size_t i = box_shadows.size(); i > 0; --i) {
            const BoxShadowLayer& shadow = box_shadows[i - 1];
            if (!shadow.inset) {
                emit_outer_box_shadow(out, br, radii, shadow, op);
            }
        }
    }
    const bool skip_canvas_bg =
        box.node != nullptr && canvas_bg_node_ != nullptr && box.node == canvas_bg_node_;
    // Blend inside the element's own backdrop. The colour is the bottom layer and is not
    // itself blended; each image then blends with that result. The group source-overs onto
    // the page, so a transparent colour does not pick up pixels behind the element.
    const bool blend_open = paint_self && !empty_cell_hidden && !skip_canvas_bg &&
                            has_paintable_bg && background_has_blend(bg_layers) &&
                            begin_background_isolation(out, br.w > 0.f && br.h > 0.f ? br : bg_rect);
    // `background-clip: text` is rare. Boxes that never name it return on the first clip kind.
    bool clip_mentions_text = clip_kind == BgBoxKind::Text;
    if (!clip_mentions_text) {
        for (const BgPaintLayer& layer : bg_layers) {
            if (layer.clip == BgBoxKind::Text) {
                clip_mentions_text = true;
                break;
            }
        }
    }
    bool text_clip_open = false;
    if (clip_mentions_text && paint_self && !empty_cell_hidden && !skip_canvas_bg &&
        !box.paint_transformed && op >= 0.999f && !radii.any() && !blend_open &&
        box.style().writing_mode == layout::WritingMode::HorizontalTb &&
        (color_is_transparent(bg) || bg.a == 255)) {
        bool saw_text = false;
        bool saw_other = false;
        if (!color_is_transparent(bg)) {
            if (clip_kind == BgBoxKind::Text) {
                saw_text = true;
            } else {
                saw_other = true;
            }
        }
        for (const BgPaintLayer& layer : bg_layers) {
            const bool grad = is_paintable_gradient_image(layer.image);
            const bool url = is_paintable_bg_url(layer.image);
            if (!grad && !url) {
                continue;
            }
            // A url layer, a non-text clip, or a second box clip stays on the border-box path.
            if (grad && layer.clip == BgBoxKind::Text) {
                saw_text = true;
            } else {
                saw_other = true;
            }
        }
        if (saw_text && !saw_other) {
            const Rect bounds = br.w > 0.f && br.h > 0.f ? br : bg_rect;
            if (bounds.w > 0.f && bounds.h > 0.f) {
                FilterEffects fx;
                fx.isolate = true;
                fx.text_clip = true;
                out.begin_filter(bounds, fx);
                text_clip_open = true;
            }
        }
    }
    if (paint_self && !empty_cell_hidden && !skip_canvas_bg && !color_is_transparent(bg) &&
        bg_rect.h > 0.f && bg_rect.w > 0.f) {
        if (box.paint_transformed) {
            // Local border-box fill, mapped through the paint Aff2 (rotate/scale).
            const Rect local{bg_rect.x - box.geo.world_x, bg_rect.y - box.geo.world_y, bg_rect.w,
                             bg_rect.h};
            // Transform-establishing boxes use BeginTransform local fill + Aff2 blit (true
            // subtree FBO path). Nested / inherited Aff2 keeps per-primitive fill_rect_xform.
            if (establishes_own_transform(box) && xform_layer_depth_ == 0) {
                out.begin_transform(local, box.paint_a, box.paint_b, box.paint_c, box.paint_d,
                                    box.paint_e, box.paint_f);
                out.fill_rect(local, bg, bg_radii);
                out.end_transform();
            } else {
                out.fill_rect_xform(local, bg, bg_radii, box.paint_a, box.paint_b, box.paint_c,
                                    box.paint_d, box.paint_e, box.paint_f);
            }
        } else {
            out.fill_rect(bg_rect, bg, bg_radii);
        }
    }
    // Images paint over the background colour, bottom layer first (CSS Backgrounds 3 §2.1).
    // `background-attachment: fixed` positions against the viewport unless this box (or an
    // ancestor) is transformed, in which case the spec treats it as `scroll`. `local` shifts
    // with this element's own scroll, the same way its contents move.
    if (paint_self && !empty_cell_hidden && !skip_canvas_bg && has_paintable_bg) {
        for (size_t i = bg_layers.size(); i > 0; --i) {
            const BgPaintLayer& layer = bg_layers[i - 1];
            Rect clip = layer.clip == clip_kind ? bg_rect : bg_box_rect(box, dx, dy, layer.clip);
            CornerRadii clip_radii =
                layer.clip == clip_kind ? bg_radii : bg_box_radii(box, radii, layer.clip);
            Rect origin = bg_box_rect(box, dx, dy, layer.origin);
            const std::string attachment = lower_ascii(layer.attachment);
            const bool fixed = attachment == "fixed" && !box.paint_transformed && frame_w_ > 0.f &&
                               frame_h_ > 0.f;
            if (fixed) {
                origin = Rect{0.f, 0.f, frame_w_, frame_h_};
            } else if (attachment == "local") {
                float shift_x = box.geo.scroll_x;
                float shift_y = box.geo.scroll_y;
                if (box.style_ptr != nullptr &&
                    box.style().writing_mode == layout::WritingMode::HorizontalTb &&
                    box.style().direction == layout::Direction::Rtl) {
                    const float view = std::max(0.f, box.geo.width - box.geo.border.horizontal());
                    const float extent = std::max(0.f, box.geo.scroll_width - view);
                    shift_x = box.geo.scroll_x + extent;
                }
                origin.x -= shift_x;
                origin.y -= shift_y;
            }
            const bool layer_blend =
                blend_open && layer.blend != MixBlendMode::Normal && clip.w > 0.f && clip.h > 0.f;
            if (layer_blend) {
                FilterEffects fx;
                fx.blend = layer.blend;
                out.begin_filter(clip, fx);
            }
            paint_one_background_image(out, layer.image, layer.position, layer.size, layer.repeat,
                                       origin, clip, clip_radii, op, resource_base_, &box);
            if (layer_blend) {
                out.end_filter();
            }
        }
    }
    if (text_clip_open) {
        emit_background_text_mask(out, box, dx, dy);
        out.end_filter();
    }
    if (blend_open) {
        out.end_filter();
    }
    // CSS Backgrounds 3 §7.1: inset shadows paint above the background and below the border.
    // First listed is on top → emit later layers first.
    if (paint_self && !empty_cell_hidden) {
        for (std::size_t i = box_shadows.size(); i > 0; --i) {
            const BoxShadowLayer& shadow = box_shadows[i - 1];
            if (shadow.inset) {
                emit_inset_box_shadow(out, br, radii, shadow, op);
            }
        }
    }

    BoxDecorationPaint deco;
    deco.br = br;
    deco.radii = radii;
    deco.bt = bt;
    deco.brw = brw;
    deco.bb = bb;
    deco.bl = bl;
    deco.ct = ct;
    deco.cr = cr;
    deco.cb = cb;
    deco.cl = cl;
    deco.bg = bg;
    deco.has_paintable_bg = has_paintable_bg;
    deco.clip_kind = clip_kind;
    deco.paint_self = paint_self;
    deco.empty_cell_hidden = empty_cell_hidden;
    deco.dx = dx;
    deco.dy = dy;
    deco.op = op;
    paint_box_border(out, box, deco, resource_base_);
    paint_box_outline(out, box, deco);
    paint_box_column_rule(out, box, deco);
}

}  // namespace paint
}  // namespace zui
