#ifndef ZUI_LAYOUT_TEXT_RUN_H
#define ZUI_LAYOUT_TEXT_RUN_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace zui {
namespace html {
struct Node;
}
namespace layout {

struct Box;

struct TextRun {
    float x = 0;
    float y = 0;
    float ascent = 0;
    /// The font content area of the line this run sits on. `y + ascent` is the baseline and
    /// `y + ascent - font_ascent` the top of the font box, which is the box a browser paints the
    /// caret over (CSS 2.1 section 10.8) - not the control's content box.
    float font_ascent = 0;
    float font_descent = 0;
    /// Used line-box block size. `::selection` fills this from `y` (the line-box start).
    /// A `line-height` taller than the font leaves a gap under an em-square highlight.
    float line_block = 0;
    std::string text;
    // Element whose font and color paint the run. Null uses the box that owns the run.
    const Box* style_box = nullptr;
    /// `#text` node whose `text` holds these bytes when the run is stamped with an ancestor
    /// element. A mixed inline keeps its characters on the text children, and that element's
    /// own `text` is empty. Null means the bytes live on `style_box->node` or the box node.
    const html::Node* text_node = nullptr;
    /// Byte offset of `text` into the owning node's `node->text` (`text_node` when set).
    /// Meaningful only when `mappable` is set.
    std::size_t byte_begin = 0;
    /// Per-glyph advances copied from shaping when the run maps 1:1 onto `node->text`.
    /// Empty means selection paint must skip this run (mapping missing / placeholder / bidi).
    std::vector<float> glyph_advances;
    /// Parallel to `glyph_advances` when this run contains a ligature. Empty means every
    /// codepoint paints its cmap glyph. `text::kSkipPaintGlyph` draws nothing. Any other value
    /// is the glyph id to paint from the face that codepoint maps to.
    std::vector<std::uint32_t> glyph_ids;
    /// Selection's byte→x mapping (`run_byte_x`, the ellipsis fit loop) needs the run's bytes to be
    /// the node's own text: no whitespace fold, no bidi reorder, and never a placeholder run.
    /// `glyph_advances` itself is filled for every run so paint can render from it.
    bool mappable = false;
};

}  // namespace layout
}  // namespace zui

#endif
