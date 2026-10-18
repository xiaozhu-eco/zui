#include "ascii.h"
#include "paint/painter_internal.h"

#include "css/image_set.h"

#include "paint/display_list.h"

#include "log/log.h"
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
enum class BgRepeatAxis { Repeat, NoRepeat, Space, Round };

bool one_repeat_axis(std::string_view word, BgRepeatAxis& out) {
    if (word == "repeat") {
        out = BgRepeatAxis::Repeat;
        return true;
    }
    if (word == "no-repeat") {
        out = BgRepeatAxis::NoRepeat;
        return true;
    }
    if (word == "space") {
        out = BgRepeatAxis::Space;
        return true;
    }
    if (word == "round") {
        out = BgRepeatAxis::Round;
        return true;
    }
    return false;
}

// One keyword (`space`) or two (`round space`). `repeat-x` / `repeat-y` are not space or round.
bool parse_bg_repeat_axes(const std::string& repeat_l, BgRepeatAxis& x, BgRepeatAxis& y) {
    std::string a;
    std::string b;
    std::string cur;
    for (char ch : repeat_l) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            if (!cur.empty()) {
                if (a.empty()) {
                    a = cur;
                } else if (b.empty()) {
                    b = cur;
                } else {
                    return false;
                }
                cur.clear();
            }
            continue;
        }
        cur.push_back(ch);
    }
    if (!cur.empty()) {
        if (a.empty()) {
            a = cur;
        } else if (b.empty()) {
            b = cur;
        } else {
            return false;
        }
    }
    if (a.empty()) {
        return false;
    }
    if (a == "repeat-x" || a == "repeat-y") {
        return false;
    }
    if (!one_repeat_axis(a, x)) {
        return false;
    }
    if (b.empty()) {
        y = x;
        return true;
    }
    return one_repeat_axis(b, y);
}

bool axis_is_spaced(BgRepeatAxis axis) {
    return axis == BgRepeatAxis::Space || axis == BgRepeatAxis::Round;
}

// Tile starts along one axis. `area0`/`area` is the background positioning area. `tile_pos` is
// the background-position of the sized tile and is used for `repeat`, `no-repeat`, `round`, and
// `space` when fewer than two copies fit. `round` rescales so a whole number of copies fill the
// positioning area, then repeats from that already-resolved position. The pinned browser
// resolves a percentage against the specified background-size and does not recompute it after
// the rescale. `space` with two or more copies pins the first and last to the edges and splits
// the leftover gap; background-position is ignored on that axis.
void repeat_axis_starts(BgRepeatAxis mode, float area0, float area, float tile_pos, float tile_size,
                        float clip0, float clip1, std::vector<float>& starts, float& drawn) {
    drawn = tile_size;
    if (!(tile_size > 0.f) || !(area > 0.f) || !(clip1 > clip0)) {
        return;
    }
    auto walk = [&](float origin, float step, float limit) {
        if (!(step > 0.f)) {
            return;
        }
        float x = origin;
        int guard = 0;
        while (x > clip0 && guard < 4096) {
            x -= step;
            ++guard;
        }
        guard = 0;
        for (; x < limit && guard < 4096; x += step, ++guard) {
            starts.push_back(x);
        }
    };
    if (mode == BgRepeatAxis::NoRepeat) {
        starts.push_back(tile_pos);
        return;
    }
    if (mode == BgRepeatAxis::Space) {
        const int n = static_cast<int>(std::floor(static_cast<double>(area) / tile_size));
        if (n < 2) {
            starts.push_back(tile_pos);
            return;
        }
        const float gap = (area - static_cast<float>(n) * tile_size) / static_cast<float>(n - 1);
        walk(area0, tile_size + gap, clip1);
        return;
    }
    if (mode == BgRepeatAxis::Round) {
        int n = static_cast<int>(std::lround(static_cast<double>(area) / tile_size));
        if (n < 1) {
            n = 1;
        }
        drawn = area / static_cast<float>(n);
        walk(tile_pos, drawn, clip1);
        return;
    }
    float x = tile_pos;
    int guard = 0;
    while (x > clip0 && guard < 4096) {
        x -= tile_size;
        ++guard;
    }
    guard = 0;
    while (x + tile_size <= clip0 && guard < 4096) {
        x += tile_size;
        ++guard;
    }
    walk(x, tile_size, clip1);
}

