// Item measurement: build one `Item` per in-flow child and ask it for the flex base, its
// min-content contribution and its hypothetical main size (CSS Flexbox section 9.2 / 9.9).

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

void FlexPass::measure_items(State& state) {
    Box& box = box_;
    const LayoutKey& key = key_;
    LayoutContext& cx = cx_;
    const LayoutStyle& style = *state.style;
    const bool row = state.row;
    const bool container_min_content = state.container_min_content;
    const bool container_max_content = state.container_max_content;
    const WritingMode mode = state.mode;
    const Direction direction = state.direction;
    const AxisSize width_basis = state.width_basis;
    const AxisSize height_basis = state.height_basis;
    const float percent_basis = state.percent_basis;
    const bool percent_known = state.percent_known;
    const bool main_known = state.main_known;
    const float inner_main = state.inner_main;
    const bool cross_known = state.cross_known;
    const float inner_cross = state.inner_cross;
    const float inner_inline = state.inner_inline;
    const bool inline_known = state.inline_known;
    const bool block_known = state.block_known;
    const float content_block = state.content_block;
    const float pad_inline = state.pad_inline;
    const float pad_block = state.pad_block;
    const Edges padding = state.padding;
    const Edges border = state.border;
    const bool button_fit = state.button_fit;
    std::vector<Item> items;
    float cursor_inline = 0;
    float cursor_block = 0;
    for (Box* child : box.children) {
        if (child == nullptr) {
            continue;
        }
        const LayoutStyle& child_style = style_of(*child);
        if (child_style.position_absolute || child_style.position_fixed) {
            // CSS Flexbox §4.1: an abspos child is the sole flex item, aligned with
            // justify-content and align-items. Siblings do not move it. Content-edge
            // only: `placed_static` adds this container's padding, and when the content
            // box was still indefinite here it re-aligns from the used content box.
            const LogicalPoint local = sole_abspos_static(
                style, child_style, inline_known ? inner_inline : 0.f, inline_known,
                block_known ? content_block : 0.f, block_known);
            child->placement = {};
            child->has_static_local = true;
            child->static_local = local;
            cx.note_out_of_flow(*child, local);
            continue;
        }
        Item item;
        item.box = child;
        item.grow = std::max(0.f, child_style.flex_grow);
        item.shrink = std::max(0.f, child_style.flex_shrink);
        const bool main_horizontal = flex_main_is_physical_horizontal(mode, row);
        const float child_pad_main =
            main_horizontal
                ? length_or_percent(child_style.padding_left, child_style.padding_left_percent,
                                    percent_basis, percent_known) +
                      length_or_percent(child_style.padding_right,
                                        child_style.padding_right_percent, percent_basis,
                                        percent_known)
                : length_or_percent(child_style.padding_top, child_style.padding_top_percent,
                                    percent_basis, percent_known) +
                      length_or_percent(child_style.padding_bottom,
                                        child_style.padding_bottom_percent, percent_basis,
                                        percent_known);
        const float child_border_main = main_horizontal
                                            ? child_style.border_left + child_style.border_right
                                            : child_style.border_top + child_style.border_bottom;
        const float chrome_main = child_pad_main + child_border_main;
        const bool child_width_auto_flag =
            child_style.width_auto || (child_style.width_percent && !width_basis.definite);
        const bool child_height_auto_flag =
            child_style.height_auto || (child_style.height_percent && !height_basis.definite);
        bool width_as_auto = false;
        bool height_as_auto = false;
        float child_width = used_main(child_style.width_auto, child_style.width_percent,
                                      child_style.width_px, width_basis, &width_as_auto);
        float child_height = used_main(child_style.height_auto, child_style.height_percent,
                                       child_style.height_px, height_basis, &height_as_auto);
        apply_size_overlays(child->node, child_width, width_as_auto, child_height, height_as_auto);
        // Main size follows the flex main axis mapped through writing-mode.
        const bool main_size_auto = main_horizontal ? (child_width_auto_flag || width_as_auto)
                                                    : (child_height_auto_flag || height_as_auto);
        const float main_specified = main_horizontal ? child_width : child_height;
        item.main_size_auto = main_size_auto;
        if (!main_size_auto) {
            item.main_specified_border =
                border_from_specified(main_specified, child_style.border_box, chrome_main);
        }
        // `flex-basis` resolves against the flex container's inner main size, not the containing
        // block (CSS Flexbox section 7.2.3), so a percentage - and equally a declaration the
        // interner kept for the used-value pass - needs that size to be known.
        const bool basis_kept = kept_math(child_style, LengthProperty::FlexBasis) != nullptr;
        const bool basis_from_container = child_style.flex_basis_percent || basis_kept;
        const bool basis_indefinite = child_style.flex_basis_content ||
                                      (child_style.flex_basis_auto && !basis_kept) ||
                                      (basis_from_container && !main_known);
        if (!basis_indefinite) {
            const AxisSize basis_axis{main_known, main_known ? inner_main : 0.f};
            const float specified =
                used_main(child_style, LengthProperty::FlexBasis, basis_axis, nullptr);
            item.base = border_from_specified(specified, child_style.border_box, chrome_main);
        } else if (!basis_from_container && child_style.flex_basis_auto && !main_size_auto) {
            item.base = item.main_specified_border;
        } else {
            item.base = -1.f;
        }
        item.cross_auto = main_horizontal ? (child_height_auto_flag || height_as_auto)
                                          : (child_width_auto_flag || width_as_auto);
        const AxisSize margin_basis{percent_known, percent_basis};
        const float m_top =
            child_style.margin_top_auto
                ? 0
                : used_edge(child_style.margin_top, child_style.margin_top_percent, margin_basis);
        const float m_right = child_style.margin_right_auto
                                  ? 0
                                  : used_edge(child_style.margin_right,
                                              child_style.margin_right_percent, margin_basis);
        const float m_bottom = child_style.margin_bottom_auto
                                   ? 0
                                   : used_edge(child_style.margin_bottom,
                                               child_style.margin_bottom_percent, margin_basis);
        const float m_left =
            child_style.margin_left_auto
                ? 0
                : used_edge(child_style.margin_left, child_style.margin_left_percent, margin_basis);
        const Edges child_margin =
            physical_edges_to_logical(mode, direction, m_top, m_right, m_bottom, m_left);
        // Same mapping for the `auto` flags; `physical_edges_to_logical` is the one place that
        // knows how physical edges map onto this container's axes.
        const Edges auto_margin = physical_edges_to_logical(
            mode, direction, child_style.margin_top_auto ? 1.f : 0.f,
            child_style.margin_right_auto ? 1.f : 0.f, child_style.margin_bottom_auto ? 1.f : 0.f,
            child_style.margin_left_auto ? 1.f : 0.f);
        if (row) {
            item.margin_main_start = child_margin.inline_start;
            item.margin_main_end = child_margin.inline_end;
            item.margin_cross_start = child_margin.block_start;
            item.margin_cross_end = child_margin.block_end;
            item.main_start_auto = auto_margin.inline_start > 0.5f;
            item.main_end_auto = auto_margin.inline_end > 0.5f;
            item.cross_start_auto = auto_margin.block_start > 0.5f;
            item.cross_end_auto = auto_margin.block_end > 0.5f;
        } else {
            item.margin_main_start = child_margin.block_start;
            item.margin_main_end = child_margin.block_end;
            item.margin_cross_start = child_margin.inline_start;
            item.margin_cross_end = child_margin.inline_end;
            item.main_start_auto = auto_margin.block_start > 0.5f;
            item.main_end_auto = auto_margin.block_end > 0.5f;
            item.cross_start_auto = auto_margin.inline_start > 0.5f;
            item.cross_end_auto = auto_margin.inline_end > 0.5f;
        }
        items.push_back(item);
    }
    std::stable_sort(items.begin(), items.end(), [](const Item& left, const Item& right) {
        return style_of(*left.box).flex_order < style_of(*right.box).flex_order;
    });

    const AxisSize container_percent{percent_known, percent_basis};
    for (Item& item : items) {
        ConstraintSpace space;
        // The cross size of a column container's stretched item is the container's inner inline
        // size (CSS Flexbox section 9.4 steps 9-10), and a nested wrapping container has to be
        // measured at that width to wrap at all. Passing it as the *available* inline size here
        // was tried and reverted: it fixed a wrapping row container inside a column container
        // (`example/tests/text`'s `.grid`) but collapsed the text items of a plain row container
        // inside a column container (`example/tests/flow`'s `.topbar`: 18px wide instead of 72px,
        // one character per line). The distinction between those two passes has to be understood
        // before the definite size can be handed down.
        const LayoutStyle& measured_style = style_of(*item.box);
        const FlexAlign item_align = measured_style.align_self == FlexAlign::Auto
                                         ? style.align_items
                                         : measured_style.align_self;
        const bool cross_stretch = item_align == FlexAlign::Stretch && measured_style.width_auto &&
                                   !measured_style.margin_left_auto &&
                                   !measured_style.margin_right_auto;
        space.available.inline_size = !row && inline_known && cross_stretch
                                          ? AxisSize{true, std::max(0.f, inner_inline)}
                                          : AxisSize{false, 0};
        space.available.block_size = {false, 0};
        space.percentage.inline_size = container_percent;
        space.percentage.block_size = block_known ? AxisSize{true, content_block + pad_block}
                                                  : key.space.percentage.block_size;
        // An absolutely positioned child's containing block is the *padding* box (CSS 2.1
        // section 10.1), which is why this stays separate from `percentage`.
        space.containing_block.inline_size = AxisSize{percent_known, percent_basis + pad_inline};
        space.containing_block.block_size = block_known ? AxisSize{true, content_block + pad_block}
                                                        : key.space.percentage.block_size;
        space.containing_block.fixed_is_viewport = key.space.containing_block.fixed_is_viewport;
        space.request.kind = RequestKind::FlexBase;
        space.request.axis = row ? Axis::Inline : Axis::Block;
        space.writing_mode = style.writing_mode;
        space.direction = style.direction;
        space.icb = key.space.icb;
        // A nested flex/grid/table with a definite flex base does not need a content measure
        // when this container is a single line with a definite cross size and the item
        // stretches (CSS Flexbox §9.4: that line's cross size is the container's, and the
        // base is already resolved). Placement still asks Final at the used main size.
        // Leaf items keep the ask: tests pin those FlexBase requests, and they are cheap
        // next to laying a month grid out twice.
        const bool heavy = measured_style.inner == DisplayInner::Flex ||
                           measured_style.inner == DisplayInner::Grid ||
                           measured_style.inner == DisplayInner::Table ||
                           measured_style.inner == DisplayInner::Multicol;
        const bool logical_stretch = item.cross_auto && cross_known &&
                                     item_align == FlexAlign::Stretch &&
                                     !item.cross_start_auto && !item.cross_end_auto;
        const RequestKind parent_ask = key.space.request.kind;
        const bool content_ask =
            container_min_content || container_max_content || button_fit ||
            parent_ask == RequestKind::FlexBase || parent_ask == RequestKind::FlexHypothetical ||
            parent_ask == RequestKind::MinContent || parent_ask == RequestKind::MaxContent ||
            parent_ask == RequestKind::FitContent;
        const bool skip_base_ask = heavy && item.base >= 0.f && inline_known && cross_known &&
                                   !key.space.allow_break &&
                                   style.flex_wrap == FlexWrap::NoWrap && logical_stretch &&
                                   !content_ask && measured_style.aspect_ratio <= 0.f;
        const LayoutResult* measured = nullptr;
        float measured_main = 0.f;
        float measured_cross = 0.f;
        if (skip_base_ask) {
            measured_main = item.base;
            measured_cross = std::max(0.f, inner_cross - item.margin_cross_start -
                                               item.margin_cross_end);
        } else {
            measured = cx.ask(*item.box, space);
            if (measured != nullptr) {
                measured_main = row ? measured->geometry.border_box_inline
                                    : measured->geometry.border_box_block;
                measured_cross = row ? measured->geometry.border_box_block
                                     : measured->geometry.border_box_inline;
            }
        }
        if (item.base < 0.f) {
            item.base = measured_main;
        }
        item.cross = measured_cross;
        item.flex_base_main = measured_main;
        item.flex_base_cross = measured_cross;
        const LayoutStyle& child_style = style_of(*item.box);
        const bool main_horizontal = flex_main_is_physical_horizontal(mode, row);
        // CSS Flexbox section 9.2 step 3: an item with an aspect ratio and a definite cross size
        // takes its flex base size from that ratio. A shelf cover is `aspect-ratio: 3 / 4` inside
        // a column flex, so its height has to come from the card's width, and the measured content
        // height the base would otherwise hold is irrelevant. The cross size is definite either
        // because a stretched item fills a known container cross size, or because the item states
        // it itself - a percentage only while its basis is known (CSS Sizing section 5.1).
        const bool cross_percent =
            main_horizontal ? child_style.height_percent : child_style.width_percent;
        const bool cross_basis_known =
            main_horizontal ? height_basis.definite : width_basis.definite;
        const bool cross_definite = item.cross_auto
                                        ? (cross_known && item_align == FlexAlign::Stretch &&
                                           !item.cross_start_auto && !item.cross_end_auto)
                                        : (!cross_percent || cross_basis_known);
        if (child_style.aspect_ratio > 0.f && item.main_size_auto && cross_definite) {
            const float cross_border =
                item.cross_auto
                    ? std::max(0.f, inner_cross - item.margin_cross_start - item.margin_cross_end)
                    : item.cross;
            item.base = std::max(0.f, cross_border / child_style.aspect_ratio);
        }
        const bool overflow_hidden = main_horizontal
                                         ? child_style.overflow_x != OverflowMode::Visible
                                         : child_style.overflow_y != OverflowMode::Visible;
        const LengthProperty min_prop =
            main_horizontal ? LengthProperty::MinWidth : LengthProperty::MinHeight;
        bool min_auto = main_horizontal ? child_style.min_width_auto : child_style.min_height_auto;
        const bool min_percent =
            main_horizontal ? child_style.min_width_percent : child_style.min_height_percent;
        const float min_px = main_horizontal ? child_style.min_width_px : child_style.min_height_px;
        const AxisSize main_basis{main_known, main_known ? inner_main : 0.f};
        // A declaration the interner kept needs the container's inner main size just as a
        // percentage does; when that size is unknown the automatic minimum stands in for it.
        const bool min_kept = kept_math(child_style, min_prop) != nullptr;
        float min_specified = 0.f;
        if (min_kept) {
            min_specified = used_main(child_style, min_prop, main_basis, &min_auto);
        } else if (!min_auto) {
            min_specified = min_percent && main_known ? inner_main * min_px / 100.f : min_px;
        }
        const float child_chrome = main_horizontal
                                       ? child_style.border_left + child_style.border_right +
                                             child_style.padding_left + child_style.padding_right
                                       : child_style.border_top + child_style.border_bottom +
                                             child_style.padding_top + child_style.padding_bottom;
        // Ask MinContent for automatic min (overflow:visible only), or whenever this container
        // answers MinContent (contribution must be real even if the item clips). Ask-fuse allows
        // two asks per child, so Final below resets the fuse first.
        const bool need_min_content = container_min_content || (min_auto && !overflow_hidden);
        // The flex-base walk already produced the min-content border (nowrap, a definite size,
        // or a nested container whose items matched). The automatic minimum reads that border.
        // `intrinsic_inline_equal` is an inline-axis claim (the flex-base walk already produced
        // the min-content border-box inline size). A column container's MinContent ask is the
        // block axis; skipping it from that flag dropped the ask `ColumnMinContentAtDefiniteCrossSize`
        // records, even when the two block sizes would agree.
        const bool base_matches_min = row && need_min_content && measured != nullptr &&
                                      measured->intrinsic_inline_equal;
        if (base_matches_min) {
            item.asked_min_content = true;
            item.content_min_main = measured_main;
            item.content_min_cross = measured_cross;
        } else if (need_min_content) {
            ConstraintSpace min_space = space;
            min_space.request.kind = RequestKind::MinContent;
            min_space.request.axis = row ? Axis::Inline : Axis::Block;
            const LayoutResult* min_measured = cx.ask(*item.box, min_space);
            item.asked_min_content = true;
            if (min_measured != nullptr) {
                item.content_min_main = row ? min_measured->geometry.border_box_inline
                                            : min_measured->geometry.border_box_block;
                item.content_min_cross = row ? min_measured->geometry.border_box_block
                                             : min_measured->geometry.border_box_inline;
            }
        }
        if (!min_auto) {
            item.min_size =
                border_from_specified(min_specified, child_style.border_box, child_chrome);
        } else if (overflow_hidden) {
            // overflow != visible => the automatic minimum size is 0 (CSS Flexbox §4.5).
            // That 0 is a content size. The border box cannot be smaller than its own
            // padding and border: a content-box `<input>` (UA `overflow: clip`) asked for
            // a target of 0 came back as its chrome and tripped the two-round ceiling.
            item.min_size = child_chrome;
        } else {
            // Content-size suggestion: the min-content size in the main axis, clamped by
            // the max-content size when the flex base is that max-content size. A row flex
            // label's min-content height is the wrapped height; its max-content height is
            // the single line. The automatic minimum is the smaller of the two (CSS Flexbox
            // §4.5), so the wrapped height must not raise the used height.
            item.min_size = item.content_min_main;
            if (style_of(*item.box).flex_basis_auto && item.main_size_auto && item.base >= 0.f) {
                item.min_size = std::min(item.min_size, item.base);
            }
            if (!item.main_size_auto) {
                item.min_size = std::min(item.min_size, item.main_specified_border);
            }
        }
        const LengthProperty max_prop =
            main_horizontal ? LengthProperty::MaxWidth : LengthProperty::MaxHeight;
        bool max_none = main_horizontal ? child_style.max_width_none : child_style.max_height_none;
        float max_specified = 0.f;
        if (kept_math(child_style, max_prop) != nullptr) {
            max_specified = used_main(child_style, max_prop, main_basis, &max_none);
        } else if (!max_none) {
            const bool max_percent =
                main_horizontal ? child_style.max_width_percent : child_style.max_height_percent;
            const float max_px =
                main_horizontal ? child_style.max_width_px : child_style.max_height_px;
            max_specified = max_percent && main_known ? inner_main * max_px / 100.f : max_px;
        }
        if (!max_none) {
            item.has_max = true;
            item.max_size =
                border_from_specified(max_specified, child_style.border_box, child_chrome);
        }
        if (item.has_max && min_auto && !overflow_hidden) {
            item.min_size = std::min(item.min_size, item.max_size);
        }
        // Max, then min (CSS Sizing): a specified minimum larger than the maximum wins.
        item.hypo = item.base;
        if (item.has_max && item.hypo > item.max_size) {
            item.hypo = item.max_size;
        }
        if (item.hypo < item.min_size) {
            item.hypo = item.min_size;
        }
        // Container MinContent uses each item's min-content contribution, not FlexBase.
        if (container_min_content) {
            float contrib = item.content_min_main;
            // Multi-line: cap by flex base if not growable, floor if not shrinkable.
            if (style.flex_wrap != FlexWrap::NoWrap) {
                if (item.grow <= 0.f) {
                    contrib = std::min(contrib, item.base);
                }
                if (item.shrink <= 0.f) {
                    contrib = std::max(contrib, item.base);
                }
            }
            if (item.has_max) {
                contrib = std::min(contrib, item.max_size);
            }
            if (!min_auto) {
                contrib = std::max(contrib, item.min_size);
            }
            item.hypo = contrib;
            // A definite main size already decides wrapping. The cross size of a
            // min-content main-size layout is the size at the longest unbreakable
            // run, which is taller than the line at the used main size (CSS Flexbox
            // §9.4 hypothetical cross size, §9.9.2). hello's ".form" grew a second
            // line because "+ Project" wraps only at its min-content width.
            if (!main_known) {
                item.cross = item.content_min_cross;
            }
        }
        // CSS Flexbox section 9.9.1: the max-content contribution of a flexible item is its
        // max-content size, not its flex base size. `flex: 1` gives the reader's `.mark` a zero
        // basis, and it is the glyph plus padding (36px) that makes `.brand` fit-content wide -
        // using the zero base instead left the brand 37px short and shifted the whole top bar. A
        // caller asking for the flex base size of a container wants that same content-derived size,
        // which is why FlexBase counts here next to MaxContent.
        if ((container_max_content || key.space.request.kind == RequestKind::FlexBase ||
             key.space.request.kind == RequestKind::FlexHypothetical || button_fit) &&
            item.grow > 0.f) {
            // A shrink-to-fit button's used width is its max-content size. `flex: 1` is a zero
            // basis, so the hypothetical size would otherwise be 0 and the item's text would paint
            // outside the button (the reader's resume title).
            item.hypo = std::max(item.hypo, measured_main);
            if (button_fit && item.base < measured_main) {
                item.base = measured_main;
            }
        }
        // CSS Flexbox §9.2 / LAYOUT RequestKind::FlexHypothetical: record a dedicated ask at the
        // clamped main size so cache keys distinguish base vs hypothetical. The second ask slot is
        // free only when MinContent was not needed (auto-min / container MinContent already used it).
        if (!skip_base_ask && !need_min_content && item.hypo >= 0.f) {
            // The base walk's border is already the clamped main size, and nothing inside it is
            // waiting on a definite width. Asking again at that size repeats the cross size.
            const bool already = measured != nullptr &&
                                 std::abs(item.hypo - measured_main) <= 0.5f &&
                                 !measured->needs_stretch;
            if (!already) {
                ConstraintSpace hypo_space = space;
                hypo_space.request.kind = RequestKind::FlexHypothetical;
                hypo_space.request.axis = row ? Axis::Inline : Axis::Block;
                if (row) {
                    hypo_space.available.inline_size = AxisSize{true, item.hypo};
                } else {
                    hypo_space.available.block_size = AxisSize{true, item.hypo};
                }
                if (const LayoutResult* hypo_measured = cx.ask(*item.box, hypo_space)) {
                    item.cross = row ? hypo_measured->geometry.border_box_block
                                     : hypo_measured->geometry.border_box_inline;
                }
            }
        }
        item.target = item.hypo;
        (void)measured_main;
        (void)child_chrome;
    }

    // A definite inline size is the border box of both contribution asks. An indefinite one
    // matches when every item's min-content inline contribution equals its flex-base inline
    // contribution (nowrap, a definite item width). The grid's max-content ask only reads that
    // border box, so it can skip the second walk.
    bool equal = inline_known;
    if (!equal) {
        equal = true;
        for (const Item& item : items) {
            if (!item.asked_min_content) {
                equal = false;
                break;
            }
            const float base_inline = row ? item.flex_base_main : item.flex_base_cross;
            const float min_inline = row ? item.content_min_main : item.content_min_cross;
            if (std::abs(base_inline - min_inline) > 0.01f) {
                equal = false;
                break;
            }
        }
    }
    state.intrinsic_inline_equal = equal;

    state.items = std::move(items);
}
}  // namespace layout
}  // namespace zui
