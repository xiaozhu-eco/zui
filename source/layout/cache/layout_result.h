#ifndef ZUI_LAYOUT_CACHE_LAYOUT_RESULT_H
#define ZUI_LAYOUT_CACHE_LAYOUT_RESULT_H

#include "layout/cache/box_geometry.h"
#include "layout/cache/break_token.h"
#include "layout/cache/child_request.h"
#include "layout/cache/float_list.h"
#include "layout/cache/inline_content.h"
#include "layout/cache/margin_strut.h"
#include "layout/cache/overflow.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace zui {
namespace layout {

struct Box;

/// Where a formatting context put one child. A later measurement pass lays the same
/// children out again and would otherwise leave those offsets in place after the used
/// result is pinned back.
struct ChildPlace {
    Box* box = nullptr;
    float inline_offset = 0;
    float block_offset = 0;
};

struct LayoutResult {
    BoxGeometry geometry;
    MarginStrut margin_strut;
    Overflow overflow;
    FloatList floats;
    std::unique_ptr<InlineContent> lines;
    BreakToken break_token;
    std::vector<ChildRequest> used_requests;
    std::vector<ChildPlace> child_places;
    // Column balancing (LAYOUT.md section 12, ROADMAP2 block 6): how much more fragmentainer space
    // the *first* content that did not fit this fragment would have needed, measured from the
    // fragment's own start. Zero when nothing was pushed out. Chromium's `MinimalSpaceShortage` is
    // the same number and its column balancing stretches columns by it.
    float space_shortage = 0;
    bool converged = true;
    /// The indefinite shrink-to-fit walk skipped a same-width second pass. This box's border box
    /// is already the contribution the caller asked for, and a descendant still has to stretch
    /// once an ancestor lays this subtree out at a definite inline size. A definite available
    /// size does not set the bit: that walk is the one that stretches.
    bool needs_stretch = false;
    /// Set when a min-content ask with the same constraint space would return this result's
    /// border-box inline size. A min-content result sets it against the max-content contribution.
    /// A flex-base result sets it when that walk already produced the min-content border, so the
    /// flex item pass can skip the second ask. A wrapping box leaves it false.
    bool intrinsic_inline_equal = false;

    LayoutResult() = default;
    LayoutResult(const LayoutResult&) = delete;
    LayoutResult& operator=(const LayoutResult&) = delete;
    LayoutResult(LayoutResult&&) = default;
    LayoutResult& operator=(LayoutResult&&) = default;
};

}  // namespace layout
}  // namespace zui

#endif
