#ifndef ZUI_SOURCE_LAYOUT_FLEX_PASS_H
#define ZUI_SOURCE_LAYOUT_FLEX_PASS_H

// One flex layout pass over a container. `run()` orchestrates the phases below; each phase lives
// in its own translation unit and shares the pass state through `State`.

#include "layout/box.h"
#include "layout/cache/break_token.h"
#include "layout/cache/fragmentation.h"
#include "layout/cache/layout_key.h"
#include "layout/cache/layout_result.h"
#include "layout/cache/logical_axes.h"
#include "layout/flex/detail.h"
#include "layout/layout_style.h"

#include <vector>

namespace zui {
namespace layout {

class LayoutContext;

struct FlexPassOutcome {
    LayoutResult layout;
    /// The container is a scroll container with a definite block size and its content does not
    /// fit, so the block scrollbar needs a gutter the item pass had to know about.
    bool block_bar_needed = false;
};

class FlexPass {
public:
    FlexPass(Box& box, const LayoutKey& key, LayoutContext& cx, bool reserve_block_bar);

    FlexPassOutcome run();

private:
    // Everything one pass computes, shared by the phases below. `measure_container` fills the
    // container fields, then each later phase reads them back and fills its own.
    struct State {
        // Container measurement (container.cpp).
        const LayoutStyle* style = nullptr;
        bool row = true;
        bool container_min_content = false;
        bool container_max_content = false;
        WritingMode mode = WritingMode::HorizontalTb;
        Direction direction = Direction::Ltr;
        AxisSize width_basis;
        AxisSize height_basis;
        Edges padding;
        Edges border;
        float inline_chrome = 0;
        float block_chrome = 0;
        float pad_inline = 0;
        float pad_block = 0;
        float self_margin_inline = 0;
        float bar = 0;
        float item_inline = 0;
        float inner_inline = 0;
        float inner_main = 0;
        float inner_cross = 0;
        float percent_basis = 0;
        float main_gap = 0;
        float cross_gap = 0;
        bool main_known = false;
        bool cross_known = false;
        bool percent_known = false;
        bool button_fit = false;
        bool block_auto = false;
        // The container's content box, still being resolved across the phases.
        bool inline_known = false;
        bool block_known = false;
        float content_inline = 0;
        float content_block = 0;
        /// Min-content and max-content asks of this container would report the same border-box
        /// inline size. `finish` copies it onto a min-content result.
        bool intrinsic_inline_equal = false;

        // Item measurement (items.cpp).
        std::vector<flex_detail::Item> items;

        // Line building (lines.cpp) and cross-axis packing (pack.cpp).
        std::vector<std::vector<flex_detail::Item*>> lines;
        std::vector<float> line_crosses;
        float content_cross = 0;
        float cross_gaps = 0;
        float align_leading = 0;
        float align_between = 0;
        bool overflow_x = false;
        bool overflow_y = false;
        bool block_bar_needed = false;

        // Placement (placement.cpp).
        float reach_main = 0;
        float reach_cross = 0;
        float lines_cross = 0;
        float frag_chrome_start = 0;
        float frag_own_start = 0;
        float frag_content_limit = 0;
        float frag_resume = 0;
        float frag_placed_block = 0;
        float frag_ask_block = 0;
        float frag_shortage = 0;
        BreakToken frag_token;
        bool frag_finished = true;
        int frag_first_unplaced = -1;

        // The result handed back (finish.cpp).
        LayoutResult out;
    };

    void measure_container(State& state);
    void measure_items(State& state);
    void build_lines(State& state);
    void pack_lines(State& state);
    void place_lines(State& state);
    void finish(State& state);

    Box& box_;
    const LayoutKey& key_;
    LayoutContext& cx_;
    bool reserve_block_bar_;
};

}  // namespace layout
}  // namespace zui

#endif
