#include "layout/baseline.h"
#include "layout/flow/detail.h"
#include "trace/trace.h"

namespace zui {
namespace layout {
namespace flow_detail {

bool keyword_is(const std::string& value, const char* keyword, std::size_t length) {
    std::size_t i = 0;
    while (i < value.size()) {
        while (i < value.size() &&
               (value[i] == ' ' || value[i] == '\t' || value[i] == '\n' || value[i] == '\r')) {
            ++i;
        }
        const std::size_t begin = i;
        while (i < value.size() && value[i] != ' ' && value[i] != '\t' && value[i] != '\n' &&
               value[i] != '\r') {
            ++i;
        }
        if (i - begin != length) {
            continue;
        }
        bool match = true;
        for (std::size_t k = 0; k < length; ++k) {
            char c = value[begin + k];
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
            if (c != keyword[k]) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

/// CSS Text 4 `text-wrap` / `text-wrap-style`. The property is not in the CSS parser's
/// known set, so the cascaded string is read here. Inheritance is the cascade's job;
/// this only sees a value set on the element itself.
bool wants_text_wrap_balance(const Box& box) {
    if (box.node == nullptr) {
        return false;
    }
    if (const std::string* wrap = box.node->style("text-wrap")) {
        if (keyword_is(*wrap, "balance", 7)) {
            return true;
        }
    }
    if (const std::string* style = box.node->style("text-wrap-style")) {
        return keyword_is(*style, "balance", 7);
    }
    return false;
}

Walk layout_lines(Box& box, const LayoutKey& key, LayoutContext& cx, const UsedEdges& edges,
                  float content_inline, bool inline_known,
                  const std::shared_ptr<const ExclusionSpace>& incoming, bool pending,
                  float scrollbar, const AxisSize& block_available, const AxisSize& block_percent,
                  LogicalPoint origin, const std::vector<Box*>* reask) {
    Walk walk;
    (void)pending;
    const LayoutStyle& style = style_of(box);
    const LayoutContext::FontEm em = cx.font_em(style);
    // Non-const: `text-wrap: balance` narrows this wrap width and runs the breaker again.
    // The used box width stays the caller's `content_inline`.
    float flow_inline =
        inline_known ? std::max(0.f, content_inline - scrollbar) : content_inline;
    const bool bfc = establishes_bfc(box, style);
    const LogicalPoint box_origin = bfc ? LogicalPoint{} : origin;
    std::shared_ptr<const ExclusionSpace> exclusion =
        shift_exclusion(incoming, -box_origin.inline_offset, -box_origin.block_offset);

    // The in-flow children become pieces first (`inline_pieces.cpp`); the wrap walk below turns
    // them into line boxes.
    zui::trace::Zone pieces_zone("lines.pieces", zui::trace::Category::LayoutPhase);
    PieceBuild build =
        build_pieces(box, key, cx, edges, content_inline, inline_known, flow_inline, exclusion,
                     block_available, block_percent, reask, walk, style);
    std::vector<Piece>& pieces = build.pieces;
    Box* outside_marker = build.outside_marker;
    const LayoutResult* outside_marker_result = build.outside_marker_result;
    note_line_walk();

    auto lines = std::make_unique<InlineContent>();
    float y = 0;
    std::vector<Piece> line;
    float line_width = 0;
    float line_start = 0;
    // Characters consumed while this line is open, including the collapsible whitespace the line
    // breaker drops. They are recorded on the LineBox when the line is closed (LAYOUT section 5).
    std::size_t line_source_chars = 0;
    bool pending_break_rect = false;
    const bool collapse = collapse_spaces(style.white_space);
    const bool can_wrap = wraps(style.white_space) && inline_known;
    const float indent = used_indent(style, key);
    // Fragmentation (LAYOUT.md section 12). `remaining_block` is how much of this box's border box
    // fits in the current fragmentainer, `incoming.block_offset` is how much of the box's own block
    // axis earlier fragments already consumed, and `incoming.line_index` is the first line that did
    // not fit there. A line that does not fit opens the next fragment; the first line of a fragment
    // always goes in even when it overflows, because a line box cannot be split and an empty
    // fragment would make no progress at all (Chromium's `MovePastBreakpoint` does the same).
    const FragmentationContext& frag = key.space.fragmentation;
    const bool fragmented = frag.active;
    const float frag_limit = fragmented ? std::max(0.f, frag.remaining_block) : 0.f;
    const float frag_own_start = fragmented ? std::max(0.f, frag.incoming.block_offset) : 0.f;
    const float own_chrome_block = border_edges(style).block_start + edges.padding_block_start;
    const int resume_line = fragmented ? std::max(0, frag.incoming.line_index) : 0;
    if (fragmented && frag.remaining_block <= 0.01f) {
        // No room at all: nothing of this box goes in (the fragmentainer pushes it whole to the
        // next fragment). An empty fragment makes no progress, so the container only ever asks this
        // way when the fragment already holds something.
        walk.fragment_full = true;
        walk.fragment_line = 0;
        walk.content_block = 0;
        walk.all_through = true;
        if (outside_marker != nullptr) {
            place_outside_marker(*outside_marker, outside_marker_result, edges, 0.f, walk);
        }
        return walk;
    }
    // Percentages on the children (`position: relative` offsets among them) measure against this
    // box's content box, the containing block of an in-flow child (CSS 2.1 section 10.1).
    pieces_zone.close();
    run_line_wrap(box, key, cx, edges, content_inline, inline_known, flow_inline, bfc,
                         box_origin, exclusion, pieces, outside_marker, outside_marker_result, walk,
                         lines, y, line, line_width, line_start, line_source_chars,
                         pending_break_rect, collapse, can_wrap, indent, frag, fragmented,
                         frag_limit, frag_own_start, own_chrome_block, resume_line, style, em,
                         block_percent);
    return walk;
}

}  // namespace flow_detail
}  // namespace layout
}  // namespace zui
