#include "paint/painter_internal.h"

#include "paint/display_list.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace zui {
namespace paint {
namespace painter_detail {

Color border_tone(Color c, float mult) {
    const float tone_scale = std::nextafter(256.f, 0.f);
    auto q = [&](std::uint8_t x) {
        const int v = static_cast<int>((static_cast<float>(x) / 255.f) * mult * tone_scale);
        return static_cast<std::uint8_t>(std::clamp(v, 0, 255));
    };
    return Color{q(c.r), q(c.g), q(c.b), c.a};
}

Color border_darken(Color c) {
    if (c.r == 255 && c.g == 255 && c.b == 255) {
        return Color{171, 171, 171, c.a};
    }
    const float v = std::max({c.r, c.g, c.b}) / 255.f;
    const float mult = (v <= 0.f) ? 0.f : std::max(0.f, (v - 0.33f) / v);
    return border_tone(c, mult);
}

Color border_lighten(Color c) {
    if (c.r == 0 && c.g == 0 && c.b == 0) {
        return Color{0x54, 0x54, 0x54, c.a};
    }
    const float v = std::max({c.r, c.g, c.b}) / 255.f;
    if (v <= 0.f) {
        return Color{0x54, 0x54, 0x54, c.a};
    }
    const float mult = std::min(1.f, v + 0.33f) / v;
    return border_tone(c, mult);
}

float border_relative_luminance(Color c) {
    auto lin = [](float u) {
        return u <= 0.04045f ? u / 12.92f : std::pow((u + 0.055f) / 1.055f, 2.4f);
    };
    return 0.2126f * lin(c.r / 255.f) + 0.7152f * lin(c.g / 255.f) + 0.0722f * lin(c.b / 255.f);
}

Color border_inset_outset_tone(Color color, bool is_darken) {
    constexpr float k_dark_lum = 0.014443844f;
    constexpr float k_light_lum = 0.83077f;
    const float lum = border_relative_luminance(color);
    if (lum <= k_dark_lum) {
        const Color once = border_lighten(color);
        return is_darken ? once : border_lighten(once);
    }
    if (is_darken) {
        return border_darken(color);
    }
    return lum > k_light_lum ? color : border_lighten(color);
}

bool border_dark_outer_for(layout::BorderLineStyle style, bool is_top_or_left) {
    if (style == layout::BorderLineStyle::Inset || style == layout::BorderLineStyle::Groove) {
        return is_top_or_left;
    }
    if (style == layout::BorderLineStyle::Outset || style == layout::BorderLineStyle::Ridge) {
        return !is_top_or_left;
    }
    return true;
}

void paint_border_edge(DisplayList& out, const layout::Box& box, bool horizontal, Rect strip,
                       Color color, layout::BorderLineStyle style, bool dark_on_outer,
                       bool outer_at_low) {
    if (strip.w <= 0.f || strip.h <= 0.f || color_is_transparent(color) ||
        style == layout::BorderLineStyle::None || style == layout::BorderLineStyle::Hidden) {
        return;
    }
    if (style == layout::BorderLineStyle::Solid) {
        if (box.paint_transformed) {
            const Rect local{strip.x - box.geo.world_x, strip.y - box.geo.world_y, strip.w, strip.h};
            out.fill_rect_xform(local, color, CornerRadii{}, box.paint_a, box.paint_b, box.paint_c,
                                box.paint_d, box.paint_e, box.paint_f);
        } else {
            out.fill_rect(strip, color);
        }
        return;
    }
    if (style == layout::BorderLineStyle::Double) {
        const float thick = horizontal ? strip.h : strip.w;
        const float outer = std::max(1.f, std::floor((thick + 1.f) / 3.f));
        if (horizontal) {
            const float y0 = outer_at_low ? strip.y : strip.y + strip.h - outer;
            const float y1 = outer_at_low ? strip.y + strip.h - outer : strip.y;
            out.fill_rect(Rect{strip.x, y0, strip.w, outer}, color);
            out.fill_rect(Rect{strip.x, y1, strip.w, outer}, color);
        } else {
            const float x0 = outer_at_low ? strip.x : strip.x + strip.w - outer;
            const float x1 = outer_at_low ? strip.x + strip.w - outer : strip.x;
            out.fill_rect(Rect{x0, strip.y, outer, strip.h}, color);
            out.fill_rect(Rect{x1, strip.y, outer, strip.h}, color);
        }
        return;
    }
    if (style == layout::BorderLineStyle::Dashed) {
        const float thick = horizontal ? strip.h : strip.w;
        const float dash = std::max(thick * 2.f, 3.f);
        const float gap = std::max(thick, 2.f);
        if (horizontal) {
            for (float x = strip.x; x < strip.x + strip.w; x += dash + gap) {
                out.fill_rect(Rect{x, strip.y, std::min(dash, strip.x + strip.w - x), strip.h},
                              color);
            }
        } else {
            for (float y = strip.y; y < strip.y + strip.h; y += dash + gap) {
                out.fill_rect(Rect{strip.x, y, strip.w, std::min(dash, strip.y + strip.h - y)},
                              color);
            }
        }
        return;
    }
    if (style == layout::BorderLineStyle::Dotted) {
        const float thick = horizontal ? strip.h : strip.w;
        const float length = horizontal ? strip.w : strip.h;
        const float dot = std::min(thick, length);
        const int n =
            std::max(1, static_cast<int>(std::ceil(length / std::max(dot * 2.f, 2.f) - 1e-4f)));
        if (horizontal) {
            if (n == 1) {
                out.fill_rect(Rect{strip.x + (length - dot) * 0.5f, strip.y, dot, strip.h}, color,
                              dot * 0.5f);
            } else {
                const float gap = (length - n * dot) / static_cast<float>(n - 1);
                for (int i = 0; i < n; ++i) {
                    const float x = strip.x + (dot + gap) * static_cast<float>(i);
                    out.fill_rect(Rect{x, strip.y, dot, strip.h}, color, dot * 0.5f);
                }
            }
        } else if (n == 1) {
            out.fill_rect(Rect{strip.x, strip.y + (length - dot) * 0.5f, strip.w, dot}, color,
                          dot * 0.5f);
        } else {
            const float gap = (length - n * dot) / static_cast<float>(n - 1);
            for (int i = 0; i < n; ++i) {
                const float y = strip.y + (dot + gap) * static_cast<float>(i);
                out.fill_rect(Rect{strip.x, y, strip.w, dot}, color, dot * 0.5f);
            }
        }
        return;
    }
    const Color dark = border_inset_outset_tone(color, true);
    const Color light = border_inset_outset_tone(color, false);
    const Color outer_c = dark_on_outer ? dark : light;
    const Color inner_c = dark_on_outer ? light : dark;
    const float half = (horizontal ? strip.h : strip.w) * 0.5f;
    if (style == layout::BorderLineStyle::Inset || style == layout::BorderLineStyle::Outset) {
        out.fill_rect(strip, dark_on_outer ? dark : light);
        return;
    }
    if (horizontal) {
        const float y_outer = outer_at_low ? strip.y : strip.y + strip.h - half;
        const float y_inner = outer_at_low ? strip.y + half : strip.y;
        out.fill_rect(Rect{strip.x, y_outer, strip.w, half}, outer_c);
        out.fill_rect(Rect{strip.x, y_inner, strip.w, strip.h - half}, inner_c);
    } else {
        const float x_outer = outer_at_low ? strip.x : strip.x + strip.w - half;
        const float x_inner = outer_at_low ? strip.x + half : strip.x;
        out.fill_rect(Rect{x_outer, strip.y, half, strip.h}, outer_c);
        out.fill_rect(Rect{x_inner, strip.y, strip.w - half, strip.h}, inner_c);
    }
}

}  // namespace painter_detail
}  // namespace paint
}  // namespace zui
