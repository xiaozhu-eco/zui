#ifndef ZUI_SOURCE_INPUT_DETAIL_H
#define ZUI_SOURCE_INPUT_DETAIL_H

// Internals shared by the input translation units: node/handler lookup, focus and tab order,
// text-editing helpers, scroll snapping and anchoring. `interaction.cpp` keeps the Interaction
// methods; these helpers used to live in its anonymous namespace.

#include "input/interaction.h"

#include "layout/point.h"
#include "layout/scrollbar_metrics.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace zui {
namespace input {
namespace input_detail {

// Keyboard scrolling steps (they were the top of the old anonymous helper block).
constexpr float kArrowScrollPx = 40.f;
// Chromium's keyboard page step is 87.5% of the scrollport, and never less than a line.
constexpr float kPageFraction = 0.875f;
/// Touch/pen travel before a press turns into a scroll instead of a click (Pointer Events slop).
constexpr float kPanSlopPx = 8.f;

// Scroll snap state shared by the snap helpers.
enum class SnapAxis { None, X, Y, Both };
enum class SnapStrictness { None, Mandatory, Proximity };
enum class SnapAlign { None, Start, Center, End };

struct SnapType {
    SnapAxis axis = SnapAxis::None;
    SnapStrictness strict = SnapStrictness::None;
};

html::Node* node_of(layout::Box* hit);

html::Node* editable_from(html::Node* node);

html::Node* focus_candidate_from(html::Node* node);

bool under_node(const html::Node* node, const html::Node* root);

bool pointer_in_layout_border(layout::LayoutEngine& layout, html::Node& node, float x, float y);

bool pointer_in_painted_border(layout::LayoutEngine& layout, html::Node& node, float x, float y);

html::Node* handler_owner(html::Node* origin, std::string_view kind);

bool is_checkable(const html::Node* node);

html::Node* find_checkable_desc(html::Node* node);

html::Node* checkable_from(html::Document& doc, html::Node* node);

void uncheck_radio_group(html::Node* node, const std::string& name, html::Node* keep,
                         std::vector<html::Node*>* unchecked = nullptr);

layout::Box* nearest_scroller(layout::Box* hit);

std::size_t utf8_next(const std::string& text, std::size_t index);

std::size_t utf8_prev(const std::string& text, std::size_t index);

/// The `\n`-bounded line @p index sits in. Home / End of a text control move inside one line.
std::size_t line_begin(const std::string& text, std::size_t index);

std::size_t line_end(const std::string& text, std::size_t index);

bool ascii_word_char(unsigned char c);

/// Whether the character at @p at (a UTF-8 lead byte) reads as part of a word: ASCII letters,
/// digits and `_`, a CJK ideograph or kana, and any other letter-ish code point. The punctuation a
/// browser breaks a word at stays out, so a CJK run is one word rather than one character per
/// double click.
bool word_char_at(const std::string& text, std::size_t at);

void word_range(const std::string& text, std::size_t index, std::size_t& out_start,
                std::size_t& out_end);

int parse_tabindex(std::string_view raw);

int tabindex_of(const html::Node* node);

bool is_sequentially_focusable(const html::Node* node);

void collect_focusable(html::Node* node, std::vector<html::Node*>& out);

std::vector<html::Node*> tab_order(html::Node* root);

bool is_file_input(const html::Node* node);

/// The labeled control of a `<label for>` or a wrapping `<label>`, or null.
html::Node* label_control(html::Document& doc, html::Node* node);

html::Node* form_ancestor(html::Node* node);

bool is_submit_button(const html::Node* node);

bool is_reset_button(const html::Node* node);

/// `<button>` and `input` of type button / submit / reset.
bool is_activation_target(const html::Node* node);

/// Single-line fields whose Enter asks the ancestor `<form>` to submit.
bool submits_on_enter(const html::Node* node);

void seed_content_editable(html::Node* node);

/// Apply a flatten-string replacement to the CE tree (prefix/suffix kept, middle deleted+inserted).
void apply_flatten_replace(html::Node* host, const std::string& previous, const std::string& next);

/// Legacy: project format_runs onto CE children. Typing/delete use tree ops at the Selection.
void sync_content_editable(html::Node* node, const std::vector<FormatRun>& runs);

/// Legacy name: sync from node->format projection (prefer sync_content_editable with runs).
void rebuild_content_editable(html::Node* node);

std::string path_basename(std::string_view path);

void trim_inplace(std::string& s);

FileDialogOptions file_dialog_from_input(const html::Node& node);

float view_height(const layout::Box& box);

std::string_view node_data_attribute(const void* element, std::string_view name);

bool always_focus_visible(const html::Node* node);

void clear_focus_flags(html::Node* node);

void apply_focus(html::Node* node, bool keyboard_modality);

void report_ime_rect(const InteractionHost& host, html::Node* node,
                     layout::CaretAffinity affinity = layout::CaretAffinity::Downstream);

SnapType parse_snap_type(const html::Node* node);

SnapAlign parse_snap_align_token(std::string_view tok);

void parse_snap_align(const html::Node* node, SnapAlign& block, SnapAlign& inline_axis);

float snap_offset(SnapAlign align, float area_start, float area_size, float port_size);

float choose_snap(float proposed, const std::vector<float>& stops, SnapStrictness strict,
                  float port_size);

bool overflow_anchor_none(const html::Node* node);

bool is_scroll_anchor_candidate(const layout::Box& box);

float content_block_in_scroller(const layout::Box* target, const layout::Box* scroller);

html::Node* examine_anchor_candidate(layout::Box& box, layout::Box& scroller, float view0,
                                     float view1);

html::Node* select_scroll_anchor(layout::Box& scroller);

layout::Box* find_box_for_node(layout::Box& root, html::Node* node);

void remember_scroll_anchor(layout::Box& scroller);

bool adjust_scroll_anchor_one(layout::Box& scroller);

bool adjust_scroll_anchors_walk(layout::Box& box);

void apply_scroll_snap(layout::Box& box, float& x, float& y, float view_w, float view_h);

layout::Box* nearest_scrollable(layout::Box* start, float scrollbar_thickness);

float align_scroll(ScrollAlign align, float target_start, float target_size, float port_start,
                   float port_size);

}  // namespace input_detail
}  // namespace input
}  // namespace zui

#endif
