#include "layout/style_table.h"

#include "layout/cache/hash_mix.h"
#include "layout/intern_grid.h"
#include "layout/style_literal.h"
#include "css/values.h"
#include "html/document.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string_view>
#include <unordered_map>

namespace zui {
namespace layout {
namespace {
void mix_float(std::uint64_t& hash, float value);
void mix_string(std::uint64_t& hash, const std::string& value);
void mix_tracks(std::uint64_t& hash, const std::vector<TrackSize>& tracks);
void mix_names(std::uint64_t& hash, const std::vector<std::vector<std::string>>& names);
void mix_track(std::uint64_t& hash, const TrackSize& track) {
    hash_mix(hash, static_cast<std::uint64_t>(track.kind));
    mix_tracks(hash, track.repeat);
    mix_names(hash, track.repeat_names);
    mix_float(hash, track.length_px);
    hash_mix(hash, track.length_percent ? 1 : 0);
    mix_float(hash, track.fr);
    hash_mix(hash, static_cast<std::uint64_t>(track.min_kind));
    mix_float(hash, track.min_px);
    hash_mix(hash, track.min_percent ? 1 : 0);
    mix_float(hash, track.min_fr);
    hash_mix(hash, static_cast<std::uint64_t>(track.max_kind));
    mix_float(hash, track.max_px);
    hash_mix(hash, track.max_percent ? 1 : 0);
    mix_float(hash, track.max_fr);
}

void mix_tracks(std::uint64_t& hash, const std::vector<TrackSize>& tracks) {
    hash_mix(hash, tracks.size());
    for (const TrackSize& track : tracks) {
        mix_track(hash, track);
    }
}

void mix_grid_line(std::uint64_t& hash, const GridLine& line) {
    hash_mix(hash, line.is_auto ? 1 : 0);
    hash_mix(hash, line.is_span ? 1 : 0);
    mix_string(hash, line.name);
    hash_mix(hash, static_cast<std::uint64_t>(line.line));
    hash_mix(hash, static_cast<std::uint64_t>(line.span));
}

void mix_names(std::uint64_t& hash, const std::vector<std::vector<std::string>>& names) {
    hash_mix(hash, names.size());
    for (const std::vector<std::string>& line : names) {
        hash_mix(hash, line.size());
        for (const std::string& name : line) {
            mix_string(hash, name);
        }
    }
}

bool tracks_eq(const std::vector<TrackSize>& left, const std::vector<TrackSize>& right);

bool track_eq(const TrackSize& left, const TrackSize& right) {
    if (!tracks_eq(left.repeat, right.repeat)) {
        return false;
    }
    if (left.repeat_names != right.repeat_names) {
        return false;
    }
    return left.kind == right.kind && left.length_percent == right.length_percent &&
           left.length_px == right.length_px && left.fr == right.fr &&
           left.min_kind == right.min_kind && left.min_percent == right.min_percent &&
           left.min_px == right.min_px && left.min_fr == right.min_fr &&
           left.max_kind == right.max_kind && left.max_percent == right.max_percent &&
           left.max_px == right.max_px && left.max_fr == right.max_fr;
}

bool tracks_eq(const std::vector<TrackSize>& left, const std::vector<TrackSize>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (!track_eq(left[i], right[i])) {
            return false;
        }
    }
    return true;
}

bool grid_line_eq(const GridLine& left, const GridLine& right) {
    return left.is_auto == right.is_auto && left.is_span == right.is_span &&
           left.name == right.name && left.line == right.line && left.span == right.span;
}

void mix_float(std::uint64_t& hash, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    hash_mix(hash, bits);
}

void mix_string(std::uint64_t& hash, const std::string& value) {
    hash_mix(hash, value.size());
    for (unsigned char c : value) {
        hash_mix(hash, c);
    }
}

}  // namespace

StyleTable& StyleTable::process() {
    static StyleTable table;
    return table;
}

std::size_t StyleTable::EntryHash::operator()(const LayoutStyle& style) const noexcept {
    std::uint64_t hash = 0;
    hash_mix(hash, static_cast<std::uint64_t>(style.outer));
    hash_mix(hash, static_cast<std::uint64_t>(style.inner));
    hash_mix(hash, style.position_relative ? 1 : 0);
    hash_mix(hash, style.position_absolute ? 1 : 0);
    hash_mix(hash, style.position_fixed ? 1 : 0);
    hash_mix(hash, style.floating ? 1 : 0);
    hash_mix(hash, style.float_right ? 1 : 0);
    hash_mix(hash, style.list_style_outside ? 1 : 0);
    hash_mix(hash, static_cast<std::uint64_t>(style.clear_side));
    mix_float(hash, style.width_px);
    mix_float(hash, style.height_px);
    hash_mix(hash, style.width_auto ? 1 : 0);
    hash_mix(hash, static_cast<std::uint64_t>(style.width_intrinsic));
    hash_mix(hash, static_cast<std::uint64_t>(style.height_intrinsic));
    hash_mix(hash, style.height_auto ? 1 : 0);
    hash_mix(hash, style.width_percent ? 1 : 0);
    hash_mix(hash, style.height_percent ? 1 : 0);
    mix_float(hash, style.margin_top);
    mix_float(hash, style.margin_right);
    mix_float(hash, style.margin_bottom);
    mix_float(hash, style.margin_left);
    hash_mix(hash, style.margin_top_percent ? 1 : 0);
    hash_mix(hash, style.margin_right_percent ? 1 : 0);
    hash_mix(hash, style.margin_bottom_percent ? 1 : 0);
    hash_mix(hash, style.margin_left_percent ? 1 : 0);
    hash_mix(hash, style.margin_top_auto ? 1 : 0);
    hash_mix(hash, style.margin_right_auto ? 1 : 0);
    hash_mix(hash, style.margin_bottom_auto ? 1 : 0);
    hash_mix(hash, style.margin_left_auto ? 1 : 0);
    hash_mix(hash, style.min_width_auto ? 1 : 0);
    hash_mix(hash, style.min_width_percent ? 1 : 0);
    mix_float(hash, style.min_width_px);
    hash_mix(hash, style.min_height_auto ? 1 : 0);
    hash_mix(hash, style.min_height_percent ? 1 : 0);
    mix_float(hash, style.min_height_px);
    hash_mix(hash, style.max_width_none ? 1 : 0);
    hash_mix(hash, style.max_width_percent ? 1 : 0);
    mix_float(hash, style.max_width_px);
    hash_mix(hash, style.max_height_none ? 1 : 0);
    hash_mix(hash, style.max_height_percent ? 1 : 0);
    mix_float(hash, style.max_height_px);
    hash_mix(hash, style.flow_root ? 1 : 0);
    hash_mix(hash, style.border_box ? 1 : 0);
    hash_mix(hash, static_cast<std::uint64_t>(style.white_space));
    hash_mix(hash, static_cast<std::uint64_t>(style.word_break));
    hash_mix(hash, static_cast<std::uint64_t>(style.overflow_wrap));
    hash_mix(hash, static_cast<std::uint64_t>(style.vertical_align));
    hash_mix(hash, style.vertical_align_percent ? 1 : 0);
    mix_float(hash, style.vertical_align_px);
    hash_mix(hash, static_cast<std::uint64_t>(style.line_height_kind));
    mix_float(hash, style.line_height_value);
    hash_mix(hash, static_cast<std::uint64_t>(style.text_align));
    hash_mix(hash, static_cast<std::uint64_t>(style.text_transform));
    hash_mix(hash, static_cast<std::uint64_t>(style.text_overflow));
    hash_mix(hash, style.text_indent_percent ? 1 : 0);
    mix_float(hash, style.text_indent_px);
    mix_float(hash, style.letter_spacing);
    mix_float(hash, style.word_spacing);
    mix_float(hash, style.aspect_ratio);
    hash_mix(hash, static_cast<std::uint64_t>(style.scrollbar_gutter));
    hash_mix(hash, static_cast<std::uint64_t>(style.scrollbar_width));
    mix_float(hash, style.padding_top);
    mix_float(hash, style.padding_right);
    mix_float(hash, style.padding_bottom);
    mix_float(hash, style.padding_left);
    hash_mix(hash, style.padding_top_percent ? 1 : 0);
    hash_mix(hash, style.padding_right_percent ? 1 : 0);
    hash_mix(hash, style.padding_bottom_percent ? 1 : 0);
    hash_mix(hash, style.padding_left_percent ? 1 : 0);
    hash_mix(hash, style.math_lengths.size());
    for (const MathLength& kept : style.math_lengths) {
        hash_mix(hash, static_cast<std::uint64_t>(kept.prop));
        mix_string(hash, kept.text);
    }
    mix_float(hash, style.border_top);
    mix_float(hash, style.border_right);
    mix_float(hash, style.border_bottom);
    mix_float(hash, style.border_left);
    hash_mix(hash, static_cast<std::uint64_t>(style.border_top_style));
    hash_mix(hash, static_cast<std::uint64_t>(style.border_right_style));
    hash_mix(hash, static_cast<std::uint64_t>(style.border_bottom_style));
    hash_mix(hash, static_cast<std::uint64_t>(style.border_left_style));
    hash_mix(hash, static_cast<std::uint64_t>(style.outline_style));
    mix_float(hash, style.outline_width);
    mix_float(hash, style.outline_offset);
    mix_float(hash, style.border_top_left_radius);
    mix_float(hash, style.border_top_right_radius);
    mix_float(hash, style.border_bottom_right_radius);
    mix_float(hash, style.border_bottom_left_radius);
    hash_mix(hash, style.border_top_left_radius_percent ? 1 : 0);
    hash_mix(hash, style.border_top_right_radius_percent ? 1 : 0);
    hash_mix(hash, style.border_bottom_right_radius_percent ? 1 : 0);
    hash_mix(hash, style.border_bottom_left_radius_percent ? 1 : 0);
    mix_float(hash, style.border_top_left_radius_y);
    mix_float(hash, style.border_top_right_radius_y);
    mix_float(hash, style.border_bottom_right_radius_y);
    mix_float(hash, style.border_bottom_left_radius_y);
    hash_mix(hash, style.border_top_left_radius_y_percent ? 1 : 0);
    hash_mix(hash, style.border_top_right_radius_y_percent ? 1 : 0);
    hash_mix(hash, style.border_bottom_right_radius_y_percent ? 1 : 0);
    hash_mix(hash, style.border_bottom_left_radius_y_percent ? 1 : 0);
    mix_float(hash, style.border_radius);
    hash_mix(hash, static_cast<std::uint64_t>(style.overflow_x));
    hash_mix(hash, static_cast<std::uint64_t>(style.overflow_y));
    mix_float(hash, style.font_size);
    hash_mix(hash, static_cast<std::uint64_t>(style.font_weight));
    hash_mix(hash, style.font_italic ? 1 : 0);
    hash_mix(hash, style.font_kerning_none ? 1 : 0);
    mix_string(hash, style.font_family);
    mix_string(hash, style.lang);
    hash_mix(hash, static_cast<std::uint64_t>(style.writing_mode));
    hash_mix(hash, static_cast<std::uint64_t>(style.direction));
    hash_mix(hash, style.cb_for_absolute ? 1 : 0);
    hash_mix(hash, style.cb_for_fixed ? 1 : 0);
    hash_mix(hash, style.contain_paint ? 1 : 0);
    hash_mix(hash, style.contain_layout ? 1 : 0);
    hash_mix(hash, style.contain_inline_size ? 1 : 0);
    hash_mix(hash, style.contain_block_size ? 1 : 0);
    hash_mix(hash, style.content_visibility_hidden ? 1 : 0);
    hash_mix(hash, style.content_visibility_auto ? 1 : 0);
    mix_float(hash, style.contain_intrinsic_inline);
    mix_float(hash, style.contain_intrinsic_block);
    hash_mix(hash, style.visibility_collapse ? 1 : 0);
    hash_mix(hash, style.table_row ? 1 : 0);
    hash_mix(hash, static_cast<std::uint64_t>(style.table_part));
    hash_mix(hash, static_cast<std::uint64_t>(style.table_colspan));
    hash_mix(hash, static_cast<std::uint64_t>(style.table_rowspan));
    hash_mix(hash, static_cast<std::uint64_t>(style.table_span));
    hash_mix(hash, style.border_collapse ? 1 : 0);
    mix_float(hash, style.border_spacing_inline);
    mix_float(hash, style.border_spacing_block);
    hash_mix(hash, style.table_fixed ? 1 : 0);
    hash_mix(hash, style.caption_below ? 1 : 0);
    hash_mix(hash, style.table_column ? 1 : 0);
    hash_mix(hash, style.position_sticky ? 1 : 0);
    hash_mix(hash, style.top_auto ? 1 : 0);
    hash_mix(hash, style.top_percent ? 1 : 0);
    mix_float(hash, style.top_px);
    hash_mix(hash, style.left_auto ? 1 : 0);
    hash_mix(hash, style.left_percent ? 1 : 0);
    mix_float(hash, style.left_px);
    hash_mix(hash, style.right_auto ? 1 : 0);
    hash_mix(hash, style.right_percent ? 1 : 0);
    mix_float(hash, style.right_px);
    hash_mix(hash, style.bottom_auto ? 1 : 0);
    hash_mix(hash, style.bottom_percent ? 1 : 0);
    mix_float(hash, style.bottom_px);
    hash_mix(hash, static_cast<std::uint64_t>(style.flex_direction));
    hash_mix(hash, static_cast<std::uint64_t>(style.flex_wrap));
    mix_float(hash, style.flex_grow);
    mix_float(hash, style.flex_shrink);
    hash_mix(hash, style.flex_basis_auto ? 1 : 0);
    hash_mix(hash, style.flex_basis_percent ? 1 : 0);
    hash_mix(hash, style.flex_basis_content ? 1 : 0);
    mix_float(hash, style.flex_basis_px);
    mix_float(hash, style.column_gap);
    hash_mix(hash, static_cast<std::uint64_t>(style.column_count));
    hash_mix(hash, style.column_width_auto ? 1 : 0);
    mix_float(hash, style.column_width);
    hash_mix(hash, style.column_fill_auto ? 1 : 0);
    hash_mix(hash, style.column_span_all ? 1 : 0);
    mix_float(hash, style.row_gap);
    hash_mix(hash, style.column_gap_percent ? 1 : 0);
    hash_mix(hash, style.row_gap_percent ? 1 : 0);
    hash_mix(hash, static_cast<std::uint64_t>(style.flex_order));
    hash_mix(hash, static_cast<std::uint64_t>(style.align_items));
    hash_mix(hash, static_cast<std::uint64_t>(style.align_self));
    hash_mix(hash, static_cast<std::uint64_t>(style.justify_content));
    hash_mix(hash, static_cast<std::uint64_t>(style.align_content));
    hash_mix(hash, static_cast<std::uint64_t>(style.justify_items));
    hash_mix(hash, static_cast<std::uint64_t>(style.justify_self));
    mix_tracks(hash, style.grid_template_columns);
    mix_tracks(hash, style.grid_template_rows);
    mix_tracks(hash, style.grid_auto_columns);
    mix_tracks(hash, style.grid_auto_rows);
    mix_grid_line(hash, style.grid_column_start);
    mix_grid_line(hash, style.grid_column_end);
    mix_grid_line(hash, style.grid_row_start);
    mix_grid_line(hash, style.grid_row_end);
    mix_names(hash, style.grid_column_line_names);
    mix_names(hash, style.grid_row_line_names);
    mix_names(hash, style.grid_template_areas);
    hash_mix(hash, style.grid_auto_flow_column ? 1 : 0);
    hash_mix(hash, style.grid_auto_flow_dense ? 1 : 0);
    hash_mix(hash, static_cast<std::uint64_t>(style.container_type));
    hash_mix(hash, static_cast<std::uint64_t>(style.break_before));
    hash_mix(hash, static_cast<std::uint64_t>(style.break_after));
    hash_mix(hash, static_cast<std::uint64_t>(style.break_inside));
    hash_mix(hash, static_cast<std::uint64_t>(style.orphans));
    hash_mix(hash, static_cast<std::uint64_t>(style.widows));
    return static_cast<std::size_t>(hash);
}

bool StyleTable::EntryEq::operator()(const LayoutStyle& left, const LayoutStyle& right) const {
    return left.outer == right.outer && left.inner == right.inner &&
           left.position_relative == right.position_relative &&
           left.position_absolute == right.position_absolute &&
           left.position_fixed == right.position_fixed && left.floating == right.floating &&
           left.float_right == right.float_right &&
           left.list_style_outside == right.list_style_outside &&
           left.clear_side == right.clear_side &&
           left.width_px == right.width_px && left.height_px == right.height_px &&
           left.width_auto == right.width_auto && left.width_intrinsic == right.width_intrinsic &&
           left.height_intrinsic == right.height_intrinsic &&
           left.height_auto == right.height_auto && left.width_percent == right.width_percent &&
           left.height_percent == right.height_percent && left.margin_top == right.margin_top &&
           left.margin_right == right.margin_right && left.margin_bottom == right.margin_bottom &&
           left.margin_left == right.margin_left &&
           left.margin_top_percent == right.margin_top_percent &&
           left.margin_right_percent == right.margin_right_percent &&
           left.margin_bottom_percent == right.margin_bottom_percent &&
           left.margin_left_percent == right.margin_left_percent &&
           left.margin_top_auto == right.margin_top_auto &&
           left.margin_right_auto == right.margin_right_auto &&
           left.margin_bottom_auto == right.margin_bottom_auto &&
           left.margin_left_auto == right.margin_left_auto &&
           left.min_width_auto == right.min_width_auto &&
           left.min_width_percent == right.min_width_percent &&
           left.min_width_px == right.min_width_px &&
           left.min_height_auto == right.min_height_auto &&
           left.min_height_percent == right.min_height_percent &&
           left.min_height_px == right.min_height_px &&
           left.max_width_none == right.max_width_none &&
           left.max_width_percent == right.max_width_percent &&
           left.max_width_px == right.max_width_px &&
           left.max_height_none == right.max_height_none &&
           left.max_height_percent == right.max_height_percent &&
           left.max_height_px == right.max_height_px && left.flow_root == right.flow_root &&
           left.border_box == right.border_box && left.white_space == right.white_space &&
           left.word_break == right.word_break && left.overflow_wrap == right.overflow_wrap &&
           left.vertical_align == right.vertical_align &&
           left.vertical_align_percent == right.vertical_align_percent &&
           left.vertical_align_px == right.vertical_align_px &&
           left.line_height_kind == right.line_height_kind &&
           left.line_height_value == right.line_height_value &&
           left.text_align == right.text_align &&
           left.text_transform == right.text_transform &&
           left.text_overflow == right.text_overflow &&
           left.text_indent_percent == right.text_indent_percent &&
           left.text_indent_px == right.text_indent_px &&
           left.letter_spacing == right.letter_spacing &&
           left.word_spacing == right.word_spacing && left.aspect_ratio == right.aspect_ratio &&
           left.scrollbar_gutter == right.scrollbar_gutter &&
           left.scrollbar_width == right.scrollbar_width &&
           left.padding_top == right.padding_top && left.padding_right == right.padding_right &&
           left.padding_bottom == right.padding_bottom && left.padding_left == right.padding_left &&
           left.padding_top_percent == right.padding_top_percent &&
           left.padding_right_percent == right.padding_right_percent &&
           left.padding_bottom_percent == right.padding_bottom_percent &&
           left.padding_left_percent == right.padding_left_percent &&
           left.math_lengths.size() == right.math_lengths.size() &&
           std::equal(left.math_lengths.begin(), left.math_lengths.end(),
                      right.math_lengths.begin(),
                      [](const MathLength& a, const MathLength& b) {
                          return a.prop == b.prop && a.text == b.text;
                      }) &&
           left.border_top == right.border_top && left.border_right == right.border_right &&
           left.border_bottom == right.border_bottom && left.border_left == right.border_left &&
           left.border_top_style == right.border_top_style &&
           left.border_right_style == right.border_right_style &&
           left.border_bottom_style == right.border_bottom_style &&
           left.border_left_style == right.border_left_style &&
           left.outline_style == right.outline_style &&
           left.outline_width == right.outline_width &&
           left.outline_offset == right.outline_offset &&
           left.border_top_left_radius == right.border_top_left_radius &&
           left.border_top_right_radius == right.border_top_right_radius &&
           left.border_bottom_right_radius == right.border_bottom_right_radius &&
           left.border_bottom_left_radius == right.border_bottom_left_radius &&
           left.border_top_left_radius_percent == right.border_top_left_radius_percent &&
           left.border_top_right_radius_percent == right.border_top_right_radius_percent &&
           left.border_bottom_right_radius_percent == right.border_bottom_right_radius_percent &&
           left.border_bottom_left_radius_percent == right.border_bottom_left_radius_percent &&
           left.border_top_left_radius_y == right.border_top_left_radius_y &&
           left.border_top_right_radius_y == right.border_top_right_radius_y &&
           left.border_bottom_right_radius_y == right.border_bottom_right_radius_y &&
           left.border_bottom_left_radius_y == right.border_bottom_left_radius_y &&
           left.border_top_left_radius_y_percent == right.border_top_left_radius_y_percent &&
           left.border_top_right_radius_y_percent == right.border_top_right_radius_y_percent &&
           left.border_bottom_right_radius_y_percent == right.border_bottom_right_radius_y_percent &&
           left.border_bottom_left_radius_y_percent == right.border_bottom_left_radius_y_percent &&
           left.border_radius == right.border_radius && left.overflow_x == right.overflow_x &&
           left.overflow_y == right.overflow_y && left.font_size == right.font_size &&
           left.font_weight == right.font_weight && left.font_italic == right.font_italic &&
           left.font_kerning_none == right.font_kerning_none &&
           left.font_family == right.font_family && left.lang == right.lang &&
           left.writing_mode == right.writing_mode && left.direction == right.direction &&
           left.cb_for_absolute == right.cb_for_absolute &&
           left.cb_for_fixed == right.cb_for_fixed &&
           left.contain_paint == right.contain_paint &&
           left.contain_layout == right.contain_layout &&
           left.contain_inline_size == right.contain_inline_size &&
           left.contain_block_size == right.contain_block_size &&
           left.content_visibility_hidden == right.content_visibility_hidden &&
           left.content_visibility_auto == right.content_visibility_auto &&
           left.contain_intrinsic_inline == right.contain_intrinsic_inline &&
           left.contain_intrinsic_block == right.contain_intrinsic_block &&
           left.visibility_collapse == right.visibility_collapse &&
           left.table_row == right.table_row && left.table_part == right.table_part &&
           left.table_colspan == right.table_colspan && left.table_rowspan == right.table_rowspan &&
           left.border_collapse == right.border_collapse && left.table_span == right.table_span &&
           left.border_spacing_inline == right.border_spacing_inline &&
           left.border_spacing_block == right.border_spacing_block &&
           left.table_fixed == right.table_fixed && left.caption_below == right.caption_below &&
           left.table_column == right.table_column &&
           left.position_sticky == right.position_sticky && left.top_auto == right.top_auto &&
           left.top_percent == right.top_percent && left.top_px == right.top_px &&
           left.left_auto == right.left_auto && left.left_percent == right.left_percent &&
           left.left_px == right.left_px && left.right_auto == right.right_auto &&
           left.right_percent == right.right_percent && left.right_px == right.right_px &&
           left.bottom_auto == right.bottom_auto && left.bottom_percent == right.bottom_percent &&
           left.bottom_px == right.bottom_px && left.flex_direction == right.flex_direction &&
           left.flex_wrap == right.flex_wrap && left.flex_grow == right.flex_grow &&
           left.flex_shrink == right.flex_shrink && left.flex_basis_auto == right.flex_basis_auto &&
           left.flex_basis_percent == right.flex_basis_percent &&
           left.flex_basis_content == right.flex_basis_content &&
           left.flex_basis_px == right.flex_basis_px && left.column_gap == right.column_gap &&
           left.row_gap == right.row_gap && left.column_count == right.column_count &&
           left.column_width_auto == right.column_width_auto &&
           left.column_width == right.column_width &&
           left.column_fill_auto == right.column_fill_auto &&
           left.column_span_all == right.column_span_all &&
           left.column_gap_percent == right.column_gap_percent &&
           left.row_gap_percent == right.row_gap_percent && left.flex_order == right.flex_order &&
           left.align_items == right.align_items && left.align_self == right.align_self &&
           left.justify_content == right.justify_content &&
           left.align_content == right.align_content && left.justify_items == right.justify_items &&
           left.justify_self == right.justify_self &&
           tracks_eq(left.grid_template_columns, right.grid_template_columns) &&
           tracks_eq(left.grid_template_rows, right.grid_template_rows) &&
           tracks_eq(left.grid_auto_columns, right.grid_auto_columns) &&
           tracks_eq(left.grid_auto_rows, right.grid_auto_rows) &&
           grid_line_eq(left.grid_column_start, right.grid_column_start) &&
           grid_line_eq(left.grid_column_end, right.grid_column_end) &&
           grid_line_eq(left.grid_row_start, right.grid_row_start) &&
           grid_line_eq(left.grid_row_end, right.grid_row_end) &&
           left.grid_column_line_names == right.grid_column_line_names &&
           left.grid_row_line_names == right.grid_row_line_names &&
           left.grid_template_areas == right.grid_template_areas &&
           left.grid_auto_flow_column == right.grid_auto_flow_column &&
           left.grid_auto_flow_dense == right.grid_auto_flow_dense &&
           left.container_type == right.container_type &&
           left.break_before == right.break_before && left.break_after == right.break_after &&
           left.break_inside == right.break_inside && left.orphans == right.orphans &&
           left.widows == right.widows;
}

std::shared_ptr<const LayoutStyle> StyleTable::intern(LayoutStyle fields) {
    const auto found = entries_.find(fields);
    if (found != entries_.end()) {
        if (std::shared_ptr<const LayoutStyle> live = found->second.lock()) {
            return live;
        }
    }
    auto* raw = new LayoutStyle(std::move(fields));
    std::shared_ptr<const LayoutStyle> held(raw, [](const LayoutStyle* style) {
        StyleTable::process().entries_.erase(*style);
        delete style;
    });
    entries_[*raw] = held;
    return held;
}

// The used `font-size` of a node and of the root element. They live here because the layout-only
// targets compile this file and not `intern_style.cpp`, and both `in_flow.cpp` and
// `flow.cpp` need the root element's size to resolve `rem` inside a math function.
//
// `em` resolves against the element's own font size and `rem` against the root element's, so both
// have to be read before any other length. The element's own used font size is not carried by the
// interner, so `font-size` is resolved here: `em` and `%` against the parent's used size and `rem`
// against the root element's (CSS Fonts section 2.1, CSS Values section 5.1). The root element
// resolves against the initial 16px. Every other length reads this function's result as its `em`
// basis, so a wrong value here also moved padding, margins and text metrics.
//
// One intern used to reparse `font-size` once per ancestor, and a cell's lengths did it again.
// The memo is the used px for one node at one computed map and one viewport. `clear_font_px_cache`
// drops it at the start of every layout so a freed node's address cannot answer.

namespace {

struct FontPxKey {
    const html::Node* node = nullptr;
    const void* computed = nullptr;
    float vw = 0.f;
    float vh = 0.f;
    bool operator==(const FontPxKey& other) const {
        return node == other.node && computed == other.computed && vw == other.vw && vh == other.vh;
    }
};

struct FontPxKeyHash {
    std::size_t operator()(const FontPxKey& key) const {
        std::size_t hash = std::hash<const void*>{}(key.node);
        const auto mix = [&hash](std::size_t value) {
            hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
        };
        mix(std::hash<const void*>{}(key.computed));
        mix(std::hash<float>{}(key.vw));
        mix(std::hash<float>{}(key.vh));
        return hash;
    }
};

std::unordered_map<FontPxKey, float, FontPxKeyHash>& font_px_cache() {
    static auto* cache = new std::unordered_map<FontPxKey, float, FontPxKeyHash>();
    return *cache;
}

// CSS 2.1 §15.7 absolute-size table at the default medium of 16px. Adjacent keywords
// are not a flat 1.2: small is 13 and large is 18. xxx-large is the Fonts 4 eighth step.
constexpr float kAbsoluteFontPx[] = {9.f, 10.f, 13.f, 16.f, 18.f, 24.f, 32.f, 48.f};
constexpr const char* kAbsoluteFontName[] = {
    "xx-small", "x-small", "small", "medium", "large", "x-large", "xx-large", "xxx-large"};

bool font_keyword_is(std::string_view text, const char* keyword) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    for (std::size_t i = 0;; ++i) {
        if (keyword[i] == '\0') {
            return i == text.size();
        }
        if (i >= text.size()) {
            return false;
        }
        const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
        if (c != keyword[i]) {
            return false;
        }
    }
}

// Index into `kAbsoluteFontPx`, or -1 when the specified value is a length or a
// relative keyword. A missing value is the initial `medium`: `initial` drops the
// declaration, and the root has no parent.
int absolute_font_index(const html::Node* node) {
    if (node == nullptr) {
        return 3;
    }
    const std::string* specified = style_literal(node, "font-size");
    if (specified == nullptr) {
        return 3;
    }
    for (int i = 0; i < 8; ++i) {
        if (font_keyword_is(*specified, kAbsoluteFontName[i])) {
            return i;
        }
    }
    return -1;
}

float font_size_px_uncached(const html::Node* node) {
    const std::string* v = style_literal(node, "font-size");
    if (v == nullptr) {
        return 16.f;
    }
    // Viewport units in `font-size` are baked here, the same way other lengths are. `em` on a
    // descendant then sees the new used size when that box is re-interned after a resize.
    note_viewport_unit_text(*v);
    const int absolute = absolute_font_index(node);
    if (absolute >= 0) {
        return kAbsoluteFontPx[absolute];
    }
    const int step = font_keyword_is(*v, "larger") ? 1 : font_keyword_is(*v, "smaller") ? -1 : 0;
    if (step != 0) {
        // A length parent scales by 1.2. An absolute-size parent, including the initial
        // medium left behind when `font-size` is dropped, steps the table.
        const int base = absolute_font_index(node->parent);
        if (base >= 0) {
            const int next = base + step;
            if (next >= 0 && next < 8) {
                return kAbsoluteFontPx[next];
            }
            const float edge = next < 0 ? kAbsoluteFontPx[0] : kAbsoluteFontPx[7];
            return step < 0 ? edge / 1.2f : edge * 1.2f;
        }
        const float parent_px = node->parent != nullptr ? font_size_px(node->parent) : 16.f;
        return step > 0 ? parent_px * 1.2f : parent_px / 1.2f;
    }
    if (node->parent == nullptr) {
        const css::ResolvedLength root = css::resolve_length(
            *v, 16.f, 16.f, g_length_viewport.width, g_length_viewport.height);
        if (!root.valid) {
            return 16.f;
        }
        return root.is_percent ? 16.f * root.px / 100.f : root.px;
    }
    const float parent = font_size_px(node->parent);
    const css::ResolvedLength resolved = css::resolve_length(
        *v, parent, root_font_size_px(node), g_length_viewport.width, g_length_viewport.height);
    if (!resolved.valid) {
        // A `font-size` that cannot be resolved behaves as `inherit`, like any invalid inherited
        // value, instead of silently jumping back to 16px.
        return parent;
    }
    return resolved.is_percent ? parent * resolved.px / 100.f : resolved.px;
}

}  // namespace

