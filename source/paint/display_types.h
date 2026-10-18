#ifndef ZUI_PAINT_DISPLAY_TYPES_H
#define ZUI_PAINT_DISPLAY_TYPES_H

#include <cstdint>
#include <string>
#include <vector>

namespace zui {
namespace paint {

struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;

    static Color transparent() { return Color{0, 0, 0, 0}; }
};

struct Rect {
    float x = 0;
    float y = 0;
    float w = 0;
    float h = 0;
};

/// Per-corner border radii in CSS order: top-left, top-right, bottom-right, bottom-left.
/// Horizontal (x) and vertical (y) axes are stored separately for elliptical corners.
struct CornerRadii {
    float tl = 0;
    float tr = 0;
    float br = 0;
    float bl = 0;
    float tl_y = 0;
    float tr_y = 0;
    float br_y = 0;
    float bl_y = 0;

    static CornerRadii uniform(float r) { return CornerRadii{r, r, r, r, r, r, r, r}; }

    float max_component() const {
        float m = tl;
        if (tr > m) {
            m = tr;
        }
        if (br > m) {
            m = br;
        }
        if (bl > m) {
            m = bl;
        }
        if (tl_y > m) {
            m = tl_y;
        }
        if (tr_y > m) {
            m = tr_y;
        }
        if (br_y > m) {
            m = br_y;
        }
        if (bl_y > m) {
            m = bl_y;
        }
        return m;
    }

    bool any() const { return max_component() > 0.5f; }
};

inline Rect intersect_rect(Rect a, Rect b) {
    const float x0 = a.x > b.x ? a.x : b.x;
    const float y0 = a.y > b.y ? a.y : b.y;
    const float x1 = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
    const float y1 = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
    return Rect{x0, y0, x1 > x0 ? x1 - x0 : 0.0f, y1 > y0 ? y1 - y0 : 0.0f};
}

/// One colour stop of a gradient: its position along the gradient line in the 0..1 range, after
/// CSS Images 3 section 3.4.3 has filled in the stops that carry no position of their own.
struct GradientStop {
    float offset = 0;
    Color color;
};

enum class GradientKind : std::uint8_t { Linear, Radial, Conic };

/// A resolved gradient in device space.
/// Linear: `(x, y)` is the start of the gradient line, `(dx, dy)` a unit vector, `len` maps
/// offset 0→1. Radial: `(x, y)` is the centre, `(rx, ry)` the ending-shape radii. Conic:
/// `(x, y)` is the centre and `angle` is the `from` direction in radians, clockwise from up
/// (CSS Images 4). `repeating` wraps the stop list (CSS Images 3 / 4 repeating-* gradients).
struct Gradient {
    GradientKind kind = GradientKind::Linear;
    bool repeating = false;
    float x = 0;
    float y = 0;
    float dx = 0;
    float dy = 1;
    float len = 0;
    float rx = 0;
    float ry = 0;
    float angle = 0;
    std::vector<GradientStop> stops;
};

/// CSS Compositing 1 mix-blend-mode subset (separable modes).
enum class MixBlendMode : std::uint8_t {
    Normal = 0,
    Multiply,
    Screen,
    Overlay,
    Darken,
    Lighten,
    ColorDodge,
    ColorBurn,
    HardLight,
    SoftLight,
    Difference,
    Exclusion,
};

/// CSS Filter Effects `drop-shadow()`. `blur` is the CSS radius and the Gaussian standard
/// deviation, the same parameter `blur()` uses. The mask is the element's alpha, not its border
/// box, so a descendant's shape casts the shadow too.
struct DropShadow {
    float dx = 0;
    float dy = 0;
    float blur = 0;
    Color color{0, 0, 0, 255};
};

/// Offscreen effect layer: CSS `filter` + optional `mask-image` + `mix-blend-mode`.
struct FilterEffects {
    float blur = 0;        // px, `blur()` standard deviation
    float brightness = 1;  // 1 = 100%
    float opacity = 1;     // 1 = opaque
    MixBlendMode blend = MixBlendMode::Normal;
    /// Isolate this group even when `blend` is normal. `background-blend-mode` blends layers
    /// with the element's own background colour, then source-overs the result. A normal
    /// composite onto the page would blend a transparent colour with whatever is behind.
    bool isolate = false;
    /// `background-clip: text`. Fills in this group write straight RGB with alpha 0. A following
    /// `DrawTextMask` multiplies that RGB by glyph coverage and stores the coverage as alpha, so
    /// the layer shader can source-over a premultiplied glyph. `isolate` must also be set: the
    /// group has to be a real offscreen layer. OpenGL implements the mask. Vulkan ignores
    /// `DrawTextMask` and paints the fills as an ordinary border-box background.
    bool text_clip = false;
    bool has_mask = false;
    bool mask_is_gradient = false;
    Gradient mask_gradient;
    std::string mask_path;
    std::vector<DropShadow> shadows;
    /// Each color function is its own row-major 4×5 sRGB matrix, in source order. They are not
    /// multiplied together: contrast (and invert) clamp to 0–1 before the next function, and a
    /// product would let an intermediate channel exceed 1. `color_matrix_count` is 0 when every
    /// function is the identity, so `grayscale(0)` does not open a layer. A lone `brightness()`
    /// stays on the per-primitive multiply and does not occupy a slot.
    /// Empty unless a color function was stored. An inline 160-float array was value-initialized
    /// and copied with every `DisplayItem`, including fills and glyphs.
    static constexpr int kMaxFilterColorMatrices = 8;
    std::vector<float> color_matrix;
    int color_matrix_count = 0;
    bool has_color_matrix = false;
    bool filter_any() const {
        return blur > 0.5f || brightness < 0.999f || brightness > 1.001f || opacity < 0.999f ||
               has_color_matrix || !shadows.empty();
    }
    bool any() const {
        return filter_any() || has_mask || blend != MixBlendMode::Normal || isolate || text_clip;
    }
};

/// `acc = left * right` for a 4×5 color matrix (column vector, `right` applied first).
/// `apply_next_first` post-multiplies `next` (a child or an earlier function). Otherwise
/// `next` is applied after whatever `acc` already does.
inline void concat_color_matrix(float acc[20], const float next[20], bool apply_next_first) {
    const float* left = apply_next_first ? acc : next;
    const float* right = apply_next_first ? next : acc;
    float out[20];
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            float sum = 0.f;
            for (int k = 0; k < 4; ++k) {
                sum += left[row * 5 + k] * right[k * 5 + col];
            }
            out[row * 5 + col] = sum;
        }
        float bias = left[row * 5 + 4];
        for (int k = 0; k < 4; ++k) {
            bias += left[row * 5 + k] * right[k * 5 + 4];
        }
        out[row * 5 + 4] = bias;
    }
    for (int i = 0; i < 20; ++i) {
        acc[i] = out[i];
    }
}

}  // namespace paint
}  // namespace zui

#endif
