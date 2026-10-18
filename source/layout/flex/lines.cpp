// Line building: collect the items into flex lines, then resolve the container's own used
// inline/block size from those lines, including the block scrollbar gutter and min/max clamps.

#include "layout/flex/pass.h"
#include "layout/flex/detail.h"

#include "layout/baseline.h"
#include "layout/box.h"
#include "layout/cache/fragment_cut.h"
#include "layout/cache/layout_context.h"
#include "layout/cache/logical_axes.h"
#include "layout/cache/used_length.h"
#include "layout/overflow_mode.h"
#include "layout/platform_metrics.h"
#include "layout/scrollbar_v.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>
#include <vector>

namespace zui {
namespace layout {

using namespace flex_detail;

void FlexPass::build_lines(State& state) {
    const LayoutKey& key = key_;
    const LayoutStyle& style = *state.style;
    const bool row = state.row;
    const WritingMode mode = state.mode;
    const bool container_min_content = state.container_min_content;
    const bool main_known = state.main_known;
    const float inner_main = state.inner_main;
    const bool cross_known = state.cross_known;
    const float inner_cross = state.inner_cross;
    const AxisSize width_basis = state.width_basis;
    const AxisSize height_basis = state.height_basis;
    const float main_gap = state.main_gap;
    const float cross_gap = state.cross_gap;
    const bool reserve_block_bar = reserve_block_bar_;
    const float inline_chrome = state.inline_chrome;
    const float block_chrome = state.block_chrome;
    const float pad_block = state.pad_block;
    const float self_margin_inline = state.self_margin_inline;
    const bool button_fit = state.button_fit;
    std::vector<Item>& items = state.items;
    float& content_inline = state.content_inline;
    float& content_block = state.content_block;
    bool& inline_known = state.inline_known;
    bool& block_known = state.block_known;
    bool& block_bar_needed = state.block_bar_needed;
    std::vector<std::vector<Item*>> lines;
    // Multi-line container min-content main size is the largest item contribution, not the
    // sum of all items. With an indefinite main size there is no wrap threshold, so put each
    // item on its own line and let the existing max-over-lines sizing pick the largest.
    if (container_min_content && style.flex_wrap != FlexWrap::NoWrap && !main_known) {
        for (Item& item : items) {
            lines.emplace_back();
            lines.back().push_back(&item);
        }
        if (lines.empty()) {
            lines.emplace_back();
        }
    } else if (!main_known || style.flex_wrap == FlexWrap::NoWrap) {
        lines.emplace_back();
        for (Item& item : items) {
            lines.back().push_back(&item);
        }
    } else {
        lines.emplace_back();
        float used = 0;
        for (Item& item : items) {
            const float outer = outer_main(item, item.hypo);
            if (!lines.back().empty() && used + main_gap + outer > inner_main + 0.5f) {
                lines.emplace_back();
                used = 0;
            }
            if (!lines.back().empty()) {
                used += main_gap;
            }
            lines.back().push_back(&item);
            used += outer;
        }
    }

    // CSS Flexbox §9.4: the hypothetical cross size is layout at the *used* main size, which
    // exists only after free space is distributed. The earlier ask stored the cross size at the
    // hypothetical main size, so a `flex: 1` / `flex-basis: 0%` item contributed the height of a
    // zero-width wrap. Measure again only when the used main size moved and this line's cross
    // size actually comes from the items. A single line with a definite cross size is the
    // container's inner cross size, and both fuse slots were already spent, so the new ask opens
    // its own segment. Placement's Final ask repeats this key when the item is not stretched.
    auto cross_at_used_main = [&](Item& item) {
        if (!item.cross_auto || item.box == nullptr) {
            return;
        }
        if (std::abs(item.target - item.hypo) <= 0.5f) {
            return;
        }
        ConstraintSpace space;
        const float main_available = item.target + item.margin_main_start + item.margin_main_end;
        if (row) {
            space.available.inline_size = AxisSize{true, main_available};
            space.available.block_size = AxisSize{false, 0};
        } else {
            space.available.block_size = AxisSize{true, main_available};
            space.available.inline_size = AxisSize{false, 0};
        }
        space.percentage.inline_size = AxisSize{state.percent_known, state.percent_basis};
        space.percentage.block_size = block_known ? AxisSize{true, content_block + pad_block}
                                                  : AxisSize{false, 0};
        space.containing_block.inline_size =
            AxisSize{state.percent_known, state.percent_basis + state.pad_inline};
        space.containing_block.block_size = block_known ? AxisSize{true, content_block + pad_block}
                                                        : AxisSize{false, 0};
        space.containing_block.fixed_is_viewport = key.space.containing_block.fixed_is_viewport;
        space.request.kind = RequestKind::Final;
        space.request.axis = row ? Axis::Inline : Axis::Block;
        space.writing_mode = style.writing_mode;
        space.direction = style.direction;
        space.icb = key.space.icb;
        cx_.reset_ask_fuse(*item.box);
        if (const LayoutResult* measured = cx_.ask(*item.box, space)) {
            item.cross = row ? measured->geometry.border_box_block
                             : measured->geometry.border_box_inline;
        }
    };
    auto line_outer_cross = [&](std::vector<Item*>& line) {
        if (!(cross_known && lines.size() == 1)) {
            for (Item* item : line) {
                cross_at_used_main(*item);
            }
        }
        float line_cross = 0;
        for (Item* item : line) {
            line_cross = std::max(line_cross,
                                  item->margin_cross_start + item->cross + item->margin_cross_end);
        }
        if (cross_known && lines.size() == 1) {
            line_cross = inner_cross;
        }
        return line_cross;
    };

    float sum_cross = 0;
    float max_line_cross = 0;
    for (std::size_t line_index = 0; line_index < lines.size(); ++line_index) {
        if (main_known) {
            distribute(lines[line_index], inner_main, main_gap);
        }
        const float line_cross = line_outer_cross(lines[line_index]);
        max_line_cross = std::max(max_line_cross, line_cross);
        sum_cross += line_cross;
    }
    if (lines.size() > 1) {
        sum_cross += cross_gap * static_cast<float>(lines.size() - 1);
    }
    // Indefinite main size has no wrap threshold, so the cross size is the largest item
    // contribution. A definite main size already collected real lines above.
    if (container_min_content && !cross_known && !main_known) {
        float max_item_cross = 0;
        for (const Item& item : items) {
            max_item_cross = std::max(max_item_cross,
                                      item.margin_cross_start + item.cross + item.margin_cross_end);
        }
        sum_cross = max_item_cross;
        max_line_cross = max_item_cross;
    }
    // How far the items reach on the block axis: a row container stacks its lines on the cross
    // axis, a column container lays them out along the inline axis and takes the tallest.
    float content_extent = sum_cross;
    if (!row) {
        content_extent = 0.f;
        for (const std::vector<Item*>& line : lines) {
            float line_main =
                main_gap * static_cast<float>(std::max(0, static_cast<int>(line.size()) - 1));
            for (const Item* item : line) {
                line_main += outer_main(*item, item->target);
            }
            content_extent = std::max(content_extent, line_main);
        }
    }
    // Decided before the auto block size fallback, so `block_known` still means "definite":
    // only a scroll container with a definite block size reserves the block scrollbar gutter.
    const bool ox = style.overflow_x != OverflowMode::Visible;
    const bool oy = style.overflow_y != OverflowMode::Visible;
    const OverflowMode block_overflow =
        is_horizontal_wm(mode) ? style.overflow_y : style.overflow_x;
    // `scrollbar-gutter: stable` reserves the block bar even when the content fits, and
    // even when the block size is still the content size. `overflow: scroll` / overflowing
    // `auto` keep the classic second pass, and only when the block size is definite.
    if (!reserve_block_bar && overflow_eats_inline(mode, ox, oy) &&
        block_overflow != OverflowMode::Visible &&
        (scrollbar_gutter_forces_reserve(style) ||
         (block_known &&
          (block_overflow == OverflowMode::Scroll ||
           (block_overflow == OverflowMode::Auto && content_extent > content_block + 0.5f))))) {
        block_bar_needed = true;
    }
    if (!inline_known) {
        if (row) {
            float widest = 0;
            for (const std::vector<Item*>& line : lines) {
                float used = 0;
                for (std::size_t i = 0; i < line.size(); ++i) {
                    if (i > 0) {
                        used += main_gap;
                    }
                    used += outer_main(*line[i], line[i]->target);
                }
                widest = std::max(widest, used);
            }
            content_inline = widest;
            inline_known = true;
        } else {
            content_inline = max_line_cross;
        }
    }
    // The indefinite pass above is the max-content size. Shrink-to-fit then clamps it to the
    // available inline size (CSS Sizing section 5.1) and, on a row, redistributes so flex items
    // actually shrink instead of overflowing a narrower button.
    if (button_fit && row && key.space.available.inline_size.definite &&
        content_inline >
            key.space.available.inline_size.px - self_margin_inline - inline_chrome + 0.5f) {
        content_inline =
            std::max(0.f, key.space.available.inline_size.px - self_margin_inline - inline_chrome);
        sum_cross = 0;
        max_line_cross = 0;
        for (std::vector<Item*>& line : lines) {
            distribute(line, content_inline, main_gap);
            const float line_cross = line_outer_cross(line);
            max_line_cross = std::max(max_line_cross, line_cross);
            sum_cross += line_cross;
        }
        if (lines.size() > 1) {
            sum_cross += cross_gap * static_cast<float>(lines.size() - 1);
        }
    }
    if (!block_known) {
        if (row) {
            content_block = sum_cross;
        } else {
            float used = 0;
            for (const std::vector<Item*>& line : lines) {
                float line_main =
                    main_gap * static_cast<float>(std::max(0, static_cast<int>(line.size()) - 1));
                for (const Item* item : line) {
                    line_main += outer_main(*item, item->target);
                }
                used = std::max(used, line_main);
            }
            content_block = used;
        }
        block_known = true;
    }
    // The container's own min/max size. A shrink-to-fit absolute flex (the reader shelf menu's
    // `min-width: 168px`, the status bar's `min-height: 30px`) otherwise stays at the size of its
    // items. Clamp before the final item ask so `width: 100%` children see the clamped inner size.
    // When min and max disagree, min wins (CSS Sizing section 3.5).
    {
        bool min_w_auto = false;
        bool min_h_auto = false;
        bool max_w_none = false;
        bool max_h_none = false;
        const float min_w = used_main(style, LengthProperty::MinWidth, width_basis, &min_w_auto);
        const float min_h = used_main(style, LengthProperty::MinHeight, height_basis, &min_h_auto);
        const float max_w = used_main(style, LengthProperty::MaxWidth, width_basis, &max_w_none);
        const float max_h = used_main(style, LengthProperty::MaxHeight, height_basis, &max_h_none);
        float min_inline = 0.f;
        float min_block = 0.f;
        float max_inline = 0.f;
        float max_block = 0.f;
        bool min_inline_auto = true;
        bool min_block_auto = true;
        bool max_inline_none = true;
        bool max_block_none = true;
        physical_size_to_logical(mode, min_w, min_h, min_w_auto, min_h_auto, &min_inline,
                                 &min_block, &min_inline_auto, &min_block_auto);
        physical_size_to_logical(mode, max_w, max_h, max_w_none, max_h_none, &max_inline,
                                 &max_block, &max_inline_none, &max_block_none);
        const auto to_content = [&](float specified, float chrome) {
            if (style.border_box) {
                return std::max(0.f, specified - chrome);
            }
            return std::max(0.f, specified);
        };
        if (!max_inline_none) {
            content_inline = std::min(content_inline, to_content(max_inline, inline_chrome));
        }
        if (!min_inline_auto) {
            content_inline = std::max(content_inline, to_content(min_inline, inline_chrome));
        }
        if (!max_block_none) {
            content_block = std::min(content_block, to_content(max_block, block_chrome));
        }
        if (!min_block_auto) {
            content_block = std::max(content_block, to_content(min_block, block_chrome));
        }
    }

    state.lines = std::move(lines);
    state.overflow_x = ox;
    state.overflow_y = oy;
}
}  // namespace layout
}  // namespace zui
