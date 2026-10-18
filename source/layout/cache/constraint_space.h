#ifndef ZUI_LAYOUT_CACHE_CONSTRAINT_SPACE_H
#define ZUI_LAYOUT_CACHE_CONSTRAINT_SPACE_H

#include "layout/cache/available_size.h"
#include "layout/cache/clearance.h"
#include "layout/cache/containing_block.h"
#include "layout/cache/direction.h"
#include "layout/cache/exclusion_space.h"
#include "layout/cache/fragmentation.h"
#include "layout/cache/logical_point.h"
#include "layout/cache/logical_size.h"
#include "layout/cache/percentage_basis.h"
#include "layout/cache/request.h"
#include "layout/cache/writing_mode.h"

#include <cstdint>
#include <memory>

namespace zui {
namespace layout {

struct LayoutStyle;

struct ConstraintSpace {
    AvailableSize available;
    PercentageBasis percentage;
    Request request;
    WritingMode writing_mode = WritingMode::HorizontalTb;
    Direction direction = Direction::Ltr;
    Clearance clearance;
    // The parent has a definite block size and asks this box to fill it instead of shrinking to
    // its content: CSS Grid align-self: stretch, and the cross axis of CSS Flexbox.
    bool stretch_block = false;
    // The inline-axis twin: the parent solved this box's inline size and asks it to fill that
    // instead of shrinking to its content - CSS Grid justify-self: stretch. It has to travel in
    // the space because a `<button>` grid item otherwise exits through the `button_fit`
    // shrink-to-fit path below (`flow.cpp`) and stays as wide as its content, so its
    // `width: 100%` children resolve against that content width instead of the track.
    bool stretch_inline = false;
    std::shared_ptr<const ExclusionSpace> exclusion;
    FragmentationContext fragmentation;
    bool allow_break = false;
    /// LAYOUT.md section 9 step C: the frame driver decided this box's `content-visibility: auto`
    /// contents are not relevant to the user, so only the `contain-intrinsic-size` placeholder is
    /// laid out. It travels in the key (`key_for` copies it off the box) because the same box has
    /// different geometry in the two states - a cache hit on the other state would be wrong.
    bool contents_skipped = false;
    /// Set by `key_for` when this box's own declaration is a math height or a math inset. Those
    /// read the block percentage basis without a percent flag on `LayoutStyle`, and two nodes can
    /// share one style, so the bit comes from the node. It is not a parent input.
    bool honor_block_basis = false;
    ContainingBlock containing_block;
    // Content-box origin in the enclosing block formatting context.
    // Zero for the context itself. Floats this box returns are already in that system.
    LogicalPoint bfc_origin;
    LogicalSize icb;

    bool equal(const ConstraintSpace& other, const LayoutStyle& style) const;
    std::uint64_t hash(const LayoutStyle& style) const;
    /// Writes the same tokens `equal` / `hash` use. False when the key does not fit in @p cap,
    /// so the caller keeps the live path instead of a truncated token.
    bool export_tokens(const LayoutStyle& style, std::uint64_t* words, int cap, int& n) const;
};

}  // namespace layout
}  // namespace zui

#endif
