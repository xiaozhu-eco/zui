#ifndef ZUI_LAYOUT_CACHE_LAYOUT_INVALIDATION_H
#define ZUI_LAYOUT_CACHE_LAYOUT_INVALIDATION_H

namespace zui {
namespace layout {

struct Box;
struct ChildRequest;
struct LayoutResult;
class LayoutContext;

// Drops the box's cache slots and walks ancestors through used_requests.
class LayoutInvalidation {
public:
    static void mark(Box& box, LayoutContext& cx);
    static void mark_font_dependents(Box& root, LayoutContext& cx);
    /// The invalidation step of a structural change (LAYOUT.md section 6): the box and every box
    /// above it go Layout, their slots go, their epochs move and the subtree-dirty flag is set up to
    /// the root. Dropping the whole chain, and not only the changed host, is what makes the frame
    /// that made the change ask it: another invalidation in the same frame can lay the dropped host
    /// out through `differs` -> `resolve_for_invalidation`, and the `store` that follows settles the
    /// dirty flags up to the root (`settle_subtree_dirty`) - the root then trusts its cached result,
    /// whose recorded contribution did not change, and never asks the host at all. With the chain's
    /// slots gone the root cannot hit, so the recompute reaches the host, which is exactly the cost
    /// LAYOUT.md section 6 asks for: the host chain recomputes and everything else hits. There is
    /// deliberately no contribution walk: walking contributions here would lay the host out early,
    /// and a fixed-position child of a fragment is only collected by the root's own compute.
    static void drop_chain(Box& box, LayoutContext& cx);
};

/// LAYOUT.md section 6 contribution compare: border box, baseline when requested, margin strut,
/// overflow, floats, and break token.
bool same_contribution(const ChildRequest& request, const LayoutResult& result);

}  // namespace layout
}  // namespace zui

#endif
