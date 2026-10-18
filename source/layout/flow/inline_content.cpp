// Inline-piece assembly for the line walk: flatten a text box (including wrapped nested inline
// boxes) into the Piece list the line breaker consumes.

#include "layout/baseline.h"
#include "layout/box_role.h"
#include "layout/cache/box_geometry.h"
#include "layout/flow/detail.h"
#include "trace/trace.h"

#include <cmath>
#include <string>

namespace zui {
namespace layout {
namespace flow_detail {
bool plain_inline(const Box& box, const LayoutStyle& style) {
    return style.outer == DisplayOuter::Inline && !box.atomic_inline && !box.is_flex_item &&
           !box.is_grid_item && !out_of_flow(style) && !style.floating &&
           style.inner == DisplayInner::Flow;
}

bool should_absorb_inline(const Box& box, const LayoutStyle& style, const LayoutResult* result,
                          const Box& parent) {
    if (!plain_inline(box, style)) {
        return false;
    }
    const bool parent_inline = style_of(parent).outer == DisplayOuter::Inline;
    if (result != nullptr && result->lines != nullptr && result->lines->lines.size() > 1) {
        // A wrapped inline with a following sibling must join the parent IFC, or its
        // second line paints on top of that sibling (the drawer Close button). A sole
        // child of a block (zmd's body span) stays one piece so the block does not
        // re-wrap the span's glyphs to the column and grow a second line box.
        if (parent_inline) {
            return true;
        }
        for (const Box* sibling : parent.children) {
            if (sibling == nullptr || sibling == &box) {
                continue;
            }
            if (sibling->role == BoxRole::Text &&
                sibling->text.find_first_not_of(" \t\n\r") == std::string::npos) {
                continue;
            }
            return true;
        }
        return false;
    }
    if (!parent_inline) {
        return false;
    }
    if (result == nullptr) {
        return true;
    }
    // Padding or margin on the inline axis is the zmd `<code>` chip: one piece, so wrap
    // moves the chip as a unit and the following glyph starts at its border edge.
    const BoxGeometry& g = result->geometry;
    const float chrome =
        g.padding.inline_start + g.padding.inline_end + g.margin.inline_start + g.margin.inline_end;
    return chrome <= 0.01f;
}

// Glyphs of a text node, stamped with the inline element that carries their font and color.
void append_text_pieces(std::vector<Piece>& pieces, Box& block, Box& text_box,
                        const LayoutResult* result, const LayoutStyle& block_style,
                        const LayoutStyle& text_style, LayoutContext& cx, Box* face) {
    block.depends_on_font = true;
    const std::size_t before = pieces.size();
    const LayoutContext::FontEm text_em = cx.font_em(text_style);
    append_glyphs(pieces, result, text_style, text_em);
    const float spacing = text_style.letter_spacing;
    const float shift = align_shift(block_style, text_style, cx);
    const bool no_wrap =
        text_style.white_space == WhiteSpace::Nowrap || text_style.white_space == WhiteSpace::Pre;
    for (std::size_t i = before; i < pieces.size(); ++i) {
        if (no_wrap && !pieces[i].glyph.forced_break) {
            pieces[i].glyph.wrap_after = false;
        }
        pieces[i].align = text_style.vertical_align;
        pieces[i].align_shift = shift;
        pieces[i].block = text_em.ascent + text_em.descent;
        pieces[i].style_box = face;
        if (block_style.table_part == TablePart::Cell) {
            pieces[i].align = VerticalAlign::Baseline;
            pieces[i].align_shift = 0.f;
        }
    }
    // Chrome Ranges cover DOM text only. Generated faces (::marker / ::before / ::after) still
    // paint in this IFC, but must not advance the element's line-break character cursor.
    const bool generated_face = face != nullptr && face->generated != html::PseudoElement::Count;
    if (result != nullptr && result->lines != nullptr) {
        std::size_t piece_i = before;
        for (const LineBox& line_box : result->lines->lines) {
            for (const ShapedRun& shaped : line_box.runs) {
                std::size_t byte = 0;
                for (const GlyphInline& glyph : shaped.glyphs) {
                    if (piece_i >= pieces.size()) {
                        break;
                    }
                    if (glyph.forced_break || byte >= shaped.text.size()) {
                        pieces[piece_i].chars.clear();
                        pieces[piece_i].source_chars = generated_face ? 0 : 1;
                    } else {
                        const std::size_t start = byte;
                        text::next_cp(shaped.text, byte);
                        pieces[piece_i].chars = shaped.text.substr(start, byte - start);
                        pieces[piece_i].source_chars =
                            generated_face ? 0 : codepoint_count(pieces[piece_i].chars);
                    }
                    ++piece_i;
                }
            }
        }
    } else {
        std::size_t byte = 0;
        for (std::size_t i = before; i < pieces.size() && byte < text_box.text.size(); ++i) {
            const std::size_t start = byte;
            text::next_cp(text_box.text, byte);
            pieces[i].chars = text_box.text.substr(start, byte - start);
            pieces[i].source_chars = generated_face ? 0 : codepoint_count(pieces[i].chars);
        }
    }
    for (std::size_t i = before; i < pieces.size(); ++i) {
        // Chrome adds the spacing after the last character too, so the piece that ends the run
        // carries it as well; otherwise everything after a letter-spaced run sits one step left.
        if (spacing != 0.f && !pieces[i].glyph.forced_break &&
            (i + 1 >= pieces.size() || !pieces[i + 1].glyph.forced_break)) {
            pieces[i].advance += spacing;
            pieces[i].glyph.advance += spacing;
        }
    }
    apply_word_spacing(pieces, before, text_style.word_spacing);
}

// A non-replaced inline box does not break its own lines. Its text joins the ancestor's
// inline formatting context, so a following button stays after the whole sentence instead
// of sitting on the line the span's second fragment overflows into.
void absorb_wrapped_inline(std::vector<Piece>& pieces, Box& block, Box& inline_el,
                           const LayoutStyle& block_style, LayoutContext& cx) {
    inline_el.inline_text_owned_by_parent = true;
    for (Box* grand : inline_el.children) {
        if (grand == nullptr) {
            continue;
        }
        const LayoutStyle& gs = style_of(*grand);
        const LayoutResult* gr = grand->cache != nullptr ? grand->cache->pinned_final() : nullptr;
        if (grand->role == BoxRole::Text) {
            Box* face = grand->parent != nullptr && grand->parent->style_ptr != nullptr
                            ? grand->parent
                            : &inline_el;
            append_text_pieces(pieces, block, *grand, gr, block_style, gs, cx, face);
            continue;
        }
        if (should_absorb_inline(*grand, gs, gr, inline_el)) {
            absorb_wrapped_inline(pieces, block, *grand, block_style, cx);
            continue;
        }
        Piece piece;
        piece.box = grand;
        piece.result = gr;
        piece.advance = gr != nullptr
                            ? gr->geometry.border_box_inline + gr->geometry.margin.inline_start +
                                  gr->geometry.margin.inline_end
                            : 0;
        piece.block = gr != nullptr
                          ? gr->geometry.border_box_block + gr->geometry.margin.block_start +
                                gr->geometry.margin.block_end
                          : 0;
        if (plain_inline(*grand, gs)) {
            grand->inline_text_owned_by_parent = false;
            const LayoutContext::FontEm child_em = cx.font_em(gs);
            piece.atomic = false;
            piece.ascent = child_em.ascent;
            piece.descent = child_em.descent;
            piece.line_height = used_line_height(gs, child_em);
            piece.block = child_em.ascent + child_em.descent;
        } else {
            piece.atomic = true;
            if (gr != nullptr) {
                // Same border-relative baseline as the inline-piece path. A text control must not
                // fall back to its bottom edge: the reader's `.finder` came out 42 tall against
                // the browser's 36 when `input#shelf_query` did. Other atomics use their own
                // baseline, or the bottom margin edge when they have none.
                const float text_base = text_input_baseline(*grand, gs, *gr, cx.font_em(gs));
                if (std::isfinite(text_base)) {
                    piece.baseline = text_base;
                } else {
                    const float base = atomic_inline_baseline(*grand, gs, *gr);
                    piece.baseline = std::isfinite(base) ? base + gr->geometry.margin.block_start
                                                         : piece.block;
                }
            } else {
                piece.baseline = piece.block;
            }
        }
        piece.align = gs.vertical_align;
        piece.align_shift = align_shift(block_style, gs, cx);
        piece.source_chars = (inline_el.generated != html::PseudoElement::Count ||
                              grand->generated != html::PseudoElement::Count)
                                 ? 0
                                 : subtree_text_chars(*grand);
        pieces.push_back(piece);
    }
}

}  // namespace flow_detail
}  // namespace layout
}  // namespace zui
