// CSS 2.1 section 10.3.3 / 10.6.4: `margin: auto` on a box with a definite available size
// splits the leftover space. An over-constrained horizontal box solves margin-right in
// ltr and margin-left in rtl; that used margin may be negative.

#include "layout/box.h"
#include "layout/cache/layout_context.h"
#include "layout/flow/detail.h"
#include "trace/trace.h"

#include <algorithm>
#include <cmath>

namespace zui {
namespace layout {
namespace flow_detail {

void resolve_auto_margins(UsedEdges& edges, const LayoutStyle& style,
                          const LayoutKey& self_key, float content_inline,
                          float inline_chrome, float content_block, float block_chrome,
                          bool width_auto, bool block_auto, bool abspos, bool flex_item,
                          bool grid_item) {
    const bool inline_definite =
        self_key.space.available.inline_size.definite && !style.floating;
    const bool over_constrained =
        !width_auto && !style.margin_left_auto && !style.margin_right_auto;
    const bool horizontal_margins =
        is_horizontal_wm(style.writing_mode) &&
        (style.margin_left_auto || style.margin_right_auto || over_constrained);
    if (inline_definite && horizontal_margins) {
        // CSS 2.1 §10.3.3: under horizontal-tb the equation is physical left/right.
        float left = 0.f;
        float right = 0.f;
        if (!style.margin_left_auto) {
            left = style.direction == Direction::Rtl ? edges.margin_inline_end
                                                     : edges.margin_inline_start;
        }
        if (!style.margin_right_auto) {
            right = style.direction == Direction::Rtl ? edges.margin_inline_start
                                                      : edges.margin_inline_end;
        }
        const float leftover = self_key.space.available.inline_size.px -
                               (content_inline + inline_chrome) - left - right;
        const int autos = (style.margin_left_auto ? 1 : 0) + (style.margin_right_auto ? 1 : 0);
        const bool solves_over = style.outer == DisplayOuter::Block && !abspos && !flex_item &&
                                 !grid_item && style.table_part == TablePart::None;
        if (autos == 0) {
            if (solves_over) {
                if (style.direction == Direction::Rtl) {
                    left += leftover;
                } else {
                    right += leftover;
                }
            }
        } else if (width_auto || leftover <= 0.f) {
            if (style.margin_left_auto) {
                left = 0.f;
            }
            if (style.margin_right_auto) {
                right = 0.f;
            }
        } else {
            const float share = leftover / static_cast<float>(autos);
            if (style.margin_left_auto) {
                left = share;
            }
            if (style.margin_right_auto) {
                right = share;
            }
        }
        const Edges logical = physical_edges_to_logical(style.writing_mode, style.direction, 0.f,
                                                        right, 0.f, left);
        edges.margin_inline_start = logical.inline_start;
        edges.margin_inline_end = logical.inline_end;
        edges.margin_inline_start_auto = false;
        edges.margin_inline_end_auto = false;
    } else if (inline_definite &&
               (edges.margin_inline_start_auto || edges.margin_inline_end_auto)) {
            float specified = 0;
            if (!edges.margin_inline_start_auto) {
                specified += edges.margin_inline_start;
            }
            if (!edges.margin_inline_end_auto) {
                specified += edges.margin_inline_end;
            }
            const float leftover = self_key.space.available.inline_size.px -
                                   (content_inline + inline_chrome) - specified;
            const int autos =
                (edges.margin_inline_start_auto ? 1 : 0) + (edges.margin_inline_end_auto ? 1 : 0);
            if (width_auto || leftover <= 0.f) {
                if (edges.margin_inline_start_auto) {
                    edges.margin_inline_start = 0;
                }
                if (edges.margin_inline_end_auto) {
                    edges.margin_inline_end = 0;
                }
            } else {
                const float share = leftover / static_cast<float>(autos);
                if (edges.margin_inline_start_auto) {
                    edges.margin_inline_start = share;
                }
                if (edges.margin_inline_end_auto) {
                    edges.margin_inline_end = share;
                }
            }
    }
    if (abspos && self_key.space.available.block_size.definite && !block_auto &&
        (style.margin_top_auto || style.margin_bottom_auto)) {
        float specified = 0;
        if (!style.margin_top_auto) {
            specified += edges.margin_block_start;
        }
        if (!style.margin_bottom_auto) {
            specified += edges.margin_block_end;
        }
        const float leftover =
            self_key.space.available.block_size.px - (content_block + block_chrome) - specified;
        const int autos = (style.margin_top_auto ? 1 : 0) + (style.margin_bottom_auto ? 1 : 0);
        if (leftover <= 0.f) {
            if (style.margin_top_auto) {
                edges.margin_block_start = 0;
            }
            if (style.margin_bottom_auto) {
                edges.margin_block_end = 0;
            }
        } else {
            const float share = leftover / static_cast<float>(autos);
            if (style.margin_top_auto) {
                edges.margin_block_start = share;
            }
            if (style.margin_bottom_auto) {
                edges.margin_block_end = share;
            }
        }
    }
}

}  // namespace flow_detail
}  // namespace layout
}  // namespace zui