float font_size_px(const html::Node* node) {
    if (node == nullptr) {
        return 16.f;
    }
    FontPxKey key;
    key.node = node;
    key.computed = node->computed.get();
    key.vw = g_length_viewport.width;
    key.vh = g_length_viewport.height;
    auto& cache = font_px_cache();
    const auto found = cache.find(key);
    if (found != cache.end()) {
        return found->second;
    }
    const float px = font_size_px_uncached(node);
    // The parent walk above may have rehashed the map. `key` is still this node.
    cache.emplace(key, px);
    return px;
}

void clear_font_px_cache() { font_px_cache().clear(); }

float root_font_size_px(const html::Node* node) {
    const html::Node* root = node;
    while (root != nullptr && root->parent != nullptr) {
        root = root->parent;
    }
    // The root element's used size is a property of the document, not of `node`, and
    // `font_size_px` walks up to this same root at *every* level of its own recursion - so this
    // call, and the parse inside it, used to run once per ancestor level of every intern. One entry
    // covers the whole tree; the root's computed style is held so the address cannot be reused by a
    // later map, and a re-committed style changes the pointer and re-derives.
    struct RootFontSize {
        const html::Node* root = nullptr;
        std::shared_ptr<const StyleMap> style;
        float px = 16.f;
    };
    // Leaked like the intern memo: releasing a `shared_ptr` from a static destructor, after the
    // rest of the process is gone, is what crashed the test binaries there.
    static RootFontSize* cached = new RootFontSize();
    if (cached->root == root && root != nullptr && cached->style == root->computed) {
        return cached->px;
    }
    cached->px = font_size_px(root);
    cached->root = root;
    cached->style = root != nullptr ? root->computed : nullptr;
    return cached->px;
}

}  // namespace layout
}  // namespace zui
