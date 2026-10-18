#ifndef ZUI_SOURCE_LAYOUT_FLOW_DETAIL_H
#define ZUI_SOURCE_LAYOUT_FLOW_DETAIL_H

#include "layout/box.h"
#include "layout/cache/box_cache.h"
#include "layout/cache/exclusion_space.h"
#include "layout/cache/float_place.h"
#include "layout/cache/inline_content.h"
#include "layout/cache/layout_context.h"
#include "layout/cache/layout_key.h"
#include "layout/cache/line_box.h"
#include "layout/cache/rect.h"
#include "layout/cache/sticky.h"
#include "layout/cache/used_length.h"
#include "layout/cache/logical_axes.h"
#include "layout/overflow_mode.h"
#include "text/font_chain.h"

#include "css/values.h"
#include "html/document.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <vector>


namespace zui {
namespace layout {
namespace flow_detail {

const LayoutStyle& style_of(const Box& box);



struct UsedEdges {
    float margin_inline_start = 0;
    float margin_inline_end = 0;
    float margin_block_start = 0;
    float margin_block_end = 0;
    bool margin_inline_start_auto = false;
    bool margin_inline_end_auto = false;
    bool margin_block_start_auto = false;
    bool margin_block_end_auto = false;
    float padding_inline_start = 0;
    float padding_inline_end = 0;
    float padding_block_start = 0;
    float padding_block_end = 0;
};

void map_physical_auto(WritingMode mode, Direction direction, bool top_auto, bool right_auto,
                       bool bottom_auto, bool left_auto, bool* inline_start_auto,
                       bool* inline_end_auto, bool* block_start_auto, bool* block_end_auto);

UsedEdges resolve_edges(const LayoutStyle& style, const AxisSize& inline_basis,
                        const html::Node* node = nullptr);

Edges border_edges(const LayoutStyle& style);

float chrome_inline(const LayoutStyle& style, const UsedEdges& edges);

float chrome_block(const LayoutStyle& style, const UsedEdges& edges);

float box_content(float specified, float chrome, bool border_box);

// Max, then min. `max_size` at or above 1e8 is "none".
float clamp_used_content(float content, float min_size, bool min_auto, float max_size,
                         bool border_box, float chrome);

// Inline offset of the content origin inside the parent's border box.
float content_inline_origin(WritingMode mode, Direction direction, const UsedEdges& edges);

float used_line_height(const LayoutStyle& style, const LayoutContext::FontEm& em);

float used_indent(const LayoutStyle& style, const LayoutKey& key);

// Positive raises the box. sub lowers by 0.2em and super raises by one third of the line's font size.
float align_shift(const LayoutStyle& line_style, const LayoutStyle& child_style,
                  LayoutContext& cx);

void apply_relative(Box& child, float cb_inline, bool inline_known, const AxisSize& cb_block);

void write_edges(BoxGeometry& geometry, const LayoutStyle& style, const UsedEdges& edges);



struct Adjoining {
    float positive = 0;
    float negative = 0;

    void add(float margin) {
        if (margin < 0) {
            negative = std::min(negative, margin);
        } else {
            positive = std::max(positive, margin);
        }
    }

