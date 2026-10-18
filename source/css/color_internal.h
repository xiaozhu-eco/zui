#ifndef ZUI_SOURCE_CSS_COLOR_INTERNAL_H
#define ZUI_SOURCE_CSS_COLOR_INTERNAL_H

// Types and helpers shared by the css color translation units (parse, convert, mix).

#include "css/values.h"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace zui {
namespace css {

namespace values_detail {

int hex_digit(char c);
std::string_view trim_sv(std::string_view s);
bool equals_ci_sv(std::string_view a, std::string_view b);
std::uint8_t clamp_u8(int v);
std::uint8_t clamp_u8f(float v);
float hue2rgb(float p, float q, float t);
CssRgba hsl_to_rgba(float h_deg, float s, float l, float a);

}  // namespace values_detail

namespace modern_color {

constexpr float kPi = 3.14159265358979323846f;

float nan_v();
bool missing(float v);

enum class Space {
    Srgb,
    SrgbLinear,
    DisplayP3,
    Lab,
    Lch,
    Oklab,
    Oklch,
    Hsl,
    Hwb,
    XyzD65,
    XyzD50,
};

enum class HueMethod { Shorter, Longer, Increasing, Decreasing };

struct Abs {
    float c[3] = {};
    float a = 1.f;
    bool none[3] = {};
    Space space = Space::Srgb;
};

inline bool polar_space(Space space) {
    return space == Space::Lch || space == Space::Oklch || space == Space::Hsl || space == Space::Hwb;
}

inline int hue_index(Space space) { return space == Space::Hsl || space == Space::Hwb ? 0 : 2; }

bool starts_fn(std::string_view s, std::string_view name);
std::string_view fn_inner(std::string_view s, std::string_view name);
std::vector<std::string_view> split_top_commas(std::string_view s);

Abs from_srgb_bytes(const CssRgba& c);
Abs to_space(const Abs& src, Space dest);
void abs_to_lin_srgb(const Abs& color, float out[3]);
CssRgba encode_srgb(const Abs& color);

std::optional<Abs> parse_abs(std::string_view s);
std::optional<Abs> parse_fn_color(std::string_view s);
std::optional<Abs> mix_colors(std::string_view inner);

float hue_towards(float h1, float h2, HueMethod method);
bool parse_method(std::string_view text, Space& space, HueMethod& hue);
bool split_pct(std::string_view arg, std::string_view& color, bool& has_pct, float& pct);

}  // namespace modern_color
}  // namespace css
}  // namespace zui

#endif
