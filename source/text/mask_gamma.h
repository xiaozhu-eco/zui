#ifndef ZUI_TEXT_MASK_GAMMA_H
#define ZUI_TEXT_MASK_GAMMA_H

#include <cmath>
#include <cstdint>

namespace zui {
namespace text {

/// Skia A8 preblend for Chromium on Windows: contrast 1.0 and the sRGB transfer
/// (`SK_GAMMA_CONTRAST`, `SK_GAMMA_SRGB`). `src_green` is the text color's green channel.
/// Skia guesses the backdrop as the inverse of that channel and stores one table, the green one.
/// The returned byte is the coverage a gamma-space src-over blend should use.
inline const std::uint8_t* glyph_coverage_lut(std::uint8_t src_green) {
    struct Tables {
        std::uint8_t value[256][256];

        static float srgb_to_linear(float u) {
            if (u <= 0.04045f) {
                return u / 12.92f;
            }
            return std::pow((u + 0.055f) / 1.055f, 2.4f);
        }
        static float linear_to_srgb(float u) {
            if (u <= 0.0031308f) {
                return u * 12.92f;
            }
            return 1.055f * std::pow(u, 1.f / 2.4f) - 0.055f;
        }
        // srca + (1 - srca) * contrast * srca. Contrast tapers with the guessed backdrop.
        static float apply_contrast(float srca, float contrast) {
            return srca + ((1.f - srca) * contrast * srca);
        }

        Tables() {
            constexpr float kContrast = 1.f;
            for (int src_i = 0; src_i < 256; ++src_i) {
                const float src = static_cast<float>(src_i) / 255.f;
                const float lin_src = srgb_to_linear(src);
                const float dst = 1.f - src;
                const float lin_dst = srgb_to_linear(dst);
                const float adjusted = kContrast * lin_dst;
                const bool near = std::fabs(src - dst) < (1.f / 256.f);
                for (int i = 0; i < 256; ++i) {
                    const float raw = static_cast<float>(i) / 255.f;
                    float srca = apply_contrast(raw, adjusted);
                    if (srca > 1.f) {
                        srca = 1.f;
                    }
                    if (srca < 0.f) {
                        srca = 0.f;
                    }
                    float result = srca;
                    if (!near) {
                        const float dsta = 1.f - srca;
                        float lin_out = lin_src * srca + dsta * lin_dst;
                        if (lin_out > 1.f) {
                            lin_out = 1.f;
                        }
                        if (lin_out < 0.f) {
                            lin_out = 0.f;
                        }
                        const float out = linear_to_srgb(lin_out);
                        result = (out - dst) / (src - dst);
                    }
                    int byte = static_cast<int>(std::lround(255.f * result));
                    if (byte < 0) {
                        byte = 0;
                    }
                    if (byte > 255) {
                        byte = 255;
                    }
                    value[src_i][i] = static_cast<std::uint8_t>(byte);
                }
            }
        }
    };
    static const Tables tables;
    return tables.value[src_green];
}

}  // namespace text
}  // namespace zui

#endif
