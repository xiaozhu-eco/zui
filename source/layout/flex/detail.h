#ifndef ZUI_SOURCE_LAYOUT_FLEX_DETAIL_H
#define ZUI_SOURCE_LAYOUT_FLEX_DETAIL_H

// Flex internals shared by the pass phase translation units (detail.cpp holds the helpers): the
// item box state and the main/cross distribution helpers.

#include "layout/box.h"
#include "layout/cache/layout_result.h"
#include "layout/layout_style.h"

#include <vector>

namespace zui {
namespace layout {
namespace flex_detail {

const LayoutStyle& style_of(const Box& box);
bool row_direction(FlexDirection direction);
bool reverse_main(FlexDirection direction);
float length_or_percent(float value, bool percent, float basis, bool basis_known);
float border_from_specified(float specified, bool border_box, float chrome);
struct Item {
    Box* box = nullptr;
    float base = 0;
    float hypo = 0;
    float min_size = 0;
    // Min-content border-box on the main/cross axes from a MinContent ask (before auto-min
    // clamps). Used for this container's own MinContent contribution.
    float content_min_main = 0;
    float content_min_cross = 0;
    bool main_size_auto = true;
    float main_specified_border = 0;  // preferred main size as border box when definite
    bool has_max = false;
    float max_size = 0;
    float grow = 0;
    float shrink = 1;
    float margin_main_start = 0;
    float margin_main_end = 0;
    float margin_cross_start = 0;
    float margin_cross_end = 0;
    /// `margin: auto` on that edge (CSS Flexbox section 8.1). The specified margin stays 0: an
    /// auto margin is 0 while the flexible lengths are solved, and `available` has to keep
    /// reading `margin_main_*` (the item's Final ask fills whatever available size it is handed,
    /// so an auto margin leaked into it would stretch the item).
    bool main_start_auto = false;
    bool main_end_auto = false;
    bool cross_start_auto = false;
    bool cross_end_auto = false;
    /// Used value of an auto margin after CSS Flexbox section 9.5 step 12 distributed the line's
    /// free space. Placement-only; `outer_main` / `available` must not see it.
    float auto_main_start = 0;
    float auto_main_end = 0;
    float auto_cross_start = 0;
    float auto_cross_end = 0;
    float cross = 0;
    /// Flex-base border box, saved before a min-content ask overwrites `cross`. The container
    /// compares it with the min-content border box to see whether a max-content ask is the same
    /// inline size.
    float flex_base_main = 0;
    float flex_base_cross = 0;
    bool cross_auto = true;
    bool frozen = false;
    float target = 0;
    bool asked_min_content = false;
    const LayoutResult* final_result = nullptr;
    float used_border = 0;
    float used_cross = 0;
};

float outer_main(const Item& item, float border);

/// Static position of an abspos child treated as the sole flex item (CSS Flexbox §4.1).
/// The point is the child's border box on the container's content edge. Auto margins are 0.
/// An axis whose size is unknown, or whose child border box is auto, stays at the margin start.
/// One item: space-between matches start; space-around and space-evenly match center.
/// Negative free space still anchors end and center.
LogicalPoint sole_abspos_static(const LayoutStyle& container, const LayoutStyle& child,
                                float inner_inline, bool inline_known, float inner_block,
                                bool block_known);
struct Pack {
    float leading = 0;
    float between = 0;
};

/// CSS Box Alignment packing for leftover free space (same rules Grid uses for tracks).
Pack pack_of(FlexPack pack, float free, int count);
void distribute(std::vector<Item*>& line, float inner, float gap);
FlexAlign used_align(const LayoutStyle& item, const LayoutStyle& container);
int auto_main_margins(const std::vector<Item*>& line);

}  // namespace flex_detail
}  // namespace layout
}  // namespace zui

#endif
