#ifndef ZUI_SOURCE_LAYOUT_INTERN_DETAIL_H
#define ZUI_SOURCE_LAYOUT_INTERN_DETAIL_H

// Longhand readers shared by intern_style.cpp (the intern/intern_style_as path) and
// read_style.cpp (the readers themselves).

#include "layout/intern_style.h"

#include "layout/cache/key_policy.h"

#include "layout/intern_grid.h"

#include "layout/style_table.h"

#include "css/values.h"
#include "text/font_chain.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace zui {
namespace layout {

css::ResolvedLength resolve_with_viewport(std::string_view text, float font_size,
                                          float root_font_size);
FlexPack parse_pack(const std::string& value);

namespace intern_detail {

bool is_math_length(const std::string* value);

struct ParsedLength {
    // False when the declaration is missing or is not a length: a keyword such as
    // inherit, a relative unit, or calc()/min() the style system has not resolved.
    // Callers then take their own initial value instead of the leading digits.
    bool valid = false;
    bool is_auto = false;
    bool is_percent = false;
    float value = 0;
};

bool ends_with(const std::string& value, char suffix);

ParsedLength parse_length(const html::Node* node, const std::string* value, bool missing_is_auto);

bool keeps_basis_text(const std::string& raw);

std::string bake_relative_units(std::string_view text, float font_size, float root_font_size,
                                float viewport_width, float viewport_height,                                const css::FontRelativeUnits& font_rel);

ParsedLength parse_length_kept(LayoutStyle& style, LengthProperty prop, const html::Node* node,
                               const std::string* value, bool missing_is_auto);

IntrinsicSize intrinsic_of(const std::string* value);

float px_of(const html::Node* node, const std::string* value, float fallback);

OverflowMode overflow_of(const std::string* value);

bool has_word(const std::string* value, const char* word);

bool contain_paint(const html::Node* node);

bool will_change_cb(const html::Node* node);

bool not_none(const html::Node* node, const char* key);

int parse_font_weight(const std::string* value);

bool parse_font_italic(const std::string* value);

bool parse_font_kerning_none(const std::string* value);

void copy_font(LayoutStyle& style, const html::Node* node);

std::string lang_of(const html::Node* node);
/// Drop the per-layout language cache. Node addresses are reused across documents.
void clear_lang_cache();

void apply_length(bool& is_auto, bool& is_percent, float& value, const ParsedLength& parsed);

void apply_edge(float& value, bool& is_percent, const ParsedLength& parsed);

void apply_margin(float& value, bool& is_percent, bool& is_auto, const ParsedLength& parsed);

bool has_unit(const std::string& value);

void read_line_height(LayoutStyle& style, const std::string* value);

void read_text_align(LayoutStyle& style, const std::string* value);

void read_vertical_align(LayoutStyle& style, const html::Node* node);

void read_inherited_inline(LayoutStyle& style, const html::Node* node);

void read_flex(LayoutStyle& style, const html::Node* node);

void read_gutter(LayoutStyle& style, const html::Node* node);

void read_scrollbar_width(LayoutStyle& style, const html::Node* node);

void apply_max(bool& is_none, bool& is_percent, float& value, LayoutStyle& style,
               LengthProperty prop, const html::Node* node, const std::string* raw);

bool table_row_display(const std::string& display, const std::string& tag);

bool table_column_display(const std::string& display, const std::string& tag);

std::vector<std::string> split_ws_simple(std::string_view raw);

TablePart table_part_of(const html::Node* node);

void read_table(LayoutStyle& style, const html::Node* node);

std::string lower_ascii(std::string_view text);

bool read_intrinsic_value(const std::string& token, float font_size, float root_font_size,                          float* out);

std::vector<std::string> intrinsic_tokens(std::string_view text);

void eat_intrinsic_auto(const std::vector<std::string>& tokens, std::size_t* index);

bool read_intrinsic_longhand(std::string_view text, float font_size, float root_font_size,                             float* out);

bool read_intrinsic_pair(std::string_view text, float font_size, float root_font_size,                         float* inline_px, float* block_px);

void read_intrinsic(LayoutStyle& style, const html::Node* node);

}  // namespace intern_detail
}  // namespace layout
}  // namespace zui

#endif
