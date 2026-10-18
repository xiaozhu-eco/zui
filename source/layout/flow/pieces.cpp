// Inline pieces: per-glyph metrics, spacing and the min-content measurements the line walk
// and the inline-content builder share.

#include "layout/flow/detail.h"

namespace zui {
namespace layout {
namespace flow_detail {

std::size_t codepoint_count(const std::string& text)
{
    std::size_t count = 0;
    for (char c : text) {
        if ((static_cast<unsigned char>(c) & 0xC0u) != 0x80u) {
            ++count;
        }
    }
    return count;
}

/// Characters a browser would count for this box: the text of its whole subtree, because a traced
/// element's `lines.breaks` is numbered over all of its descendant text nodes.
std::size_t subtree_text_chars(const Box& box)
{
    std::size_t total = box.role == BoxRole::Text ? codepoint_count(box.text) : 0;
    for (const Box* child : box.children) {
        if (child != nullptr) {
            total += subtree_text_chars(*child);
        }
    }
    return total;
}

void append_glyphs(std::vector<Piece>& pieces, const LayoutResult* result,
                   const LayoutStyle& piece_style, const LayoutContext::FontEm& piece_em)
{
    if (result == nullptr || result->lines == nullptr) {
        return;
    }
    const float piece_line_height = used_line_height(piece_style, piece_em);
    for (const LineBox& line : result->lines->lines) {
        for (const ShapedRun& run : line.runs) {
            for (const GlyphInline& glyph : run.glyphs) {
                Piece piece;
                piece.glyph = glyph;
                piece.advance = glyph.advance;
                piece.space = glyph.collapsible;
                piece.ascent = piece_em.ascent;
                piece.descent = piece_em.descent;
                piece.line_height = piece_line_height;
                pieces.push_back(piece);
            }
        }
    }
}

void apply_word_spacing(std::vector<Piece>& pieces, std::size_t begin, float word_spacing) {
    if (word_spacing == 0.f) {
        return;
    }
    for (std::size_t i = begin; i < pieces.size(); ++i) {
        if (pieces[i].glyph.forced_break || pieces[i].chars.empty()) {
            continue;
        }
        std::size_t byte = 0;
        const std::uint32_t cp = text::next_cp(pieces[i].chars, byte);
        // CSS Text 3 §8.2 word-separator characters (Latin subset): SPACE and NBSP.
        if (cp == ' ' || cp == 0xA0) {
            pieces[i].advance += word_spacing;
            pieces[i].glyph.advance += word_spacing;
        }
    }
}

bool collapse_spaces(WhiteSpace white_space)
{
    return white_space == WhiteSpace::Normal || white_space == WhiteSpace::Nowrap ||
           white_space == WhiteSpace::PreLine;
}

bool wraps(WhiteSpace white_space)
{
    return white_space == WhiteSpace::Normal || white_space == WhiteSpace::PreWrap ||
           white_space == WhiteSpace::PreLine || white_space == WhiteSpace::BreakSpaces;
}

bool inline_context(const Box& box)
{
    bool any = false;
    for (Box* child : box.children) {
        if (child == nullptr) {
            continue;
        }
        const LayoutStyle& style = style_of(*child);
        if (out_of_flow(style) || style.floating) {
            continue;
        }
        any = true;
        if (!inline_level(*child)) {
            return false;
        }
    }
    return any;
}

float longest_unbreakable(const InlineContent* lines)
{
    float min_content = 0;
    float word = 0;
    if (lines == nullptr) {
        return 0;
    }
    for (const LineBox& line : lines->lines) {
        for (const ShapedRun& shaped : line.runs) {
            for (const GlyphInline& glyph : shaped.glyphs) {
                if (glyph.collapsible || glyph.forced_break) {
                    min_content = std::max(min_content, word);
                    word = 0;
                    continue;
                }
                word += glyph.advance;
                if (glyph.wrap_after) {
                    min_content = std::max(min_content, word);
                    word = 0;
                }
            }
        }
        min_content = std::max(min_content, word);
        word = 0;
    }
    return min_content;
}

// Border-box min-content, including this box's padding and border. Margins stay with the caller.
float content_min(const Box& box, const LayoutResult* result)
{
    if (result == nullptr) {
        return 0;
    }
    const LayoutStyle& style = style_of(box);
    if (box.is_replaced || !style.width_auto) {
        return result->geometry.border_box_inline;
    }
    // No soft wrap: the min-content inline size is the widest line, not the longest word, and
    // not a stretched border box a definite available size may have written over the text.
    if (style.white_space == WhiteSpace::Nowrap || style.white_space == WhiteSpace::Pre) {
        if (result->lines != nullptr && !result->lines->lines.empty()) {
            float widest = 0.f;
            for (const LineBox& line : result->lines->lines) {
                widest = std::max(widest, line.inline_size);
            }
            const float chrome = std::max(
                0.f, result->geometry.border_box_inline - result->geometry.content_inline);
            return widest + chrome;
        }
        return result->geometry.border_box_inline;
    }
    float inner = longest_unbreakable(result->lines.get());
    bool saw = result->lines != nullptr && !result->lines->lines.empty();
    for (Box* child : box.children) {
        if (child == nullptr || out_of_flow(style_of(*child)) || is_outside_marker(*child, style)) {
            continue;
        }
        const LayoutResult* child_result =
            child->cache != nullptr ? child->cache->pinned_final() : nullptr;
        if (child_result == nullptr) {
            continue;
        }
        const float margins =
            child_result->geometry.margin.inline_start + child_result->geometry.margin.inline_end;
        inner = std::max(inner, content_min(*child, child_result) + margins);
        saw = true;
    }
    if (!saw) {
        return result->geometry.border_box_inline;
    }
    const float chrome =
        std::max(0.f, result->geometry.border_box_inline - result->geometry.content_inline);
    return inner + chrome;
}

}  // namespace flow_detail
}  // namespace layout
}  // namespace zui
