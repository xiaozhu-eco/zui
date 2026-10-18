// Sticky positioning: writing the sticky offset of a box and its descendants.

#include "layout/flow/detail.h"

namespace zui {
namespace layout {
namespace flow_detail {

bool sticky_scrolls(const LayoutStyle& style) {
    auto scrollable = [](OverflowMode mode) {
        return mode == OverflowMode::Auto || mode == OverflowMode::Scroll ||
               mode == OverflowMode::Hidden;
    };
    return scrollable(style.overflow_x) || scrollable(style.overflow_y);
}

void write_sticky_descendants(Box& scroller, Box& node, float cb_inline, float cb_block,
                              float scroll_x, float scroll_y)
{
    if (node.style_ptr != nullptr && node.style().position_sticky && !out_of_flow(node.style())) {
        Box* nearest = nullptr;
        for (Box* p = node.parent; p != nullptr; p = p->parent) {
            if (p->style_ptr != nullptr && sticky_scrolls(p->style())) {
                nearest = p;
                break;
            }
        }
        if (nearest == &scroller) {
            float child_inline = 0;
            float child_block = 0;
            if (node.cache != nullptr && node.cache->pinned_final() != nullptr) {
                child_inline = node.cache->pinned_final()->geometry.border_box_inline;
                child_block = node.cache->pinned_final()->geometry.border_box_block;
            }
            LogicalPoint origin = node.placement.cb_offset;
            for (Box* p = node.parent; p != nullptr && p != &scroller; p = p->parent) {
                origin.inline_offset += p->placement.cb_offset.inline_offset;
                origin.block_offset += p->placement.cb_offset.block_offset;
            }
            LogicalPoint cb_origin{};
            float containing_inline = cb_inline;
            float containing_block = cb_block;
            if (node.parent != nullptr && node.parent != &scroller) {
                cb_origin = node.parent->placement.cb_offset;
                for (Box* p = node.parent->parent; p != nullptr && p != &scroller; p = p->parent) {
                    cb_origin.inline_offset += p->placement.cb_offset.inline_offset;
                    cb_origin.block_offset += p->placement.cb_offset.block_offset;
                }
                if (node.parent->cache != nullptr && node.parent->cache->pinned_final() != nullptr) {
                    containing_inline =
                        node.parent->cache->pinned_final()->geometry.border_box_inline;
                    containing_block =
                        node.parent->cache->pinned_final()->geometry.border_box_block;
                }
            }
            node.placement.sticky_offset =
                sticky_shift(node.style(), origin, child_inline, child_block, cb_inline, cb_block,
                             scroll_x, scroll_y, cb_origin, containing_inline, containing_block);
        }
    }
    for (Box* child : node.children) {
        if (child == nullptr) {
            continue;
        }
        if (child->style_ptr != nullptr && sticky_scrolls(child->style())) {
            continue;
        }
        write_sticky_descendants(scroller, *child, cb_inline, cb_block, scroll_x, scroll_y);
    }
}

void write_sticky(Box& box, const LayoutStyle& style, const LayoutResult& out)
{
    if (style.overflow_x == OverflowMode::Visible && style.overflow_y == OverflowMode::Visible) {
        return;
    }
    if (box.node == nullptr) {
        return;
    }
    const float cb_inline = out.geometry.border_box_inline - out.geometry.border.inline_start - out.geometry.border.inline_end;
    const float cb_block = out.geometry.border_box_block - out.geometry.border.block_start - out.geometry.border.block_end;
    for (Box* child : box.children) {
        if (child == nullptr) {
            continue;
        }
        if (child->style_ptr != nullptr && sticky_scrolls(child->style())) {
            continue;
        }
        const WritingMode mode = style.writing_mode;
        const float scroll_inline =
            is_horizontal_wm(mode) ? box.node->scroll_x : box.node->scroll_y;
        const float scroll_block =
            is_horizontal_wm(mode) ? box.node->scroll_y : box.node->scroll_x;
        write_sticky_descendants(box, *child, cb_inline, cb_block, scroll_inline, scroll_block);
    }
}

}  // namespace flow_detail
}  // namespace layout
}  // namespace zui
