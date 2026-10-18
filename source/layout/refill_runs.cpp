// Wave 1 of docs/RFC_MODEL.md: a kept model row keeps its boxes, and the placement walk clears
// every box's `geo.runs`. This rebuilds the run of a box whose characters did not change and
// whose whole content is one line.
//
// It is not a second layout. `can_refill_runs` accepts only the shape `flow/atomic.cpp`'s
// standalone-text path laid out - a leaf text box, not painted by an ancestor's line - and the
// run it writes takes its metrics from the same `LayoutContext` and its origin from the same
// logical-to-physical conversion `paint_map.cpp`'s `fill_geometry` uses.

#include "layout/refill_runs.h"

#include "layout/box.h"
#include "layout/cache/layout_context.h"
#include "layout/engine.h"
#include "layout/flow/detail.h"
#include "layout/inline/shape.h"
#include "layout/paint_map_internal.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace zui {
namespace layout {

namespace {

/// The parent's line owns this box's glyphs (`fill_geometry` returns before writing a run for it).
bool parent_paints_text(const Box& box) {
    if (box.role != BoxRole::Text || box.parent == nullptr) {
        return false;
    }
    const LayoutResult* parent = paint_map_detail::result_of(*box.parent);
    return parent != nullptr && parent->lines != nullptr && !parent->lines->lines.empty();
}

bool is_generated(const Box& box) {
    return box.node != nullptr && box.node->tag.rfind("::", 0) == 0;
}

bool wraps(WhiteSpace white_space) {
    return white_space == WhiteSpace::Normal || white_space == WhiteSpace::PreWrap ||
           white_space == WhiteSpace::PreLine || white_space == WhiteSpace::BreakSpaces;
}

bool preserves_breaks(WhiteSpace white_space) {
    return white_space == WhiteSpace::Pre || white_space == WhiteSpace::PreWrap ||
           white_space == WhiteSpace::PreLine || white_space == WhiteSpace::BreakSpaces;
}

}  // namespace

bool can_refill_runs(const Box& box, LayoutContext& cx) {
    // 1. Placement cleared the runs, but the geometry it wrote is still there. A box whose pinned
    //    result was missing had width and height zeroed too, and is left to the next real layout.
    if (!box.geo.runs.empty() || box.geo.width <= 0.f || box.geo.height <= 0.f) {
        return false;
    }
    // 2. The ancestor's line paints these glyphs; an empty run here is what keeps them painted once.
    if (parent_paints_text(box)) {
        return false;
    }
    // 3. A non-replaced inline box whose text the ancestor inline formatting context broke across
    //    its own line boxes.
    if (box.inline_text_owned_by_parent) {
        return false;
    }
    // 4. The box's characters are one string and it has nothing else: a leaf with non-empty text.
    //    Mixed content keeps a `#text` child (`loader.cpp`'s `convert_children`), generated content
    //    is a pseudo child box, and a `::placeholder` run is generated - none of them is one line
    //    of this box's own text.
    if (!box.children.empty() || box.text.empty() || is_generated(box)) {
        return false;
    }
    const LayoutStyle& style = box.style_ptr != nullptr ? box.style() : style_or_default(box);
    // 5. One line at this box's own metric. `shape` gives no break opportunities and no per-line
    //    block offset, so spacing that the line breaker would have added, a forced break, and text
    //    that does not fit the content box all leave the box to the next real layout.
    if (style.letter_spacing != 0.f || style.word_spacing != 0.f ||
        style.text_align == TextAlign::Justify) {
        return false;
    }
    if (preserves_breaks(style.white_space) && box.text.find('\n') != std::string::npos) {
        return false;
    }
    if (!wraps(style.white_space)) {
        return true;
    }
    float advance = 0.f;
    for (const ShapedRun& run : cx.shape(box.text, style, style.direction)) {
        advance += run.advance_inline;
    }
    const float content_inline =
        is_horizontal_wm(style.writing_mode) ? box.geo.content_width : box.geo.content_height;
    return advance <= content_inline + 0.01f;
}

std::size_t refill_runs(Box& box, LayoutContext& cx) {
    const LayoutResult* result = paint_map_detail::result_of(box);
    if (result == nullptr) {
        return 0;
    }
    const LayoutStyle& style = box.style_ptr != nullptr ? box.style() : style_or_default(box);
    const WritingMode mode = style.writing_mode;
    const Direction direction = style.direction;
    const LayoutContext::FontEm em = cx.font_em(style);
    const float font_box = std::max(0.f, em.ascent + em.descent);
    // `flow/atomic.cpp`'s standalone-text path: a text that *is* the flex or grid item is
    // blockified, so its border box is the used line box (CSS Flexbox section 4). Everything else
    // keeps the font box.
    float baseline = em.ascent;
    float line_block = font_box;
    if (box.is_flex_item || box.is_grid_item) {
        const float strut = flow_detail::used_line_height(style, em);
        const float half_leading = std::floor((strut - font_box) * 0.5f);
        baseline = em.ascent + half_leading;
        line_block = strut;
    }
    const BoxGeometry& geometry = result->geometry;
    std::vector<ShapedRun> runs = cx.shape(box.text, style, direction);
    // `shape` reports advances, not positions. This box is one line, so the runs follow one
    // another from the content-box inline origin - the same placement `atomic.cpp` writes.
    float cursor = 0.f;
    std::size_t written = 0;
    for (ShapedRun& shaped : runs) {
        const float origin_inline = cursor;
        cursor += shaped.advance_inline +
                  style.letter_spacing * static_cast<float>(shaped.glyphs.size());
        if (shaped.text.empty()) {
            continue;
        }
        const float logical_i =
            geometry.border.inline_start + geometry.padding.inline_start + origin_inline;
        const float logical_b = geometry.border.block_start + geometry.padding.block_start;
        const PhysicalPoint origin =
            logical_point_to_physical(mode, direction, logical_i, logical_b,
                                      geometry.border_box_inline, geometry.border_box_block);
        TextRun run;
        run.x = origin.x;
        run.y = origin.y;
        run.ascent = baseline;
        run.font_ascent = em.ascent;
        run.font_descent = em.descent;
        run.line_block = line_block;
        run.text = shaped.text;
        // A refilled box has no element of its own to carry the style (an element's own text run
        // has `node == nullptr`), so paint falls back to this box, the way `fill_geometry` leaves it.
        run.style_box = nullptr;
        run.byte_begin = 0;
        bool shaped_glyph = false;
        run.glyph_advances.reserve(shaped.glyphs.size());
        for (const GlyphInline& g : shaped.glyphs) {
            run.glyph_advances.push_back(g.advance);
            if (g.paint_glyph != 0) {
                shaped_glyph = true;
            }
        }
        if (shaped_glyph) {
            run.glyph_ids.reserve(shaped.glyphs.size());
            for (const GlyphInline& g : shaped.glyphs) {
                run.glyph_ids.push_back(g.paint_glyph);
            }
        }
        // Selection's byte-to-x mapping needs the shaped string to be the node's own text: a
        // whitespace fold or a bidi reorder breaks it. The run is still painted either way.
        const html::Node* owning = box.node;
        run.mappable = owning != nullptr && shaped.text == owning->text;
        box.geo.runs.push_back(std::move(run));
        ++written;
    }
    return written;
}

std::size_t LayoutEngine::refill_runs(html::Node* host) {
    Box* root = host != nullptr ? principal_box(host) : tree_.root();
    if (root == nullptr) {
        return 0;
    }
    std::size_t written = 0;
    std::vector<Box*> stack{root};
    while (!stack.empty()) {
        Box* box = stack.back();
        stack.pop_back();
        if (box == nullptr) {
            continue;
        }
        if (can_refill_runs(*box, cache_)) {
            // Qualified: the member of the same name hides the free function inside this scope.
            written += ::zui::layout::refill_runs(*box, cache_);
        }
        for (Box* child : box->children) {
            stack.push_back(child);
        }
    }
    return written;
}

}  // namespace layout
}  // namespace zui
