#ifndef ZUI_LAYOUT_STACKING_H
#define ZUI_LAYOUT_STACKING_H

#include "layout/box.h"

#include <vector>

namespace zui {
namespace layout {

/// One box's part in the stacking order (LAYOUT.md section 11). Each input is its own predicate,
/// built from that section's list - deliberately *not* `cb_for_absolute` / `cb_for_fixed`, whose
/// sets differ: `opacity` below 1 builds a stacking context but not a containing block, and a
/// positioned containing block is not necessarily a stacking context.
struct StackingInfo {
    /// This box establishes a stacking context: its descendants are painted inside it.
    bool context = false;
    /// The box is positioned (`position` other than `static`), so `z-index` applies to it.
    bool positioned = false;
    /// `z-index: auto`. Only meaningful together with `positioned` or `context`.
    bool z_auto = true;
    /// The used `z-index`; `auto` reads as 0.
    int z_index = 0;
    /// `order` (CSS Flexbox section 5.4), which decides the order inside one stacking context.
    int order = 0;
    /// A float: CSS 2.1 appendix E paints the in-flow floats of a block *after* its in-flow
    /// block-level children and *before* its inline content, whatever the tree order is.
    bool floating = false;
    /// In-flow inline-level content (text, a non-replaced inline box, an atomic inline), which the
    /// same three steps put last of the non-positioned group.
    bool inline_level = false;
};

StackingInfo stacking_info(const Box& box);

/// Whether this box's children paint in a different order than the tree order. False is the fast
/// path: the painter and the hit tester then walk `box.children` directly.
bool has_stacking_children(const Box& box);

/// The paint order of this box's children (LAYOUT.md section 11). The hit tester walks the same
/// order in reverse, so a hit lands on the box the painter drew on top.
///
/// When @p before_own_inlines is non-null it receives how many leading entries belong to appendix E
/// steps 2–4 (negative-z contexts, in-flow blocks, floats). The box's own text runs paint next
/// (step 5), then the remaining entries (inlines, positioned / z-auto, positive-z).
void stacking_paint_order(const Box& box, std::vector<Box*>& order,
                          std::size_t* before_own_inlines = nullptr);

/// Whether @p box is a stacking context with a used negative `z-index` (appendix E step 2).
bool stacking_negative_z(const Box& box);

/// Whether @p host (a non-context box) must skip painting @p child because the nearest ancestor
/// stacking context lifted that child into its own paint order (`z-index: auto` appendix E rule).
bool stacking_skip_lifted_child(const Box& host, const Box& child);

}  // namespace layout
}  // namespace zui

#endif
