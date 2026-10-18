#ifndef ZUI_SOURCE_LAYOUT_BASELINE_H
#define ZUI_SOURCE_LAYOUT_BASELINE_H

#include "html/document.h"
#include "layout/box.h"
#include "layout/cache/layout_result.h"
#include "layout/display.h"
#include "layout/layout_style.h"
#include "layout/overflow_mode.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace zui {
namespace layout {

/// Which of an atomic inline's own baselines the line it sits on uses.
///
/// Three different rules meet here, and a browser applies all three:
///
///  - an **inline-block** contributes the baseline of its **last** in-flow line box (CSS 2.1
///    section 10.8.3), not its first: measured against Chrome, a 2-line inline-block puts the
///    second line's baseline on the line.
///  - an **inline-grid** / **inline-flex** contributes the baseline its items produce: the *first*
///    item of the first row / first line, using that item's own first baseline when it has one and
///    the block-end edge of its border box otherwise (CSS Grid section 8.5, CSS Flexbox section
///    8.5). Measured against Chrome too: with two first-row items of different baselines the
///    container reports the first one in document order rather than the largest, an item's
///    block-end margin is not part of the value, and `overflow` on the container does not change
///    it.
///  - anything else that is atomic and inline-level (an inline-table takes its first row, CSS 2.1
///    section 17.5.3) reports what its own layout produced.
///
/// `None` means the box has no baseline to offer and the line has to fall back to the box's
/// **bottom margin edge** - CSS 2.1 section 10.8.3 for an inline-block with no in-flow line box or
/// with `overflow` other than `visible`, and what Chrome does for an empty inline-grid.
enum class AtomicBaseline { None, First, Last };

/// True when the box's own layout produced in-flow line boxes, which is what makes a line-box
/// baseline available at all.
inline bool has_line_boxes(const LayoutResult& result) {
    return result.lines != nullptr && !result.lines->lines.empty();
}

/// CSS Overflow 3: only `visible` keeps an inline-block's own baseline. `hidden`, `clip`, `auto`
/// and `scroll` all make it fall back to its bottom margin edge.
inline bool overflow_visible(const LayoutStyle& style) {
    return style.overflow_x == OverflowMode::Visible && style.overflow_y == OverflowMode::Visible;
}

/// CSS Ruby: a ruby container is inline-level but its baseline is the **base** text's, not the
/// annotation's, so it is one of the `First` kinds below.
inline bool ruby_container(const Box& box) {
    if (box.node == nullptr) {
        return false;
    }
    const std::string* display = box.node->style("display");
    return display != nullptr && *display == "ruby";
}

inline AtomicBaseline atomic_baseline_kind(const Box& box, const LayoutStyle& style,
                                           const LayoutResult& result) {
    // A replaced box has no line boxes at all, and Chrome aligns the block-end edge of its border
    // box to the baseline (`layout_replaced` reports exactly that; measured on a checkbox with a
    // 6px block-end margin: the margin lands in the descent, not in the baseline). Handled above:
    // `atomic_inline_baseline` gives a replaced box its margin edge and a widget its border edge.
    if (box.is_replaced) {
        return AtomicBaseline::First;
    }
    if (ruby_container(box)) {
        // The container stacks an annotation above the base, so its *last* line-box baseline is
        // the annotation's; CSS Ruby puts the base's baseline on the line.
        return AtomicBaseline::First;
    }
    switch (style.inner) {
        case DisplayInner::Flow:
            // An inline-block. No in-flow line box means no baseline either.
            if (!overflow_visible(style) || !has_line_boxes(result)) {
                return AtomicBaseline::None;
            }
            return AtomicBaseline::Last;
        case DisplayInner::Grid:
        case DisplayInner::Flex:
            // The container kinds synthesize `first` from their items, so the line reads it even
            // when the container itself has no line boxes.
            return AtomicBaseline::First;
        case DisplayInner::Table:
        case DisplayInner::Multicol:
        default:
            break;
    }
    if (style.table_part == TablePart::Table) {
        return AtomicBaseline::First;  // an inline-table reports its first row
    }
    return AtomicBaseline::First;
}

/// A box the engine paints as a widget instead of laying text out in: a checkbox, a radio, a range
/// or colour swatch, a file button, a progress bar. Chrome treats them as replaced boxes - the
/// block-end edge of the border box sits on the baseline, so a block-end margin shows up in the
/// descent instead of lifting the baseline. Measured against Chrome on a checkbox with a 6px
/// block-end margin; an *empty inline-block* with the same margin is the opposite case, and its
/// margin does land in the baseline.
inline bool widget_box(const Box& box) {
    return html::is_checkbox(box.node) || html::is_radio(box.node) || html::is_range(box.node) ||
           html::is_file(box.node) || html::is_progress(box.node);
}

/// The baseline a **grid / flex item** without one of its own contributes when its container
/// derives a baseline for the row or the line: the block-end edge of its border box, block-end
/// margin excluded (CSS Box Alignment section 9.2). Measured against Chrome - and different from
/// the inline fallback, which uses the margin edge.
inline float synthesized_item_baseline(const LayoutResult& result) {
    return result.geometry.border_box_block;
}

/// The baseline the box offers to the line, measured from its **border box** block-start edge, or
/// NaN when it has none. Callers add the box's block-start margin themselves, because the margin
/// box is where the line places the piece.
inline float atomic_inline_baseline(const Box& box, const LayoutStyle& style,
                                    const LayoutResult& result) {
    if (widget_box(box)) {
        const float own = result.geometry.baselines.first;
        return std::isfinite(own) ? own : synthesized_item_baseline(result);
    }
    if (box.is_replaced) {
        // CSS 2.1 section 10.8.1: a replaced element's baseline is its bottom **margin** edge, so
        // the block-end margin moves the baseline instead of showing up in the descent. Measured
        // on a 10px image with a 6px block-end margin: Chrome puts its margin-box bottom on the
        // line.
        return result.geometry.border_box_block + result.geometry.margin.block_end;
    }
    switch (atomic_baseline_kind(box, style, result)) {
        case AtomicBaseline::None:
            return std::numeric_limits<float>::quiet_NaN();
        case AtomicBaseline::Last:
            return result.geometry.baselines.last;
        case AtomicBaseline::First:
            break;
    }
    return result.geometry.baselines.first;
}

}  // namespace layout
}  // namespace zui

#endif
