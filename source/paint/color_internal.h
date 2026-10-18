#ifndef ZUI_SOURCE_PAINT_COLOR_INTERNAL_H
#define ZUI_SOURCE_PAINT_COLOR_INTERNAL_H

#include "paint/display_list.h"
#include "paint/painter_internal.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace zui {
namespace paint {
namespace color_detail {

constexpr float kPi = 3.14159265358979323846f;

std::string_view trim(std::string_view s);
std::string lowered(std::string_view s);
std::vector<std::string_view> split_args(std::string_view s);
bool scan_number(std::string_view s, float& value, std::string_view& rest);
bool parse_angle(std::string_view s, float& deg);
bool parse_stop_position(std::string_view s, float line_len, float& fraction);
void tokenize_ws(std::string_view s, std::vector<std::string>& toks);
Color mix_premul(Color a, Color b, float f);
Color resolve_gradient_stop(std::string_view text, const layout::Box* color_box);

struct GradientHint {
    std::size_t after = 0;
    float position = 0.f;
};

bool known_interpolation_space(const std::string& name);
bool polar_interpolation_space(const std::string& name);
bool take_interpolation_method(std::vector<std::string>& toks, std::string& spec);
Color mix_in_method(Color a, Color b, float t, const std::string& spec);
void resample_in_method(std::vector<GradientStop>& stops, const std::vector<GradientHint>& hints,
                        const std::string& spec);
void expand_srgb_hints(std::vector<GradientStop>& stops, const std::vector<GradientHint>& hints);
bool parse_length_against(std::string_view s, float basis, float& px);
Color lerp_stops(const Gradient& g, float t);

}  // namespace color_detail
}  // namespace paint
}  // namespace zui

#endif
