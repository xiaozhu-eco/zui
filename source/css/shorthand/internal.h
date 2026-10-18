#ifndef ZUI_SOURCE_CSS_SHORTHAND_INTERNAL_H
#define ZUI_SOURCE_CSS_SHORTHAND_INTERNAL_H

// Internals shared by the shorthand expansion translation units:
//
//   serialize.cpp      lexbor component values -> CSS text, the longhand table
//   box.cpp            margin / padding / border / flex shorthands
//   tokenize.cpp       value tokenizers (tokens, slash, grid token runs)
//   background.cpp     `background` layers
//   grid_template.cpp  `grid-template` / `grid`
//   custom.cpp         the custom-property / deferred expansion
//   shorthand.cpp      the public entry points

#include <lexbor/css/css.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zui {
namespace css {
namespace shorthand_detail {

using Parts = std::vector<std::pair<std::string, std::string>>;

std::string ser_length(const lxb_css_value_length_percentage_t& lp);
std::string ser_border_width(const lxb_css_value_length_type_t& w);
std::string ser_ident(lxb_css_value_type_t type, const char* fallback);
std::string ser_color(const lxb_css_value_color_t& c);
std::string ser_number(const lxb_css_value_number_type_t& n, const char* fallback);
std::string ser_basis(const lxb_css_property_flex_basis_t& basis);

void add(Parts& out, const char* name, std::string value);
const std::vector<const char*>* longhands_of(const std::string& name);
bool expand_wide_keyword(const std::string& name, const char* keyword, Parts& out);

void distribute4(const std::string* v, int n, std::string out[4]);
void expand_edges(const lxb_css_value_length_percentage_t* sides, const char* names[4],
                  Parts& out);
void expand_border_side(const char* side, const lxb_css_property_border_t& b, Parts& out);
void expand_border_all(const lxb_css_property_border_t& b, Parts& out);
void expand_flex(const lxb_css_property_flex_t& flex, Parts& out);
bool expand_flex_text(std::string_view value, Parts& out);

std::vector<std::string> tokens(std::string_view value);
std::string lower_copy(std::string s);
std::string trim_copy(std::string_view s);
const char* wide_keyword_of(std::string_view value);
std::vector<std::string> split_slash(std::string_view value);
std::vector<std::string> grid_tokens(std::string_view value);
std::vector<std::string> split_comma_top(std::string_view value);
std::vector<std::string> tokens_with_slash(std::string_view value);
/// `true` for a token that looks like a `<length-percentage>` (`12px`, `-3%`, `calc(...)`).
bool looks_like_length_pct(const std::string& l);

bool expand_background(std::string_view value, Parts& out);
bool expand_grid_template(bool with_auto, std::string_view value, Parts& out);
bool expand_custom(std::string name, std::string_view value, Parts& out);

}  // namespace shorthand_detail
}  // namespace css
}  // namespace zui

#endif
