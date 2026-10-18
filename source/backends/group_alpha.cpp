#include "backends/group_alpha.h"

#include <algorithm>
#include <cstdint>

namespace zui {
namespace backends {
namespace {

/// Multiplies an 8-bit channel by a group factor, rounding half up and clamping.
/// `255 * 0.5` is 128, `183 * 0.5` is 92, and `183 * 1.4` saturates at 255.
std::uint8_t scale_channel(std::uint8_t c, float factor) {
    const float v = static_cast<float>(c) * factor;
    if (v <= 0.f) {
        return 0;
    }
    if (v >= 255.f) {
        return 255;
    }
    return static_cast<std::uint8_t>(v + 0.5f);
}

void scale_color(paint::Color& color, float alpha, float brightness, bool scale_a, bool scale_b) {
    if (scale_a) {
        color.a = scale_channel(color.a, alpha);
    }
    if (scale_b) {
        color.r = scale_channel(color.r, brightness);
        color.g = scale_channel(color.g, brightness);
        color.b = scale_channel(color.b, brightness);
    }
}

std::uint8_t matrix_channel(float v) {
    if (v <= 0.f) {
        return 0;
    }
    if (v >= 1.f) {
        return 255;
    }
    return static_cast<std::uint8_t>(v * 255.f + 0.5f);
}

float clamp01(float v) {
    if (v < 0.f) {
        return 0.f;
    }
    if (v > 1.f) {
        return 1.f;
    }
    return v;
}

void apply_color_matrices(paint::Color& color, const float* mats, int count) {
    float r = static_cast<float>(color.r) / 255.f;
    float g = static_cast<float>(color.g) / 255.f;
    float b = static_cast<float>(color.b) / 255.f;
    float a = static_cast<float>(color.a) / 255.f;
    const int cap = paint::FilterEffects::kMaxFilterColorMatrices;
    if (count > cap) {
        count = cap;
    }
    for (int s = 0; s < count; ++s) {
        const float* m = mats + s * 20;
        const float nr = m[0] * r + m[1] * g + m[2] * b + m[3] * a + m[4];
        const float ng = m[5] * r + m[6] * g + m[7] * b + m[8] * a + m[9];
        const float nb = m[10] * r + m[11] * g + m[12] * b + m[13] * a + m[14];
        const float na = m[15] * r + m[16] * g + m[17] * b + m[18] * a + m[19];
        r = clamp01(nr);
        g = clamp01(ng);
        b = clamp01(nb);
        a = clamp01(na);
    }
    color.r = matrix_channel(r);
    color.g = matrix_channel(g);
    color.b = matrix_channel(b);
    color.a = matrix_channel(a);
}

bool group_needs_layer(const paint::FilterEffects& fx) {
    // `blur()` and `drop-shadow()` read the group's composited alpha. Folding them into each
    // primitive leaves the element sharp. A color matrix has a bias term (contrast, invert), so
    // it also has to run on the composited layer when the backend isolates groups. Brightness
    // still scales each primitive: it is a per-channel multiply and does not need a second pass
    // by itself.
    if (fx.blur > 0.5f || !fx.shadows.empty() || fx.has_color_matrix) {
        return true;
    }
    if (fx.opacity < 0.999f) {
        return true;
    }
    if (fx.blend != paint::MixBlendMode::Normal || fx.isolate) {
        return true;
    }
    return fx.has_mask && fx.mask_is_gradient;
}

}  // namespace

void submit_items(GroupCursor& cursor, const std::vector<paint::DisplayItem>& items,
                  const std::function<void(const paint::DisplayItem&)>& draw) {
    // CSS Compositing 1: `opacity < 1` (and `filter: opacity()`) form a group that composites
    // as one layer at that factor. `filter: blur()` and `drop-shadow()` do too. `filter:
    // brightness()` multiplies the group's RGB.
    // Without `isolate_groups` both factors are applied per primitive, which is exact when the
    // group does not overlap itself. Enclosing factors multiply. An unmatched Begin leaves the
    // factor in place for every later item; renderers must not walk the list themselves.
    // With `isolate_groups`, a group that needs a real layer is not premultiplied: `draw` sees
    // the markers and the backend composites the layer once. Brightness still scales primitives.
    for (const paint::DisplayItem& raw : items) {
        if (raw.op == paint::DisplayOp::BeginFilter) {
            const bool layer = cursor.isolate_groups && group_needs_layer(raw.filter);
            cursor.stack.push_back(cursor.alpha);
            cursor.brightness_stack.push_back(cursor.brightness);
            cursor.layer_open.push_back(layer ? 1 : 0);
            {
                constexpr int kSlots = paint::FilterEffects::kMaxFilterColorMatrices * 20;
                std::array<float, kSlots> saved{};
                for (int i = 0; i < kSlots; ++i) {
                    saved[static_cast<std::size_t>(i)] = cursor.color_matrix[i];
                }
                cursor.color_matrix_stack.push_back(saved);
                cursor.color_matrix_count_stack.push_back(cursor.color_matrix_count);
                cursor.color_matrix_flag_stack.push_back(cursor.has_color_matrix ? 1 : 0);
                if (!layer && raw.filter.color_matrix_count > 0) {
                    // This filter runs before the ones already on the cursor (it is the inner one).
                    const int cap = paint::FilterEffects::kMaxFilterColorMatrices;
                    float next[kSlots] = {};
                    int n = 0;
                    const int inner = std::min(raw.filter.color_matrix_count, cap);
                    for (int s = 0; s < inner; ++s, ++n) {
                        for (int k = 0; k < 20; ++k) {
                            next[n * 20 + k] = raw.filter.color_matrix[s * 20 + k];
                        }
                    }
                    const int outer = cursor.color_matrix_count;
                    for (int s = 0; s < outer && n < cap; ++s, ++n) {
                        for (int k = 0; k < 20; ++k) {
                            next[n * 20 + k] = saved[static_cast<std::size_t>(s * 20 + k)];
                        }
                    }
                    for (int i = 0; i < kSlots; ++i) {
                        cursor.color_matrix[i] = next[i];
                    }
                    cursor.color_matrix_count = n;
                    cursor.has_color_matrix = n > 0;
                }
            }
            if (!layer) {
                cursor.alpha *= std::max(0.f, std::min(1.f, raw.filter.opacity));
            }
            cursor.brightness *= std::max(0.f, raw.filter.brightness);
            if (layer) {
                draw(raw);
            }
            continue;
        }
        if (raw.op == paint::DisplayOp::EndFilter) {
            const bool layer = !cursor.layer_open.empty() && cursor.layer_open.back() != 0;
            if (!cursor.layer_open.empty()) {
                cursor.layer_open.pop_back();
            }
            if (!cursor.stack.empty()) {
                cursor.alpha = cursor.stack.back();
                cursor.stack.pop_back();
            }
            if (!cursor.brightness_stack.empty()) {
                cursor.brightness = cursor.brightness_stack.back();
                cursor.brightness_stack.pop_back();
            }
            if (!cursor.color_matrix_stack.empty()) {
                constexpr int kSlots = paint::FilterEffects::kMaxFilterColorMatrices * 20;
                const std::array<float, kSlots>& saved = cursor.color_matrix_stack.back();
                for (int i = 0; i < kSlots; ++i) {
                    cursor.color_matrix[i] = saved[static_cast<std::size_t>(i)];
                }
                cursor.color_matrix_stack.pop_back();
            }
            if (!cursor.color_matrix_count_stack.empty()) {
                cursor.color_matrix_count = cursor.color_matrix_count_stack.back();
                cursor.color_matrix_count_stack.pop_back();
            }
            if (!cursor.color_matrix_flag_stack.empty()) {
                cursor.has_color_matrix = cursor.color_matrix_flag_stack.back() != 0;
                cursor.color_matrix_flag_stack.pop_back();
            }
            if (layer) {
                draw(raw);
            }
            continue;
        }
        const paint::DisplayItem* item = &raw;
        paint::DisplayItem scaled;
        const bool scale_a = cursor.alpha < 0.999f;
        const bool scale_b = cursor.brightness < 0.999f || cursor.brightness > 1.001f;
        const bool scale_m = cursor.has_color_matrix;
        if (scale_a || scale_b || scale_m) {
            scaled = raw;
            if (scale_m) {
                apply_color_matrices(scaled.color, cursor.color_matrix, cursor.color_matrix_count);
            }
            scale_color(scaled.color, cursor.alpha, cursor.brightness, scale_a, scale_b);
            for (paint::GradientStop& stop : scaled.gradient.stops) {
                if (scale_m) {
                    apply_color_matrices(stop.color, cursor.color_matrix, cursor.color_matrix_count);
                }
                scale_color(stop.color, cursor.alpha, cursor.brightness, scale_a, scale_b);
            }
            item = &scaled;
        }
        draw(*item);
    }
}

}  // namespace backends
}  // namespace zui