// CSS Backgrounds 3 `space` and `round`. The positioned size is `image_rect`. Gradients do not
// come here. A document that does not use these keywords keeps the `emit_bg_bitmap` path.
void paint_bg_space_round(DisplayList& out, const std::string& path, const Rect& image_rect,
                          const Rect& positioning, const Rect& clip, const CornerRadii& clip_radii,
                          const std::string& repeat_l) {
    BgRepeatAxis x_mode = BgRepeatAxis::Repeat;
    BgRepeatAxis y_mode = BgRepeatAxis::Repeat;
    if (!parse_bg_repeat_axes(repeat_l, x_mode, y_mode)) {
        emit_bg_bitmap(out, path, image_rect, clip, clip_radii, repeat_l);
        return;
    }
    std::vector<float> xs;
    std::vector<float> ys;
    float drawn_w = image_rect.w;
    float drawn_h = image_rect.h;
    repeat_axis_starts(x_mode, positioning.x, positioning.w, image_rect.x, image_rect.w, clip.x,
                       clip.x + clip.w, xs, drawn_w);
    repeat_axis_starts(y_mode, positioning.y, positioning.h, image_rect.y, image_rect.h, clip.y,
                       clip.y + clip.h, ys, drawn_h);
    if (xs.empty() || ys.empty() || !(drawn_w > 0.f) || !(drawn_h > 0.f)) {
        return;
    }
    out.push_clip(clip, clip_radii);
    for (float y : ys) {
        for (float x : xs) {
            out.draw_image(Rect{x, y, drawn_w, drawn_h}, path);
        }
    }
    out.pop_clip();
}

// `auto` / `cover` / `contain` keep the single fill of the painting area. An explicit size, or
// `space` / `round`, tiles the gradient at the background positioning size.
bool bg_size_is_explicit(const std::string& size_s) {
    if (size_s.empty()) {
        return false;
    }
    const std::string l = lower_ascii(size_s);
    return l != "auto" && l != "auto auto" && l != "cover" && l != "contain";
}

void paint_repeated_gradient(DisplayList& out, const std::string& image_s, const Rect& image_rect,
                             const Rect& positioning, const Rect& clip, const CornerRadii& clip_radii,
                             const std::string& repeat_l, float opacity, const layout::Box* color_box) {
    BgRepeatAxis x_mode = BgRepeatAxis::Repeat;
    BgRepeatAxis y_mode = BgRepeatAxis::Repeat;
    if (repeat_l == "repeat-x") {
        y_mode = BgRepeatAxis::NoRepeat;
    } else if (repeat_l == "repeat-y") {
        x_mode = BgRepeatAxis::NoRepeat;
    } else if (!parse_bg_repeat_axes(repeat_l, x_mode, y_mode)) {
        x_mode = BgRepeatAxis::Repeat;
        y_mode = BgRepeatAxis::Repeat;
    }
    std::vector<float> xs;
    std::vector<float> ys;
    float drawn_w = image_rect.w;
    float drawn_h = image_rect.h;
    repeat_axis_starts(x_mode, positioning.x, positioning.w, image_rect.x, image_rect.w, clip.x,
                       clip.x + clip.w, xs, drawn_w);
    repeat_axis_starts(y_mode, positioning.y, positioning.h, image_rect.y, image_rect.h, clip.y,
                       clip.y + clip.h, ys, drawn_h);
    if (xs.empty() || ys.empty() || !(drawn_w > 0.f) || !(drawn_h > 0.f)) {
        return;
    }
    out.push_clip(clip, clip_radii);
    for (float y : ys) {
        for (float x : xs) {
            const Rect tile{x, y, drawn_w, drawn_h};
            Gradient gradient;
            if (!gradient_image_for_box(image_s, tile, gradient, color_box)) {
                continue;
            }
            for (GradientStop& stop : gradient.stops) {
                stop.color = apply_opacity(stop.color, opacity);
            }
            out.fill_gradient(tile, gradient, CornerRadii{});
        }
    }
    out.pop_clip();
}

