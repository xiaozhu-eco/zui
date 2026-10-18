// The inline piece builder: turn an inline formatting context's in-flow children into
// `Piece`s for the line walk (CSS 2.1 section 9.4). Floats, out-of-flow boxes and `::marker`
// are recorded where they sit rather than emitted as text.

#include "layout/baseline.h"
#include "layout/flow/detail.h"
#include "trace/trace.h"

#include <cmath>

namespace zui {
namespace layout {
namespace flow_detail {

PieceBuild build_pieces(Box& box, const LayoutKey& key, LayoutContext& cx,
                        const UsedEdges& edges, float content_inline, bool inline_known,
                        float flow_inline,
                        const std::shared_ptr<const ExclusionSpace>& exclusion,
                        const AxisSize& block_available, const AxisSize& block_percent,
                        const std::vector<Box*>* reask, Walk& walk,
                        const LayoutStyle& style) {
    std::vector<Piece> pieces;
    Box* outside_marker = nullptr;
    const LayoutResult* outside_marker_result = nullptr;
    for (Box* child : box.children) {
        if (child == nullptr) {
            continue;
        }
        const LayoutStyle& child_style = style_of(*child);
        if (is_outside_marker(*child, style)) {
            AxisSize available{inline_known, std::max(0.f, flow_inline)};
            {
                outside_marker_result =
                    ask_child(cx, *child,
                              child_space(key, edges, content_inline, inline_known, available,
                                          shared_exclusion(nullptr), block_available, block_percent,
                                          {}, style.writing_mode, style.direction),
                              walk, reask);
            }
            outside_marker = child;
            continue;
        }
        if (child->node != nullptr && child->node->tag == "br") {
            // HTML br is a forced line break, not a character of the parent's text. Its box
            // sits at the end of the line it ends, with the strut height and no advance.
            AxisSize available{inline_known, std::max(0.f, flow_inline)};
            const LayoutResult* result = nullptr;
            {
                result = ask_child(cx, *child,
                                   child_space(key, edges, content_inline, inline_known, available,
                                               shared_exclusion(nullptr), block_available,
                                               block_percent, {}, style.writing_mode,
                                               style.direction),
                                   walk, reask);
            }
            Piece piece;
            piece.glyph.forced_break = true;
            piece.box = child;
            piece.result = result;
            piece.atomic = true;
            piece.advance = 0;
            piece.block = result != nullptr ? result->geometry.border_box_block : 0;
            piece.source_chars = 0;
            pieces.push_back(piece);
            continue;
        }
        if (box.node != nullptr && html::is_select(box.node) && select_source_whitespace(*child)) {
            continue;
        }
        if (out_of_flow(child_style)) {
            // The static position is the line cursor at the moment this box would have
            // been placed, not the start of the block. Recording it here, before the line
            // exists, put every absolute inline at the padding edge, so the second radio
            // in example/tests/style sat on top of the first chip instead of after it.
            Piece piece;
            piece.abs_static = true;
            piece.atomic = true;
            piece.box = child;
            piece.advance = 0;
            pieces.push_back(piece);
            child->placement = {};
            continue;
        }
        if (child_style.floating) {
            AxisSize available{inline_known, std::max(0.f, flow_inline)};
            const LayoutResult* result = nullptr;
            {
                result = ask_child(cx, *child,
                                   child_space(key, edges, content_inline, inline_known, available,
                                               shared_exclusion(nullptr), block_available,
                                               block_percent, {}, style.writing_mode,
                                               style.direction),
                                   walk, reask);
            }
            if (result == nullptr) {
                continue;
            }
            Piece piece;
            piece.is_float = true;
            piece.box = child;
            piece.result = result;
            piece.float_side = child_style.float_right ? FloatSide::Right : FloatSide::Left;
            piece.clear_side = child_style.clear_side;
            piece.advance = result->geometry.border_box_inline;
            piece.block = result->geometry.border_box_block;
            piece.source_chars = subtree_text_chars(*child);
            pieces.push_back(piece);
            continue;
        }
        AxisSize available{inline_known, std::max(0.f, flow_inline)};
        const LayoutResult* result = nullptr;
        {
            result = ask_child(
                cx, *child,
                child_space(key, edges, content_inline, inline_known, available, exclusion,
                            block_available, block_percent, {}, style.writing_mode,
                            style.direction),
                walk, reask);
        }
        if (child->role == BoxRole::Text) {
            box.depends_on_font = true;
            const std::size_t before = pieces.size();
            const LayoutContext::FontEm text_em = cx.font_em(child_style);
            append_glyphs(pieces, result, child_style, text_em);
            const float spacing = child_style.letter_spacing;
            const float shift = align_shift(style, child_style, cx);
            const bool no_wrap = child_style.white_space == WhiteSpace::Nowrap ||
                                 child_style.white_space == WhiteSpace::Pre;
            for (std::size_t i = before; i < pieces.size(); ++i) {
                if (no_wrap && !pieces[i].glyph.forced_break) {
                    pieces[i].glyph.wrap_after = false;
                }
                pieces[i].align = child_style.vertical_align;
                pieces[i].align_shift = shift;
                pieces[i].block = text_em.ascent + text_em.descent;
                // A direct text child usually has no node of its own, so the run paints with the
                // block. A text control's placeholder is the exception: the run carries the
                // generated element's style, and without this the glyphs are drawn in the input's
                // colour and weight.
                if (child->node != nullptr && child->node->computed) {
                    pieces[i].style_box = child;
                }
                // A table cell's vertical-align positions the whole cell in the row. The
                // text box copies the cell's style, so leaving it as `middle` would shift
                // the glyphs inside the line and then again with the cell.
                if (style.table_part == TablePart::Cell && child->role == BoxRole::Text) {
                    pieces[i].align = VerticalAlign::Baseline;
                    pieces[i].align_shift = 0.f;
                }
            }
            // Prefer visual-order characters carried on ShapedRun.text (bidi L2).
            if (result != nullptr && result->lines != nullptr) {
                std::size_t piece_i = before;
                for (const LineBox& line_box : result->lines->lines) {
                    for (const ShapedRun& shaped : line_box.runs) {
                        std::size_t byte = 0;
                        // Pair glyphs with characters one for one. A forced-break glyph paints
                        // nothing and carries no character (shape() leaves it out of run.text),
                        // so walking run.text per piece instead of per glyph shifted every
                        // following piece by one and silently dropped the character after a
                        // collapsed newline.
                        for (const GlyphInline& glyph : shaped.glyphs) {
                            if (piece_i >= pieces.size()) {
                                break;
                            }
                            if (glyph.forced_break || byte >= shaped.text.size()) {
                                pieces[piece_i].chars.clear();
                                pieces[piece_i].source_chars = 1;
                            } else {
                                const std::size_t start = byte;
                                text::next_cp(shaped.text, byte);
                                pieces[piece_i].chars = shaped.text.substr(start, byte - start);
                                pieces[piece_i].source_chars =
                                    codepoint_count(pieces[piece_i].chars);
                            }
                            ++piece_i;
                        }
                    }
                }
            } else {
                std::size_t byte = 0;
                for (std::size_t i = before; i < pieces.size() && byte < child->text.size(); ++i) {
                    const std::size_t start = byte;
                    text::next_cp(child->text, byte);
                    pieces[i].chars = child->text.substr(start, byte - start);
                    pieces[i].source_chars = codepoint_count(pieces[i].chars);
                }
            }
            for (std::size_t i = before; i < pieces.size(); ++i) {
                // Same rule as append_text_pieces: Chrome adds the spacing after the last
                // character too, so a following sibling starts one spacing further right.
                if (spacing != 0.f && !pieces[i].glyph.forced_break &&
                    (i + 1 >= pieces.size() || !pieces[i + 1].glyph.forced_break)) {
                    pieces[i].advance += spacing;
                    pieces[i].glyph.advance += spacing;
                }
            }
            apply_word_spacing(pieces, before, child_style.word_spacing);
            continue;
        }
        Piece piece;
        piece.atomic = true;
        piece.box = child;
        piece.result = result;
        piece.advance = result != nullptr ? result->geometry.border_box_inline +
                                                result->geometry.margin.inline_start +
                                                result->geometry.margin.inline_end
                                          : 0;
        // An inline-block's margin box is what the line grows by. Leaving the vertical
        // margin out put the next block under the button and the click landed on the label.
        piece.block = result != nullptr ? result->geometry.border_box_block +
                                              result->geometry.margin.block_start +
                                              result->geometry.margin.block_end
                                        : 0;
        // A `display: inline` box is not atomic: it puts its own line-height box into this line
        // (CSS 2.1 section 10.8) while what it *reports* is its font content area. Build the piece
        // the way a text piece is built - its own font metrics - and let the child's layout report
        // the content area for the trace and the painter.
        const bool inline_box = child_style.outer == DisplayOuter::Inline &&
                                !child->atomic_inline && !child->is_flex_item &&
                                !child->is_grid_item && !out_of_flow(child_style) &&
                                !child_style.floating && child_style.inner == DisplayInner::Flow;
        if (inline_box) {
            child->inline_text_owned_by_parent = false;
            // CSS 2.1 section 9.4.2: the block container breaks the text. A span that wrapped
            // on its own leaves the tail of the sentence on the following line, on top of the
            // next atomic inline (the drawer Close button beside "run together."). A
            // zero-chrome inline (`<mark>`, a highlight span) must join this IFC too:
            // as one piece its advance is the whole selected run, and a short prefix in
            // front of it wraps onto its own line.
            if (should_absorb_inline(*child, child_style, result, box)) {
                absorb_wrapped_inline(pieces, box, *child, style, cx);
                continue;
            }
            const LayoutContext::FontEm child_em = cx.font_em(child_style);
            piece.atomic = false;
            piece.ascent = child_em.ascent;
            piece.descent = child_em.descent;
            piece.line_height = used_line_height(child_style, child_em);
            piece.block = child_em.ascent + child_em.descent;
            piece.align = child_style.vertical_align;
            piece.align_shift = align_shift(style, child_style, cx);
            // Generated markers/pseudos paint here but do not belong to the element's Range.
            piece.source_chars =
                child->generated != html::PseudoElement::Count ? 0 : subtree_text_chars(*child);
            pieces.push_back(piece);
            continue;
        }
        if (result != nullptr) {
            // A single-line text input centres its inner text. The value is already measured from
            // the border edge; the line adds the margin through piece.block, so it is not added
            // again here. Every other atomic uses its own baseline, and a missing one falls back
            // to the bottom margin edge (CSS 2.1 section 10.8.3).
            const float text_base =
                text_input_baseline(*child, child_style, *result, cx.font_em(child_style));
            if (std::isfinite(text_base)) {
                piece.baseline = text_base;
            } else {
                const float base = atomic_inline_baseline(*child, child_style, *result);
                piece.baseline =
                    std::isfinite(base) ? base + result->geometry.margin.block_start : piece.block;
            }
        } else {
            piece.baseline = piece.block;
        }
        piece.align = child_style.vertical_align;
        piece.align_shift = align_shift(style, child_style, cx);
        piece.source_chars =
            child->generated != html::PseudoElement::Count ? 0 : subtree_text_chars(*child);
        pieces.push_back(piece);
    }

    PieceBuild out;
    out.pieces = std::move(pieces);
    out.outside_marker = outside_marker;
    out.outside_marker_result = outside_marker_result;
    return out;
}

}  // namespace flow_detail
}  // namespace layout
}  // namespace zui