    float used() const { return positive + negative; }
};

std::shared_ptr<const ExclusionSpace>
shared_exclusion(const std::shared_ptr<const ExclusionSpace>& current);

std::shared_ptr<const ExclusionSpace>
shift_exclusion(const std::shared_ptr<const ExclusionSpace>& src, float dx, float dy);

bool out_of_flow(const LayoutStyle& style);

/// A `#text` node between a `<select>`'s options, or the anonymous block `wrap_inlines` built
/// around one. The control's label is its own text run (`node == nullptr`). These nodes inherit
/// `white-space: pre` and would otherwise become line boxes that stretch the button over the popup.
inline bool select_source_whitespace(const Box& box) {
    if (box.role == BoxRole::Text && box.node != nullptr && box.node->tag == "#text" &&
        box.node->parent != nullptr && html::is_select(box.node->parent)) {
        return true;
    }
    if (box.role != BoxRole::Anonymous || box.children.empty()) {
        return false;
    }
    for (const Box* child : box.children) {
        if (child != nullptr && !select_source_whitespace(*child)) {
            return false;
        }
    }
    return true;
}

/// `::marker` with `list-style-position: outside` on the list item: hangs beside the principal
/// box and must not join the IFC or contribute to scrollable overflow.
bool is_outside_marker(const Box& child, const LayoutStyle& parent_style);

bool establishes_bfc(const Box& box, const LayoutStyle& style);

bool inline_level(const Box& box);

/// The terminal specializations of `layout_normal`: a text run, a `br`, a replaced element and a
/// simple ruby container each own their whole box. Returns true when it filled `out`.
bool layout_normal_special(Box& box, const LayoutKey& self_key, LayoutContext& cx,
                           const UsedEdges& edges, const LayoutStyle& style, LayoutResult& out);

/// CSS 2.1 section 10.3.3 / 10.6.4: `margin: auto` against a definite available size splits the
/// leftover space. An over-constrained in-flow block solves margin-right (ltr) or margin-left
/// (rtl). Inline-blocks, flex items, and grid items do not. Rewrites `edges`.
void resolve_auto_margins(UsedEdges& edges, const LayoutStyle& style, const LayoutKey& self_key,
                          float content_inline, float inline_chrome, float content_block,
                          float block_chrome, bool width_auto, bool block_auto, bool abspos,
                          bool flex_item, bool grid_item);

/// The HTML widget preferred sizes: checkbox / radio, range, progress, file, and a text input with
/// `width: auto`. Each fills the used size it owns when the corresponding axis is still `auto`.
void resolve_widget_size(Box& box, LayoutContext& cx, const LayoutStyle& style, bool& width_auto,
                         float& width_used, bool& height_auto, float& height_used);

/// Scrollable overflow of a native `input[type=file]` (the shadow button plus the filename).
/// The border box stays the author's used size. A zero rect means there is no native chrome
/// (`appearance: none`, or fonts are not ready).
Rect file_shadow_scroll(const Box& box, LayoutContext& cx, const LayoutStyle& style);

/// Baseline of a single-line text input, measured from its border-box block-start, or NaN
/// when `box` is not one. `overflow: clip` makes `atomic_inline_baseline` report none, and
/// the line then uses the bottom margin edge.
float text_input_baseline(const Box& box, const LayoutStyle& style, const LayoutResult& result,
                          const LayoutContext::FontEm& em);



struct BfcAsk {
    Box* box = nullptr;
    float y = 0;
    float height = 0;
    float available = 0;
};



struct Walk {
    float content_inline = 0;
    float content_block = 0;
    // LAYOUT.md section 4: the ink rectangle this box's contents reach, in this box's own
    // border-box frame (so it can go negative). `normal_flow` unions it with the box's own border
    // box and stores the result as `Overflow::ink`; shadows are paint-only and never land here.
    Rect ink;
    // How far a line box directly in this box reaches, without the container's end padding.
    // `scrollWidth` adds that padding for a line and leaves it off a block child's border box.
    float line_inline = 0;
    // Where the inline content itself reaches, which can be past `content_block`: a line box is as
    // tall as `line-height`, and a content area taller than that (a negative half-leading) sticks
    // out of it. A browser reports that as scrollable overflow but not as used height, so the two
    // quantities stay apart.
    float ink_block = 0;
    float float_bottom = 0;
    bool has_float = false;
    bool all_through = true;
    Adjoining leading;
    Adjoining trailing;
    std::vector<FloatRecord> floats;
    std::vector<ChildPlace> places;
    struct SiblingNote {
        Box* box = nullptr;
        bool is_bfc = false;
        bool is_float = false;
        bool has_lines = false;
        bool has_floats = false;
        int order = 0;
        struct LineSpan {
            float y = 0;
            float h = 0;
        };
        std::vector<LineSpan> line_spans;
        float x = 0;
        float y = 0;
        float w = 0;
        float h = 0;
    };
    std::vector<Box*> asked;
    std::vector<BfcAsk> bfc_asks;
    std::vector<SiblingNote> siblings;
    std::vector<FloatRecord> placed;
    bool unstable = false;
    // Fragmentation (LAYOUT.md section 12): `fragment_full` is set when the fragmentainer ran out
    // of room and the walk stopped before a line or a child. `fragment_line` / `fragment_child`
    // say where the next fragment resumes (`-1`: that walk did not stop there).
    bool fragment_full = false;
    int fragment_line = -1;
    int fragment_child = -1;
    // The block offset inside this box where the next fragment resumes (its own block axis).
    float fragment_block = 0;
    // When the break is inside the child at `fragment_child` (that child split between its own
    // lines), how much of the child's own block axis it already consumed - the frame the child
    // resumes in, which is not this box's.
    float fragment_child_block = 0;
    // Deeper resume: when that child broke at a nested child boundary, the child's own
    // `child_index` / `child_block_offset` (`-1` / `0` for a line-only split).
    int fragment_inner_child = -1;
    float fragment_inner_child_block = 0;
    // How much more room the first content that did not fit this fragment would have needed
    // (LAYOUT.md section 12; the container's column balancing stretches by it).
    float fragment_shortage = 0;
    std::unique_ptr<InlineContent> lines;
    BaselineSet baselines;
};

void remember_place(Walk& walk, Box* child);

/// Place an outside marker so its inline-end edge meets the content-box start. Does not call
/// `note_child_ink` (markers outside must not grow scrollWidth / scrollHeight).
void place_outside_marker(Box& marker, const LayoutResult* result, const UsedEdges& edges,
                          float block_offset, Walk& walk);

/// LAYOUT.md section 4: union a placed child's ink (its border box plus whatever the layout put
/// inside it) into this box's ink rectangle. `walk.ink` is in this box's border-box frame, which is
/// the frame `Overflow::ink` is stored in.
void note_child_ink(Walk& walk, const LayoutStyle& box_style, const Box& child,
                    const LayoutResult& result, const Edges& border);

bool overlaps_float(float y, float height, const FloatRecord& band);

const LayoutResult* ask_child(LayoutContext& cx, Box& child, const ConstraintSpace& space,
                              Walk& walk, const std::vector<Box*>* reask);

bool boxes_overlap(float x, float y, float w, float h, const Rect& band);

ConstraintSpace child_space(const LayoutKey& key, const UsedEdges& edges, float content_inline,
                            bool inline_known, const AxisSize& inline_available,
                            const std::shared_ptr<const ExclusionSpace>& exclusion,
                            const AxisSize& block_available, const AxisSize& block_percent,
                            LogicalPoint bfc_origin, WritingMode writing_mode,
                            Direction direction);

void layout_text(Box& box, const LayoutKey& key, LayoutContext& cx, LayoutResult& out);

void layout_replaced(Box& box, const LayoutKey& key, LayoutContext& cx, LayoutResult& out);

/// CSS Ruby Level 1 simple ruby: annotations (`display:ruby-text`) stack above the base on the
/// block axis; the container's inline size is max(base, annotation) with both runs centered.
bool is_ruby_container(const Box& box);
bool is_ruby_text(const Box& box);
void layout_ruby(Box& box, const LayoutKey& key, LayoutContext& cx, const UsedEdges& edges,
                 LayoutResult& out);



struct Piece {
    bool atomic = false;
    bool is_float = false;
    // An out-of-flow inline. It takes no space; the line cursor where it would have
    // sat is its static position.
    bool abs_static = false;
    bool space = false;
    // pre-wrap: preserved spaces that overflow hang outside the line box and must not grow
    // scrollWidth / line_inline (CSS Text hanging glyphs). Still painted via shaped runs.
    bool hanging = false;
    float advance = 0;
    GlyphInline glyph;
    Box* box = nullptr;
    float block = 0;
    float baseline = 0;
    // A text piece's own metrics. CSS 2.1 section 10.8 gives every inline box the height of its
    // own line-height (not of its font box), so the line box needs them per piece, not just for
    // the block's strut.
    float ascent = 0;
    float descent = 0;
    float line_height = 0;
    VerticalAlign align = VerticalAlign::Baseline;
    float align_shift = 0;
    std::string chars;
    // Characters of the element's own text this piece stands for, in codepoints. Used by the
    // trace export to number line starts the way a browser does (`lines.breaks`), which counts
    // the text nodes of the element even where the line breaker dropped the character.
    std::size_t source_chars = 0;
    FloatSide float_side = FloatSide::Left;
    ClearSide clear_side = ClearSide::None;
    const LayoutResult* result = nullptr;
    // Element that owns this glyph's font and color when the glyph was pulled out of a
    // nested inline box into the ancestor's line boxes.
    Box* style_box = nullptr;
};

std::size_t codepoint_count(const std::string& text);

/// The inline pieces of one line-formatting context, built before wrapping.
struct PieceBuild {
    std::vector<Piece> pieces;
    /// `::marker` with `list-style-position: outside`, laid out but not part of the line.
    Box* outside_marker = nullptr;
    const LayoutResult* outside_marker_result = nullptr;
};

/// Walk the in-flow inline children of `box` and turn them into `Piece`s (CSS 2.1 section 9.4).
/// Floats, out-of-flow boxes and `::marker` are recorded where they sit, not emitted as text.
PieceBuild build_pieces(Box& box, const LayoutKey& key, LayoutContext& cx, const UsedEdges& edges,
                        float content_inline, bool inline_known, float flow_inline,
                        const std::shared_ptr<const ExclusionSpace>& exclusion,
                        const AxisSize& block_available, const AxisSize& block_percent,
                        const std::vector<Box*>* reask, Walk& walk, const LayoutStyle& style);

/// Characters a browser would count for this box: the text of its whole subtree, because a traced
/// element's `lines.breaks` is numbered over all of its descendant text nodes.
std::size_t subtree_text_chars(const Box& box);

void append_glyphs(std::vector<Piece>& pieces, const LayoutResult* result,
                   const LayoutStyle& piece_style, const LayoutContext::FontEm& piece_em);

/// CSS Text 3 §8.2: add `word_spacing` to each word-separator piece (U+0020 / NBSP).
void apply_word_spacing(std::vector<Piece>& pieces, std::size_t begin, float word_spacing);

bool collapse_spaces(WhiteSpace white_space);

bool wraps(WhiteSpace white_space);

/// Diagnostic counters for the cold-frame probe. Not a layout result.
void note_block_walk();
void note_line_walk();
void note_line_tail(double ms);
void note_flow_special();
void note_flow_plain();
void note_flow_second();
void flow_bucket(int bucket);
/// `layout_blocks` / `build_pieces` time with `ask_child` subtracted.
void note_block_book(double ms);
void note_piece_book(double ms);

Walk layout_lines(Box& box, const LayoutKey& key, LayoutContext& cx, const UsedEdges& edges,
                  float content_inline, bool inline_known,
                  const std::shared_ptr<const ExclusionSpace>& incoming, bool pending,
                  float scrollbar, const AxisSize& block_available, const AxisSize& block_percent,
                  LogicalPoint origin, const std::vector<Box*>* reask);

/// The wrap walk `layout_lines` runs after pieces are built, including a `text-wrap: balance`
/// retry. The piece builder stays in `lines.cpp`.
bool wants_text_wrap_balance(const Box& box);

void run_line_wrap(Box& box, const LayoutKey& key, LayoutContext& cx, const UsedEdges& edges,
                   float content_inline, bool inline_known, float& flow_inline, bool bfc,
                   const LogicalPoint& box_origin,
                   std::shared_ptr<const ExclusionSpace>& exclusion, std::vector<Piece>& pieces,
                   Box* outside_marker, const LayoutResult* outside_marker_result, Walk& walk,
                   std::unique_ptr<InlineContent>& lines, float& y, std::vector<Piece>& line,
                   float& line_width, float& line_start, std::size_t& line_source_chars,
                   bool& pending_break_rect, bool collapse, bool can_wrap, float indent,
                   const FragmentationContext& frag, bool fragmented, float frag_limit,
                   float frag_own_start, float own_chrome_block, int resume_line,
                   const LayoutStyle& style, const LayoutContext::FontEm& em,
                   const AxisSize& block_percent);

// Inline-content assembly shared by the line walk (`lines.cpp`) and the piece builder
// (`inline_content.cpp`).
bool plain_inline(const Box& box, const LayoutStyle& style);
/// True when a nested `display:inline` box must join the parent IFC (CSS 2.1 §9.4.2).
/// A span that already wrapped on its own is always absorbed (so a following button is
/// not painted under its second line). A zero-chrome span is absorbed only into an
/// *inline* parent, so a highlight cannot sit as one unbreakable piece after a short
/// prefix; a block parent keeps a one-line body span as a single piece (first-line
/// overflow) instead of re-wrapping its glyphs to the column width.
bool should_absorb_inline(const Box& box, const LayoutStyle& style, const LayoutResult* result,
                          const Box& parent);
void append_text_pieces(std::vector<Piece>& pieces, Box& block, Box& text_box,
                        const LayoutResult* result, const LayoutStyle& block_style,
                        const LayoutStyle& text_style, LayoutContext& cx, Box* face);
void absorb_wrapped_inline(std::vector<Piece>& pieces, Box& block, Box& inline_el,
                           const LayoutStyle& block_style, LayoutContext& cx);

Walk layout_blocks(Box& box, const LayoutKey& key, LayoutContext& cx, const UsedEdges& edges,
                   float content_inline, bool inline_known, bool inside_top,
                   const std::shared_ptr<const ExclusionSpace>& incoming, bool pending,
                   const std::vector<BfcAsk>* prior, float scrollbar,
                   const AxisSize& block_available, const AxisSize& block_percent,
                   const std::vector<Box*>* reask);

/// Fragmented block walk (LAYOUT.md section 12).
///
/// A block container inside a fragmentainer places its in-flow children until one of them does not
/// fit, then reports where the next fragment resumes. Subset, deliberately: in-flow children with
/// the usual adjacent-margin collapse, no floats, no clearance and no out-of-flow children across
/// a break (they are placed in the fragment they start in), and a child that does not fit whole in
/// an empty fragment is placed anyway, overflowing it - a break there would make no progress, and
/// the container's balancing stretches the fragmentainer by exactly that overflow.
Walk layout_blocks_fragmented(Box& box, const LayoutKey& key, LayoutContext& cx,
                              const UsedEdges& edges, float content_inline, bool inline_known,
                              float scrollbar, const AxisSize& block_percent,
                              const FragmentationContext& frag);

bool inline_context(const Box& box);

void write_sticky(Box& box, const LayoutStyle& style, const LayoutResult& out);

float longest_unbreakable(const InlineContent* lines);

// Border-box min-content, including this box's padding and border. Margins stay with the caller.
float content_min(const Box& box, const LayoutResult* result);


Walk run_flow_content(Box& box, const LayoutKey& self_key, LayoutContext& cx,
                      const UsedEdges& edges, float limit, bool known, bool pending,
                      const std::vector<BfcAsk>* prior, const std::vector<Box*>* reask,
                      bool blocks, bool bfc, bool inside_top, float scrollbar,
                      const AxisSize& block_percent, const AxisSize& block_available);

struct NormalFlowWork {
    Box* box = nullptr;
    const LayoutKey* self_key = nullptr;
    LayoutContext* cx = nullptr;
    LayoutResult* out = nullptr;
    const LayoutStyle* style = nullptr;
    UsedEdges edges{};
    bool bfc = false;
    bool inline_auto = false;
    bool block_auto = false;
    float inline_chrome = 0;
    float block_chrome = 0;
    bool lock_inline = false;
    bool lock_block = false;
    bool abspos = false;
    bool flex_inline = false;
    bool flex_block = false;
    bool grid_inline = false;
    bool grid_stretch = false;
    bool cell_sized_by_column = false;
    bool shrink = false;
    float content_inline = 0;
    bool inline_known = false;
    bool stretch_block = false;
    float content_block = 0;
    bool blocks = false;
    float min_inline = 0;
    float max_inline = 1.0e9f;
    bool min_inline_auto = true;
    bool inside_top = false;
    bool inside_bottom = false;
    float scrollbar_thickness = 0;
    float scrollbar = 0;
    float block_scrollbar = 0;
    AxisSize block_percent{};
    Walk laid{};
    float final_inline = 0;
    bool known_now = false;
    bool min_separates = false;
    float min_block = 0;
    bool min_block_auto = true;
    float max_block = 1.0e9f;
    bool width_auto = false;
    float block_used = 0;
    bool add_block_bar = false;
    bool add_inline_bar = false;
};

void layout_normal_second_walk(NormalFlowWork& w);
void layout_normal_finish(NormalFlowWork& w);


void finish_wrapped_line(Box& box, const UsedEdges& edges, float content_inline, bool inline_known,
                         float cb_inline, Walk& walk, std::unique_ptr<InlineContent>& lines,
                         float& y, std::vector<Piece>& line, float& line_width, float& line_start,
                         std::size_t& line_source_chars, bool& pending_break_rect, bool collapse,
                         const LayoutStyle& style, const LayoutContext::FontEm& em,
                         const AxisSize& block_percent, bool fragmented, float frag_limit,
                         float frag_own_start, float own_chrome_block, int& line_count,
                         bool last_line, bool forced_empty, LayoutContext& cx, const LayoutKey& key,
                         float flow_inline, int resume_line, const FragmentationContext& frag,
                         const std::shared_ptr<const ExclusionSpace>& exclusion);

}  // namespace flow_detail
}  // namespace layout
}  // namespace zui

#endif
