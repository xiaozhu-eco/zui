// CSS Backgrounds 3 §5 border-image. The stroke is replaced when the source paints.
// Lookup stops at `border-image-source` when the property is absent, so a document
// that never sets it pays one map probe per painted box and nothing else.

#include "paint/painter_internal.h"

#include "image/image.h"
#include "perf/perf_budget.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace zui {
namespace paint {
namespace painter_detail {
namespace {

enum class Repeat : std::uint8_t { Stretch, Repeat, Round, Space };

enum class SpecKind : std::uint8_t { None, Auto, Number, Px, Percent, Em };

struct Spec {
    SpecKind kind = SpecKind::None;
    float value = 0.f;
};

std::vector<std::string> split_ws(std::string_view value) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < value.size()) {
        while (i < value.size() &&
               std::isspace(static_cast<unsigned char>(value[i]))) {
            ++i;
        }
        if (i >= value.size()) {
            break;
        }
        const std::size_t start = i;
        while (i < value.size() &&
               !std::isspace(static_cast<unsigned char>(value[i]))) {
            ++i;
        }
        out.emplace_back(value.substr(start, i - start));
    }
    return out;
}

bool parse_spec(std::string_view tok, Spec& out) {
    tok = trim_sv_local(tok);
    if (tok.empty()) {
        return false;
    }
    const std::string lower = lower_ascii(tok);
    if (lower == "auto") {
        out.kind = SpecKind::Auto;
        out.value = 0.f;
        return true;
    }
    if (lower == "fill") {
        return false;
    }
    char* end = nullptr;
    const std::string tmp(lower);
    const float v = std::strtof(tmp.c_str(), &end);
    if (end == tmp.c_str()) {
        return false;
    }
    std::string_view unit(end);
    while (!unit.empty() && std::isspace(static_cast<unsigned char>(unit.front()))) {
        unit.remove_prefix(1);
    }
    if (v < 0.f) {
        return false;
    }
    if (unit.empty()) {
        out.kind = SpecKind::Number;
        out.value = v;
        return true;
    }
    if (unit == "px") {
        out.kind = SpecKind::Px;
        out.value = v;
        return true;
    }
    if (unit == "%") {
        out.kind = SpecKind::Percent;
        out.value = v;
        return true;
    }
    if (unit == "em" || unit == "rem") {
        out.kind = SpecKind::Em;
        out.value = v;
        return true;
    }
    return false;
}

bool parse_repeat_word(std::string_view tok, Repeat& out) {
    const std::string l = lower_ascii(trim_sv_local(tok));
    if (l == "stretch") {
        out = Repeat::Stretch;
        return true;
    }
    if (l == "repeat") {
        out = Repeat::Repeat;
        return true;
    }
    if (l == "round") {
        out = Repeat::Round;
        return true;
    }
    if (l == "space") {
        out = Repeat::Space;
        return true;
    }
    return false;
}

// Slice offsets are image pixels. A number past the image size is 100% (the spec).
float slice_px(const Spec& spec, float axis) {
    float px = 0.f;
    if (spec.kind == SpecKind::Percent) {
        px = axis * spec.value / 100.f;
    } else if (spec.kind == SpecKind::Number || spec.kind == SpecKind::Px) {
        px = spec.value;
    } else {
        return 0.f;
    }
    if (px < 0.f) {
        return 0.f;
    }
    if (axis > 0.f && px > axis) {
        return axis;
    }
    return px;
}

struct Span {
    float pos = 0.f;
    float size = 0.f;
};

void tiles_1d(float origin, float length, float tile, Repeat mode, std::vector<Span>& out) {
    out.clear();
    if (length <= 0.5f) {
        return;
    }
    if (mode == Repeat::Stretch || tile <= 0.5f) {
        out.push_back(Span{origin, length});
        return;
    }
    if (mode == Repeat::Round) {
        int n = static_cast<int>(std::floor(length / tile + 0.5f));
        if (n < 1) {
            n = 1;
        }
        const float sz = length / static_cast<float>(n);
        out.reserve(static_cast<std::size_t>(n));
        for (int i = 0; i < n && i < 256; ++i) {
            out.push_back(Span{origin + sz * static_cast<float>(i), sz});
        }
        return;
    }
    if (mode == Repeat::Space) {
        const int n = static_cast<int>(std::floor(length / tile + 1e-4f));
        if (n < 1) {
            return;
        }
        const float gap = (length - static_cast<float>(n) * tile) / static_cast<float>(n + 1);
        const int drawn = std::min(n, 256);
        for (int i = 0; i < drawn; ++i) {
            out.push_back(Span{origin + gap + (tile + gap) * static_cast<float>(i), tile});
        }
        return;
    }
    float start = origin + (length - tile) * 0.5f;
    int back = 0;
    while (start > origin && back < 4096) {
        start -= tile;
        ++back;
    }
    for (int i = 0; i < 256; ++i) {
        const float p = start + tile * static_cast<float>(i);
        if (p >= origin + length - 0.01f) {
            break;
        }
        out.push_back(Span{p, tile});
    }
}

bool remap_gradient(const Gradient& src, Rect from, Rect to, Gradient& out) {
    if (from.w <= 0.f || from.h <= 0.f || to.w <= 0.f || to.h <= 0.f) {
        return false;
    }
    const float sx = from.w / to.w;
    const float sy = from.h / to.h;
    if (!(sx > 0.f) || !(sy > 0.f)) {
        return false;
    }
    out = src;
    if (src.kind == GradientKind::Radial) {
        out.x = to.x + (src.x - from.x) / sx;
        out.y = to.y + (src.y - from.y) / sy;
        out.rx = src.rx / sx;
        out.ry = src.ry / sy;
        return out.rx > 0.f && out.ry > 0.f;
    }
    if (src.kind == GradientKind::Conic) {
        const float scale = std::max(sx, sy);
        if (std::fabs(sx - sy) > 0.002f * scale) {
            return false;
        }
        out.x = to.x + (src.x - from.x) / sx;
        out.y = to.y + (src.y - from.y) / sy;
        return true;
    }
    const float vx = sx * src.dx;
    const float vy = sy * src.dy;
    const float vlen = std::sqrt(vx * vx + vy * vy);
    if (vlen < 1e-6f || src.len <= 1e-6f) {
        const Color c = gradient_color_at(src, from.x, from.y);
        out.kind = GradientKind::Linear;
        out.repeating = false;
        out.stops = {GradientStop{0.f, c}, GradientStop{1.f, c}};
        out.x = to.x;
        out.y = to.y;
        out.dx = 1.f;
        out.dy = 0.f;
        out.len = std::max(to.w, 1.f);
        return true;
    }
    const float t0 = ((from.x - src.x) * src.dx + (from.y - src.y) * src.dy) / src.len;
    const float dux = vx / vlen;
    const float duy = vy / vlen;
    const float len = src.len / vlen;
    out.dx = dux;
    out.dy = duy;
    out.len = len;
    out.x = to.x - dux * t0 * len;
    out.y = to.y - duy * t0 * len;
    return true;
}

}  // namespace

