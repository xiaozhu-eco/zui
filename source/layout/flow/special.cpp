// The terminal specializations of `layout_normal`: a text run, a `br`, a replaced element and
// a simple ruby container each own their whole box and return before the block/inline walk.

#include "layout/box.h"
#include "layout/cache/layout_context.h"
#include "layout/flow/detail.h"
#include "trace/trace.h"

#include <algorithm>
#include <cmath>

namespace zui {
namespace layout {
namespace flow_detail {

bool layout_normal_special(Box& box, const LayoutKey& self_key, LayoutContext& cx,
                           const UsedEdges& edges, const LayoutStyle& style,
                           LayoutResult& out) {
    if (box.role == BoxRole::Text) {
        zui::trace::Zone text_zone("flow.text", zui::trace::Category::LayoutPhase);
        layout_text(box, self_key, cx, out);
        if (self_key.space.request.kind == RequestKind::MinContent) {
            const float max_advance = out.geometry.content_inline;
            // `nowrap` and `pre` have no soft wrap, so the min-content inline size is the
            // whole run. Spaces still carry a wrap opportunity for `normal`, which is why
            // only that path reads the longest unbreakable word.
            const bool no_soft_wrap = style.white_space == WhiteSpace::Nowrap ||
                                      style.white_space == WhiteSpace::Pre;
            const float min_inline =
                no_soft_wrap ? max_advance : longest_unbreakable(out.lines.get());
            if (style.width_auto && !style.width_percent &&
                style.width_intrinsic == IntrinsicSize::None) {
                out.intrinsic_inline_equal = std::abs(min_inline - max_advance) <= 0.01f;
            }
            out.geometry.content_inline = min_inline;
            out.geometry.border_box_inline = min_inline;
            if (out.lines != nullptr && !out.lines->lines.empty()) {
                out.lines->lines.front().inline_size = min_inline;
            }
        } else if (self_key.space.request.kind == RequestKind::FlexBase && style.width_auto &&
                   !style.width_percent && style.width_intrinsic == IntrinsicSize::None) {
            // Flex base keeps the max-content advance. When that is also the min-content size the
            // item pass can skip the min ask; the geometry stays the advance either way.
            const bool no_soft_wrap = style.white_space == WhiteSpace::Nowrap ||
                                      style.white_space == WhiteSpace::Pre;
            const float min_inline = no_soft_wrap ? out.geometry.content_inline
                                                  : longest_unbreakable(out.lines.get());
            out.intrinsic_inline_equal =
                std::abs(min_inline - out.geometry.content_inline) <= 0.01f;
        }
        // A flex or grid item is handed the main size its container already solved
        // (LAYOUT section 3). The advance above is the max-content contribution.
        // Reporting it for a Final ask left the flex pass's target — the clamped
        // min-content, one glyph for CJK — disagreeing with this box, which is the
        // section 5 ceiling on the album label and the chat bubbles. MinContent and
        // FlexBase keep the intrinsic sizes; only the solved asks take the definite
        // available size. The glyph run stays at the advance so the text still paints.
        const RequestKind kind = self_key.space.request.kind;
        if ((box.is_flex_item || box.is_grid_item) &&
            (kind == RequestKind::Final || kind == RequestKind::FlexHypothetical)) {
            if (self_key.space.available.inline_size.definite) {
                const float border = std::max(0.f, self_key.space.available.inline_size.px -
                                                       edges.margin_inline_start -
                                                       edges.margin_inline_end);
                out.geometry.border_box_inline = border;
                out.geometry.content_inline = border;
            }
            if (self_key.space.available.block_size.definite) {
                const float border = std::max(0.f, self_key.space.available.block_size.px -
                                                       edges.margin_block_start -
                                                       edges.margin_block_end);
                out.geometry.border_box_block = border;
                out.geometry.content_block = border;
            }
        }
        out.margin_strut.start = edges.margin_block_start;
        out.margin_strut.end = edges.margin_block_end;
        return true;
    }
    if (box.node != nullptr && box.node->tag == "br") {
        // A br has no advance. Its border box is the strut of the line it ends, which is
        // what Chrome reports for the element's own rect.
        const LayoutContext::FontEm em = cx.font_em(style);
        const float block = std::max(0.f, em.ascent + em.descent);
        out.geometry.content_inline = 0;
        out.geometry.content_block = block;
        out.geometry.border_box_inline = 0;
        out.geometry.border_box_block = block;
        out.margin_strut.start = edges.margin_block_start;
        out.margin_strut.end = edges.margin_block_end;
        return true;
    }
    if (box.is_replaced) {
        layout_replaced(box, self_key, cx, out);
        out.margin_strut.start = edges.margin_block_start;
        out.margin_strut.end = edges.margin_block_end;
        out.margin_strut.blocks_margin_collapse = true;
        return true;
    }
    if (is_ruby_container(box)) {
        layout_ruby(box, self_key, cx, edges, out);
        out.margin_strut.start = edges.margin_block_start;
        out.margin_strut.end = edges.margin_block_end;
        out.margin_strut.blocks_margin_collapse = true;
        write_sticky(box, style, out);
        return true;
    }
    return false;
}

}  // namespace flow_detail
}  // namespace layout
}  // namespace zui