void paint_one_background_image(DisplayList& out, std::string_view image, std::string_view position,
                                std::string_view size, std::string_view repeat, const Rect& origin,
                                const Rect& clip, const CornerRadii& clip_radii, float opacity,
                                const std::string& resource_base, const layout::Box* color_box) {
    image = trim_sv_local(image);
    if (image.empty() || lower_ascii(image) == "none" || clip.w <= 0.f || clip.h <= 0.f) {
        return;
    }
    std::string image_s(image);
    // CSS Images 4. The difftest browser pins deviceScaleFactor to 1, and the host pixel ratio is
    // not on this path. A 1x option wins over 2x; a lone 2x option is still painted.
    if (zui::css::is_image_set(image_s)) {
        image_s = zui::css::select_image_set(image_s, 1.f);
        if (image_s.empty()) {
            return;
        }
    }
    const std::string repeat_l = lower_ascii(trim_sv_local(repeat).empty() ? std::string_view("repeat")
                                                                           : trim_sv_local(repeat));
    const bool no_repeat = repeat_l == "no-repeat";
    const std::string pos_s(trim_sv_local(position));
    const std::string size_s(trim_sv_local(size));
    const std::string* pos_v = pos_s.empty() ? nullptr : &pos_s;
    const std::string* size_v = size_s.empty() ? nullptr : &size_s;

    if (is_paintable_gradient_image(image_s)) {
        BgRepeatAxis repeat_x = BgRepeatAxis::Repeat;
        BgRepeatAxis repeat_y = BgRepeatAxis::Repeat;
        const bool spaced = parse_bg_repeat_axes(repeat_l, repeat_x, repeat_y) &&
                            (axis_is_spaced(repeat_x) || axis_is_spaced(repeat_y));
        // A sized gradient is one image of that size, then repeated. Omitting the size keeps the
        // single fill: the default image is the positioning area, and existing goldens match that.
        if (!no_repeat && (spaced || bg_size_is_explicit(size_s))) {
            Rect image_rect;
            if (!resolve_bg_image_rect(origin, pos_v, size_v, image_rect)) {
                return;
            }
            paint_repeated_gradient(out, image_s, image_rect, origin, clip, clip_radii, repeat_l,
                                    opacity, color_box);
            return;
        }
        Rect paint_area = clip;
        CornerRadii paint_radii = clip_radii;
        if (no_repeat) {
            Rect image_rect;
            if (!resolve_bg_image_rect(origin, pos_v, size_v, image_rect)) {
                return;
            }
            paint_area = intersect_rect(image_rect, clip);
            paint_radii = CornerRadii{};
        }
        if (paint_area.w <= 0.f || paint_area.h <= 0.f) {
            return;
        }
        Gradient gradient;
        if (!gradient_image_for_box(image_s, paint_area, gradient, color_box)) {
            return;
        }
        for (GradientStop& stop : gradient.stops) {
            stop.color = apply_opacity(stop.color, opacity);
        }
        out.fill_gradient(paint_area, gradient, paint_radii);
        return;
    }

    if (!is_paintable_bg_url(image_s)) {
        return;
    }
    std::string url_path;
    if (!parse_bg_url(image_s, url_path)) {
        return;
    }
    const std::string resolved = resolve_paint_resource(url_path, resource_base);
    const auto decodes_before = image::decode_count();
    const image::Bitmap* bitmap = image::load(resolved);
    if (image::decode_count() != decodes_before) {
        perf::note_image_decode();
    }
    if (bitmap == nullptr || bitmap->rgba == nullptr || bitmap->width <= 0 || bitmap->height <= 0) {
        if (bitmap == nullptr) {
            ZUI_TRACE("[image] paint: background not decodable: {}", resolved);
        }
        return;
    }
    Rect image_rect;
    if (!resolve_bg_image_rect(origin, pos_v, size_v, image_rect, static_cast<float>(bitmap->width),
                               static_cast<float>(bitmap->height))) {
        return;
    }
    BgRepeatAxis repeat_x = BgRepeatAxis::Repeat;
    BgRepeatAxis repeat_y = BgRepeatAxis::Repeat;
    if (parse_bg_repeat_axes(repeat_l, repeat_x, repeat_y) &&
        (axis_is_spaced(repeat_x) || axis_is_spaced(repeat_y))) {
        paint_bg_space_round(out, resolved, image_rect, origin, clip, clip_radii, repeat_l);
        return;
    }
    emit_bg_bitmap(out, resolved, image_rect, clip, clip_radii, repeat_l);
}

}  // namespace painter_detail
}  // namespace paint
}  // namespace zui