bool paint_border_image(DisplayList& out, const layout::Box& box, const Rect& border_box,
                        float opacity, const std::string& resource_base) {
    const std::string* source_v = computed(box, "border-image-source");
    if (source_v == nullptr) {
        return false;
    }
    const std::string_view source = trim_sv_local(*source_v);
    if (source.empty() || lower_ascii(source) == "none") {
        return false;
    }

    const float border_w[4] = {box.geo.border.top, box.geo.border.right, box.geo.border.bottom,
                               box.geo.border.left};
    float em_px = 16.f;
    if (box.style_ptr != nullptr && box.style().font_size > 0.f) {
        em_px = box.style().font_size;
    }

    // Outset first: it grows the border image area, which percentages of width are relative to.
    float outset[4] = {};
    {
        float axis[4] = {};
        const std::string* prop = computed(box, "border-image-outset");
        const std::string text =
            prop != nullptr && !trim_sv_local(*prop).empty() ? *prop : std::string("0");
        const std::vector<std::string> parts = split_ws(text);
        Spec specs[4];
        int n = 0;
        for (const std::string& part : parts) {
            if (n >= 4 || !parse_spec(part, specs[n])) {
                return false;
            }
            if (specs[n].kind == SpecKind::Auto || specs[n].kind == SpecKind::Percent) {
                return false;
            }
            ++n;
        }
        if (n == 0) {
            return false;
        }
        float raw[4];
        Spec boxed[4];
        if (n == 1) {
            boxed[0] = boxed[1] = boxed[2] = boxed[3] = specs[0];
        } else if (n == 2) {
            boxed[0] = boxed[2] = specs[0];
            boxed[1] = boxed[3] = specs[1];
        } else if (n == 3) {
            boxed[0] = specs[0];
            boxed[1] = boxed[3] = specs[1];
            boxed[2] = specs[2];
        } else {
            boxed[0] = specs[0];
            boxed[1] = specs[1];
            boxed[2] = specs[2];
            boxed[3] = specs[3];
        }
        for (int i = 0; i < 4; ++i) {
            if (boxed[i].kind == SpecKind::Number) {
                raw[i] = boxed[i].value * border_w[i];
            } else if (boxed[i].kind == SpecKind::Em) {
                raw[i] = boxed[i].value * em_px;
            } else {
                raw[i] = boxed[i].value;
            }
            outset[i] = std::max(0.f, raw[i]);
        }
    }

    const Rect area{border_box.x - outset[3], border_box.y - outset[0],
                    border_box.w + outset[3] + outset[1], border_box.h + outset[0] + outset[2]};
    if (area.w <= 0.5f || area.h <= 0.5f) {
        return false;
    }

    const bool gradient_source = is_paintable_gradient_image(source);
    std::string image_path;
    float img_w = 0.f;
    float img_h = 0.f;
    Gradient gradient;
    if (gradient_source) {
        // A gradient has no natural size. Concrete size is the border image area, and a
        // <number> slice is a CSS pixel of that size.
        img_w = area.w;
        img_h = area.h;
        if (!gradient_image_for_box(source, area, gradient, &box)) {
            return false;
        }
    } else if (is_paintable_bg_url(source)) {
        std::string url_path;
        if (!parse_bg_url(source, url_path)) {
            return false;
        }
        image_path = resolve_paint_resource(url_path, resource_base);
        int iw = 0;
        int ih = 0;
        const auto decodes_before = image::decode_count();
        const bool sized = image::intrinsic_size(image_path, iw, ih);
        if (image::decode_count() != decodes_before) {
            perf::note_image_decode();
        }
        if (!sized || iw <= 0 || ih <= 0) {
            return false;
        }
        img_w = static_cast<float>(iw);
        img_h = static_cast<float>(ih);
    } else {
        return false;
    }

    const std::string* slice_prop = computed(box, "border-image-slice");
    bool fill = false;
    std::vector<std::string> slice_tokens;
    {
        const std::string text = slice_prop != nullptr && !trim_sv_local(*slice_prop).empty()
                                     ? *slice_prop
                                     : std::string("100%");
        for (const std::string& part : split_ws(text)) {
            if (lower_ascii(part) == "fill") {
                fill = true;
                continue;
            }
            slice_tokens.push_back(part);
        }
    }
    if (slice_tokens.empty() || slice_tokens.size() > 4) {
        return false;
    }
    Spec slice_specs[4];
    for (std::size_t i = 0; i < slice_tokens.size(); ++i) {
        if (!parse_spec(slice_tokens[i], slice_specs[i])) {
            return false;
        }
        if (slice_specs[i].kind != SpecKind::Number && slice_specs[i].kind != SpecKind::Percent &&
            slice_specs[i].kind != SpecKind::Px) {
            return false;
        }
    }
    // Per-side specs via the 1-4 distribution, then resolved against the image axis.
    Spec slice_box[4];
    const int sn = static_cast<int>(slice_tokens.size());
    if (sn == 1) {
        slice_box[0] = slice_box[1] = slice_box[2] = slice_box[3] = slice_specs[0];
    } else if (sn == 2) {
        slice_box[0] = slice_box[2] = slice_specs[0];
        slice_box[1] = slice_box[3] = slice_specs[1];
    } else if (sn == 3) {
        slice_box[0] = slice_specs[0];
        slice_box[1] = slice_box[3] = slice_specs[1];
        slice_box[2] = slice_specs[2];
    } else {
        slice_box[0] = slice_specs[0];
        slice_box[1] = slice_specs[1];
        slice_box[2] = slice_specs[2];
        slice_box[3] = slice_specs[3];
    }
    // Top/bottom percentages use the image height; left/right use the width.
    const float slice_axis[4] = {img_h, img_w, img_h, img_w};
    float cut[4];
    for (int i = 0; i < 4; ++i) {
        cut[i] = slice_px(slice_box[i], slice_axis[i]);
    }
    const float cut_t = cut[0];
    const float cut_r = cut[1];
    const float cut_b = cut[2];
    const float cut_l = cut[3];

    float width[4];
    {
        float axis[4] = {area.h, area.w, area.h, area.w};
        const std::string* prop = computed(box, "border-image-width");
        // Parsed as px/number/percent/em/auto. Numbers are multiples of border-width, not px.
        const std::string text =
            prop != nullptr && !trim_sv_local(*prop).empty() ? *prop : std::string("1");
        const std::vector<std::string> parts = split_ws(text);
        Spec specs[4];
        int n = 0;
        for (const std::string& part : parts) {
            if (lower_ascii(part) == "fill") {
                continue;
            }
            if (n >= 4 || !parse_spec(part, specs[n])) {
                return false;
            }
            ++n;
        }
        if (n == 0) {
            return false;
        }
        Spec boxed[4];
        if (n == 1) {
            boxed[0] = boxed[1] = boxed[2] = boxed[3] = specs[0];
        } else if (n == 2) {
            boxed[0] = boxed[2] = specs[0];
            boxed[1] = boxed[3] = specs[1];
        } else if (n == 3) {
            boxed[0] = specs[0];
            boxed[1] = boxed[3] = specs[1];
            boxed[2] = specs[2];
        } else {
            boxed[0] = specs[0];
            boxed[1] = specs[1];
            boxed[2] = specs[2];
            boxed[3] = specs[3];
        }
        for (int i = 0; i < 4; ++i) {
            const Spec& spec = boxed[i];
            if (spec.kind == SpecKind::Auto) {
                // Natural size of the slice. Gradients are already in CSS px; raster image
                // pixels are 1 CSS px before the repeat step scales them.
                width[i] = cut[i];
            } else if (spec.kind == SpecKind::Number) {
                width[i] = spec.value * border_w[i];
            } else if (spec.kind == SpecKind::Percent) {
                width[i] = axis[i] * spec.value / 100.f;
            } else if (spec.kind == SpecKind::Em) {
                width[i] = spec.value * em_px;
            } else if (spec.kind == SpecKind::Px) {
                width[i] = spec.value;
            } else {
                return false;
            }
            width[i] = std::max(0.f, width[i]);
        }
    }
    {
        const float sum_w = width[3] + width[1];
        const float sum_h = width[0] + width[2];
        float fw = sum_w > 0.f ? area.w / sum_w : 1.f;
        float fh = sum_h > 0.f ? area.h / sum_h : 1.f;
        const float f = std::min(fw, fh);
        if (f < 1.f) {
            for (float& w : width) {
                w *= f;
            }
        }
    }
    const float wt = width[0];
    const float wr = width[1];
    const float wb = width[2];
    const float wl = width[3];

    Repeat repeat_h = Repeat::Stretch;
    Repeat repeat_v = Repeat::Stretch;
    {
        const std::string* prop = computed(box, "border-image-repeat");
        const std::string text =
            prop != nullptr && !trim_sv_local(*prop).empty() ? *prop : std::string("stretch");
        const std::vector<std::string> parts = split_ws(text);
        if (parts.empty() || parts.size() > 2) {
            return false;
        }
        if (!parse_repeat_word(parts[0], repeat_h)) {
            return false;
        }
        if (parts.size() == 2) {
            if (!parse_repeat_word(parts[1], repeat_v)) {
                return false;
            }
        } else {
            repeat_v = repeat_h;
        }
    }

    const float mid_src_w = std::max(0.f, img_w - cut_l - cut_r);
    const float mid_src_h = std::max(0.f, img_h - cut_t - cut_b);
    const float mid_x = area.x + wl;
    const float mid_y = area.y + wt;
    const float mid_w = std::max(0.f, area.w - wl - wr);
    const float mid_h = std::max(0.f, area.h - wt - wb);

    auto factor = [](float slice, float width_px, float other_slice, float other_width) {
        if (slice > 0.5f) {
            return width_px / slice;
        }
        if (other_slice > 0.5f) {
            return other_width / other_slice;
        }
        return 1.f;
    };
    const float width_factor = factor(cut_t, wt, cut_b, wb);
    const float height_factor = factor(cut_l, wl, cut_r, wr);

    const Color tint = apply_opacity(Color{255, 255, 255, 255}, opacity);
    const bool use_gradient = gradient_source;

    auto paint_piece = [&](float sx, float sy, float sw, float sh, Rect tile, Rect region) {
        if (sw <= 0.5f || sh <= 0.5f || tile.w <= 0.5f || tile.h <= 0.5f) {
            return;
        }
        const Rect vis = intersect_rect(tile, region);
        if (vis.w <= 0.5f || vis.h <= 0.5f) {
            return;
        }
        if (use_gradient) {
            const Rect from{area.x + sx, area.y + sy, sw, sh};
            Gradient mapped;
            if (!remap_gradient(gradient, from, tile, mapped)) {
                return;
            }
            for (GradientStop& stop : mapped.stops) {
                stop.color = apply_opacity(stop.color, opacity);
            }
            out.fill_gradient(vis, mapped);
            return;
        }
        // The image texture is sampled with a linear filter. A UV on the shared
        // texel edge mixes the neighboring slice into this one (the corner's red
        // into the edge's first pixel). Inset by half a texel so the sample stays
        // inside the slice; both ends move, so a stretch's midpoint does not.
        const float inset_u = 0.5f / img_w;
        const float inset_v = 0.5f / img_h;
        float u0 = sx / img_w;
        float v0 = sy / img_h;
        float u1 = (sx + sw) / img_w;
        float v1 = (sy + sh) / img_h;
        if (u1 - u0 > inset_u * 2.f) {
            u0 += inset_u;
            u1 -= inset_u;
        }
        if (v1 - v0 > inset_v * 2.f) {
            v0 += inset_v;
            v1 -= inset_v;
        }
        const float fx0 = (vis.x - tile.x) / tile.w;
        const float fy0 = (vis.y - tile.y) / tile.h;
        const float fx1 = (vis.x + vis.w - tile.x) / tile.w;
        const float fy1 = (vis.y + vis.h - tile.y) / tile.h;
        out.draw_image(vis, image_path, u0 + (u1 - u0) * fx0, v0 + (v1 - v0) * fy0,
                       u0 + (u1 - u0) * fx1, v0 + (v1 - v0) * fy1, tint);
    };

    auto paint_tiled = [&](float sx, float sy, float sw, float sh, float origin_x, float origin_y,
                           float length_x, float length_y, float tile_w, float tile_h, Repeat mode_x,
                           Repeat mode_y) {
        if (sw <= 0.5f || sh <= 0.5f || length_x <= 0.5f || length_y <= 0.5f) {
            return;
        }
        std::vector<Span> xs;
        std::vector<Span> ys;
        tiles_1d(origin_x, length_x, tile_w, mode_x, xs);
        tiles_1d(origin_y, length_y, tile_h, mode_y, ys);
        const Rect region{origin_x, origin_y, length_x, length_y};
        for (const Span& x : xs) {
            for (const Span& y : ys) {
                paint_piece(sx, sy, sw, sh, Rect{x.pos, y.pos, x.size, y.size}, region);
            }
        }
    };

    // Corners are scaled to their regions and are not tiled.
    paint_piece(0.f, 0.f, cut_l, cut_t, Rect{area.x, area.y, wl, wt},
                Rect{area.x, area.y, wl, wt});
    paint_piece(img_w - cut_r, 0.f, cut_r, cut_t, Rect{area.x + area.w - wr, area.y, wr, wt},
                Rect{area.x + area.w - wr, area.y, wr, wt});
    paint_piece(0.f, img_h - cut_b, cut_l, cut_b, Rect{area.x, area.y + area.h - wb, wl, wb},
                Rect{area.x, area.y + area.h - wb, wl, wb});
    paint_piece(img_w - cut_r, img_h - cut_b, cut_r, cut_b,
                Rect{area.x + area.w - wr, area.y + area.h - wb, wr, wb},
                Rect{area.x + area.w - wr, area.y + area.h - wb, wr, wb});

    // Edges. Step 1 scales the thickness to border-image-width and the other axis in proportion.
    // Step 2 tiles that result along the edge with the matching repeat keyword.
    if (cut_t > 0.5f && wt > 0.5f && mid_src_w > 0.5f && mid_w > 0.5f) {
        const float tile_w = mid_src_w * (wt / cut_t);
        paint_tiled(cut_l, 0.f, mid_src_w, cut_t, mid_x, area.y, mid_w, wt, tile_w, wt, repeat_h,
                    Repeat::Stretch);
    }
    if (cut_b > 0.5f && wb > 0.5f && mid_src_w > 0.5f && mid_w > 0.5f) {
        const float tile_w = mid_src_w * (wb / cut_b);
        paint_tiled(cut_l, img_h - cut_b, mid_src_w, cut_b, mid_x, area.y + area.h - wb, mid_w, wb,
                    tile_w, wb, repeat_h, Repeat::Stretch);
    }
    if (cut_l > 0.5f && wl > 0.5f && mid_src_h > 0.5f && mid_h > 0.5f) {
        const float tile_h = mid_src_h * (wl / cut_l);
        paint_tiled(0.f, cut_t, cut_l, mid_src_h, area.x, mid_y, wl, mid_h, wl, tile_h,
                    Repeat::Stretch, repeat_v);
    }
    if (cut_r > 0.5f && wr > 0.5f && mid_src_h > 0.5f && mid_h > 0.5f) {
        const float tile_h = mid_src_h * (wr / cut_r);
        paint_tiled(img_w - cut_r, cut_t, cut_r, mid_src_h, area.x + area.w - wr, mid_y, wr, mid_h,
                    wr, tile_h, Repeat::Stretch, repeat_v);
    }
    if (fill && mid_src_w > 0.5f && mid_src_h > 0.5f && mid_w > 0.5f && mid_h > 0.5f) {
        paint_tiled(cut_l, cut_t, mid_src_w, mid_src_h, mid_x, mid_y, mid_w, mid_h,
                    mid_src_w * width_factor, mid_src_h * height_factor, repeat_h, repeat_v);
    }
    return true;
}

}  // namespace painter_detail
}  // namespace paint
}  // namespace zui
