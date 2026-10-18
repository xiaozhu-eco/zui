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
#include <vector>

namespace zui {
namespace paint {
namespace painter_detail {
/// CSS Backgrounds 3 §7.1: comma-separated layers. Each layer is
/// `[inset] offset-x offset-y [blur [spread]] [color]` (color may lead; `inset` anywhere).
std::vector<BoxShadowLayer> parse_box_shadows(std::string_view css, const layout::Box& box) {
    std::vector<BoxShadowLayer> layers;
    css = trim_sv_local(css);
    if (css.empty() || lower_ascii(std::string(css)) == "none") {
        return layers;
    }
    std::vector<std::string> layer_strs;
    {
        std::string cur;
        int depth = 0;
        for (char ch : css) {
            if (ch == '(') {
                ++depth;
                cur.push_back(ch);
                continue;
            }
            if (ch == ')') {
                depth = std::max(0, depth - 1);
                cur.push_back(ch);
                continue;
            }
            if (ch == ',' && depth == 0) {
                if (!cur.empty()) {
                    layer_strs.push_back(cur);
                    cur.clear();
                }
                continue;
            }
            cur.push_back(ch);
        }
        if (!cur.empty()) {
            layer_strs.push_back(cur);
        }
    }
    auto len_px = [](const std::string& tok, float& px) {
        bool bad = false;
        if (tok == "0") {
            px = 0.f;
            return true;
        }
        return parse_bg_length(tok, 0.f, px, bad) && !bad;
    };
    for (const std::string& layer_css : layer_strs) {
        std::vector<std::string> parts;
        std::string cur;
        int depth = 0;
        for (char ch : layer_css) {
            if (ch == '(') {
                ++depth;
                cur.push_back(ch);
                continue;
            }
            if (ch == ')') {
                depth = std::max(0, depth - 1);
                cur.push_back(ch);
                continue;
            }
            if (depth == 0 && std::isspace(static_cast<unsigned char>(ch))) {
                if (!cur.empty()) {
                    parts.push_back(cur);
                    cur.clear();
                }
                continue;
            }
            cur.push_back(ch);
        }
        if (!cur.empty()) {
            parts.push_back(cur);
        }
        bool inset = false;
        std::string color_tok;
        std::vector<std::string> lengths;
        for (const std::string& p : parts) {
            const std::string l = lower_ascii(p);
            if (l == "inset") {
                inset = true;
                continue;
            }
            bool unparseable = false;
            float dummy = 0.f;
            if (parse_bg_length(p, 0.f, dummy, unparseable) && !unparseable) {
                lengths.push_back(p);
                continue;
            }
            if (p == "0") {
                lengths.push_back(p);
                continue;
            }
            if (color_tok.empty()) {
                color_tok = p;
            }
        }
        if (lengths.size() < 2 || lengths.size() > 4) {
            continue;
        }
        float ox = 0.f;
        float oy = 0.f;
        float blur = 0.f;
        float spread = 0.f;
        if (!len_px(lengths[0], ox) || !len_px(lengths[1], oy)) {
            continue;
        }
        if (lengths.size() >= 3 && !len_px(lengths[2], blur)) {
            continue;
        }
        if (lengths.size() >= 4 && !len_px(lengths[3], spread)) {
            continue;
        }
        if (blur < 0.f) {
            blur = -blur;
        }
        BoxShadowLayer layer;
        layer.ox = ox;
        layer.oy = oy;
        layer.blur = blur;
        layer.spread = spread;
        layer.inset = inset;
        if (color_tok.empty()) {
            layer.color = paint_color(box, computed(box, "color"));
        } else {
            const std::string* fake = &color_tok;
            layer.color = paint_color(box, fake);
        }
        if (!color_is_transparent(layer.color)) {
            layers.push_back(layer);
        }
    }
    return layers;
}

void outer_shadow_outset(std::string_view css, const layout::Box& box, float& left, float& top,
                         float& right, float& bottom) {
    left = 0.f;
    top = 0.f;
    right = 0.f;
    bottom = 0.f;
    // Same slack the display-list cull uses for a blurred shadow, so a fringe this
    // visit keeps is not dropped again when the item is recorded.
    for (const BoxShadowLayer& shadow : parse_box_shadows(css, box)) {
        if (shadow.inset) {
            continue;
        }
        const float slack = shadow.blur * 1.5f;
        left = std::max(left, shadow.spread - shadow.ox + slack);
        right = std::max(right, shadow.spread + shadow.ox + slack);
        top = std::max(top, shadow.spread - shadow.oy + slack);
        bottom = std::max(bottom, shadow.spread + shadow.oy + slack);
    }
}

void emit_outer_box_shadow(DisplayList& out, const Rect& br, const CornerRadii& radii,
                           const BoxShadowLayer& shadow, float opacity) {
    Rect shape = br;
    shape.x += shadow.ox - shadow.spread;
    shape.y += shadow.oy - shadow.spread;
    shape.w += shadow.spread * 2.f;
    shape.h += shadow.spread * 2.f;
    CornerRadii shadow_radii = radii;
    if (shadow.spread != 0.f) {
        auto grow = [&](float v) { return v > 0.f ? std::max(0.f, v + shadow.spread) : 0.f; };
        shadow_radii.tl = grow(shadow_radii.tl);
        shadow_radii.tr = grow(shadow_radii.tr);
        shadow_radii.br = grow(shadow_radii.br);
        shadow_radii.bl = grow(shadow_radii.bl);
        shadow_radii.tl_y = grow(shadow_radii.tl_y);
        shadow_radii.tr_y = grow(shadow_radii.tr_y);
        shadow_radii.br_y = grow(shadow_radii.br_y);
        shadow_radii.bl_y = grow(shadow_radii.bl_y);
    }
    if (shape.w > 0.f && shape.h > 0.f) {
        out.shadow_rect(shape, apply_opacity(shadow.color, opacity), shadow_radii, shadow.blur);
    }
}

void emit_inset_box_shadow(DisplayList& out, const Rect& br, const CornerRadii& radii,
                           const BoxShadowLayer& shadow, float opacity) {
    // Inset: clip to border box; hole is offset then shrunk by spread (positive spread grows
    // the shadow inward).
    Rect hole = br;
    hole.x += shadow.ox + shadow.spread;
    hole.y += shadow.oy + shadow.spread;
    hole.w -= shadow.spread * 2.f;
    hole.h -= shadow.spread * 2.f;
    CornerRadii hole_radii = radii;
    if (shadow.spread != 0.f) {
        auto shrink = [&](float v) { return v > 0.f ? std::max(0.f, v - shadow.spread) : 0.f; };
        hole_radii.tl = shrink(hole_radii.tl);
        hole_radii.tr = shrink(hole_radii.tr);
        hole_radii.br = shrink(hole_radii.br);
        hole_radii.bl = shrink(hole_radii.bl);
        hole_radii.tl_y = shrink(hole_radii.tl_y);
        hole_radii.tr_y = shrink(hole_radii.tr_y);
        hole_radii.br_y = shrink(hole_radii.br_y);
        hole_radii.bl_y = shrink(hole_radii.bl_y);
    }
    out.shadow_rect(br, apply_opacity(shadow.color, opacity), radii, shadow.blur, true, hole,
                    hole_radii);
}

/// CSS Text Decoration / CSS2 `text-shadow`: comma-separated layers. Each layer is
/// `offset-x offset-y [blur] [color]` (color may lead). Empty / `none` → empty list.
std::vector<TextShadowLayer> parse_text_shadows(std::string_view css, const layout::Box& box) {
    std::vector<TextShadowLayer> layers;
    css = trim_sv_local(css);
    if (css.empty() || lower_ascii(std::string(css)) == "none") {
        return layers;
    }
    std::vector<std::string> layer_strs;
    {
        std::string cur;
        int depth = 0;
        for (char ch : css) {
            if (ch == '(') {
                ++depth;
                cur.push_back(ch);
                continue;
            }
            if (ch == ')') {
                depth = std::max(0, depth - 1);
                cur.push_back(ch);
                continue;
            }
            if (ch == ',' && depth == 0) {
                if (!cur.empty()) {
                    layer_strs.push_back(cur);
                    cur.clear();
                }
                continue;
            }
            cur.push_back(ch);
        }
        if (!cur.empty()) {
            layer_strs.push_back(cur);
        }
    }
    auto len_px = [](const std::string& tok, float& px) {
        bool bad = false;
        if (tok == "0") {
            px = 0.f;
            return true;
        }
        return parse_bg_length(tok, 0.f, px, bad) && !bad;
    };
    for (const std::string& layer_css : layer_strs) {
        std::vector<std::string> parts;
        std::string cur;
        int depth = 0;
        for (char ch : layer_css) {
            if (ch == '(') {
                ++depth;
                cur.push_back(ch);
                continue;
            }
            if (ch == ')') {
                depth = std::max(0, depth - 1);
                cur.push_back(ch);
                continue;
            }
            if (depth == 0 && std::isspace(static_cast<unsigned char>(ch))) {
                if (!cur.empty()) {
                    parts.push_back(cur);
                    cur.clear();
                }
                continue;
            }
            cur.push_back(ch);
        }
        if (!cur.empty()) {
            parts.push_back(cur);
        }
        std::string color_tok;
        std::vector<std::string> lengths;
        for (const std::string& p : parts) {
            bool unparseable = false;
            float dummy = 0.f;
            if (p == "0" || (parse_bg_length(p, 0.f, dummy, unparseable) && !unparseable)) {
                lengths.push_back(p);
                continue;
            }
            if (color_tok.empty()) {
                color_tok = p;
            }
        }
        if (lengths.size() < 2 || lengths.size() > 3) {
            continue;
        }
        TextShadowLayer layer;
        if (!len_px(lengths[0], layer.ox) || !len_px(lengths[1], layer.oy)) {
            continue;
        }
        if (lengths.size() >= 3) {
            if (!len_px(lengths[2], layer.blur)) {
                continue;
            }
            if (layer.blur < 0.f) {
                layer.blur = -layer.blur;
            }
        }
        if (color_tok.empty()) {
            layer.color = paint_color(box, computed(box, "color"));
        } else {
            const std::string* fake = &color_tok;
            layer.color = paint_color(box, fake);
        }
        if (!color_is_transparent(layer.color)) {
            layers.push_back(layer);
        }
    }
    return layers;
}

}  // namespace painter_detail
}  // namespace paint
}  // namespace zui
