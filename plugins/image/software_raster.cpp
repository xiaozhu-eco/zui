#include "image/svg/software_raster.h"

#include "image/driver.h"
#include "image/svg/parser/group.h"
#include "image/svg/parser/paint.h"
#include "image/svg/parser/shape.h"
#include "image/svg/parser/viewport.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace zui {
namespace image {
namespace svg {

namespace {

int clamp_byte(float value) {
    const int scaled = static_cast<int>(std::lround(value * 255.f));
    return std::max(0, std::min(255, scaled));
}

void paint_box(Raster& out, int x0, int y0, int x1, int y1, int r, int g, int b, int a) {
    if (a <= 0) {
        return;
    }
    x0 = std::max(0, x0);
    y0 = std::max(0, y0);
    x1 = std::min(out.width, x1);
    y1 = std::min(out.height, y1);
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    for (int y = y0; y < y1; ++y) {
        std::uint8_t* row = out.rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(out.width) * 4;
        for (int x = x0; x < x1; ++x) {
            std::uint8_t* px = row + static_cast<std::size_t>(x) * 4;
            if (a >= 255) {
                px[0] = static_cast<std::uint8_t>(r);
                px[1] = static_cast<std::uint8_t>(g);
                px[2] = static_cast<std::uint8_t>(b);
                px[3] = 255;
                continue;
            }
            const int src_a = a;
            const int dst_a = px[3];
            const int out_a = src_a + dst_a * (255 - src_a) / 255;
            if (out_a == 0) {
                continue;
            }
            px[0] = static_cast<std::uint8_t>((r * src_a + px[0] * dst_a * (255 - src_a) / 255) / out_a);
            px[1] = static_cast<std::uint8_t>((g * src_a + px[1] * dst_a * (255 - src_a) / 255) / out_a);
            px[2] = static_cast<std::uint8_t>((b * src_a + px[2] * dst_a * (255 - src_a) / 255) / out_a);
            px[3] = static_cast<std::uint8_t>(out_a);
        }
    }
}

void draw_shape(const Shape& shape, const Transform& xform, float opacity, Raster& out) {
    if (shape.path.curves.empty()) {
        return;
    }
    float min_x = 0.f;
    float min_y = 0.f;
    float max_x = 0.f;
    float max_y = 0.f;
    bool any = false;
    auto take = [&](float x, float y) {
        float ox = 0.f;
        float oy = 0.f;
        xform.apply(x, y, ox, oy);
        if (!any) {
            min_x = max_x = ox;
            min_y = max_y = oy;
            any = true;
            return;
        }
        min_x = std::min(min_x, ox);
        min_y = std::min(min_y, oy);
        max_x = std::max(max_x, ox);
        max_y = std::max(max_y, oy);
    };
    for (const Curve& curve : shape.path.curves) {
        take(curve.x0, curve.y0);
        take(curve.x1, curve.y1);
        take(curve.x2, curve.y2);
        take(curve.x3, curve.y3);
    }
    if (!any) {
        return;
    }
    if (max_x - min_x < 1.f) {
        max_x = min_x + 1.f;
    }
    if (max_y - min_y < 1.f) {
        max_y = min_y + 1.f;
    }
    const Paint* paint = shape.fill.kind == Paint::Kind::Color ? &shape.fill : nullptr;
    if (paint == nullptr && shape.stroke.kind == Paint::Kind::Color) {
        paint = &shape.stroke;
    }
    if (paint == nullptr || paint->color.none) {
        return;
    }
    const float alpha = paint->color.a * opacity * shape.opacity;
    paint_box(out, static_cast<int>(std::floor(min_x)), static_cast<int>(std::floor(min_y)),
              static_cast<int>(std::ceil(max_x)), static_cast<int>(std::ceil(max_y)),
              clamp_byte(paint->color.r), clamp_byte(paint->color.g), clamp_byte(paint->color.b),
              clamp_byte(alpha));
}

void draw_node(const Node* node, const Transform& parent, float opacity, Raster& out) {
    if (node == nullptr) {
        return;
    }
    const Transform local = parent.mul(node->transform);
    const float next_opacity = opacity * node->opacity;
    if (const auto* group = dynamic_cast<const Group*>(node)) {
        if (!group->painted) {
            return;
        }
        for (const auto& child : group->children) {
            draw_node(child.get(), local, next_opacity, out);
        }
        return;
    }
    if (const auto* shape = dynamic_cast<const Shape*>(node)) {
        draw_shape(*shape, local, next_opacity, out);
    }
}

}  // namespace

bool rasterize_software(const Document& document, Raster& out, std::string& error) {
    const int width = static_cast<int>(std::lround(document.width));
    const int height = static_cast<int>(std::lround(document.height));
    if (!dimensions_ok(width, height)) {
        error = "svg viewport";
        return false;
    }
    out.width = width;
    out.height = height;
    out.rgba.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 0);
    draw_node(document.root.get(), Viewport::user_to_pixel(document), 1.f, out);
    return true;
}

}  // namespace svg
}  // namespace image
}  // namespace zui
