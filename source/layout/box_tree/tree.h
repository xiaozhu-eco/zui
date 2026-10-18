#ifndef ZUI_LAYOUT_BOX_TREE_H
#define ZUI_LAYOUT_BOX_TREE_H

#include "layout/box.h"
#include "layout/viewport.h"

#include "html/document.h"

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace zui {
namespace layout {

class LayoutContext;

class BoxTree {
public:
    Box* root() const { return root_; }
    Box* principal(html::Node* node) const;

    void reset();
    /// Directory of the document that owns the tree. Relative `<img src>` values are resolved
    /// against it, so a page still finds its images when the process cwd is somewhere else.
    void set_resource_base(std::string document_path);
    /// `viewport` is the initial containing block: it is what viewport units in a length resolve
    /// against. The first call lowers the tree against it. A later call only stores the viewport;
    /// `reapply_viewport_units` re-interns the boxes whose baked lengths depend on it.
    void ensure(html::Node* document_root, Viewport viewport = {});
    /// Window resize: re-intern boxes that resolved vw/vh/dvh/vmin/vmax against the previous
    /// viewport and mark the ones whose used style changed. A scrollbar gutter does not come
    /// through here; vw/vh stay on the full window (LAYOUT.md section 9).
    void reapply_viewport_units(LayoutContext& cx);
    void mark_needs_layout(html::Node* node, LayoutContext& cx);
    /// Re-derives the LayoutStyle this node's box paints from, without touching geometry: paint
    /// reads radii, border styles and the like out of the interned style, so a `Paint`-only diff
    /// still has to refresh it (docs/PERF.md I6, docs/PERF_REFACTOR.md stage 1). Nothing is marked
    /// layout-dirty, so the frame stays `laid_out == 0`.
    void refresh_paint_style(html::Node* node);
    void sync_text(html::Node* node, LayoutContext& cx);
    /// Re-resolves `<img src>` onto `Box::replaced_src` after an A-tier `set(...,"src")`.
    void sync_replaced_src(html::Node* node, LayoutContext& cx);
    void detach(html::Node* node);
    /// Breaks every name of the boxes `detach` took out of the tree and frees them. Called once
    /// the frame has laid out and written its new pins: a slot or a pin of a replaced result may
    /// still have named them, and `restore_pinned_places` writes through those names at the end of
    /// the frame that follows.
    /// @param live_lost set when a *live* box above them lost cached answers that named them: it
    /// has to be laid out again before its geometry is read.
    /// @return true when any box was actually freed (the rest of the tree did not move).
    bool release_detached(LayoutContext& cx, bool& live_lost);
    void relower(html::Node* node, LayoutContext& cx);
    /// Reconciles `node`'s child boxes with the DOM children it has now, and re-interns the style
    /// of a child listed in `reattach` (the cascade's `display` / `position` set). Keeping the boxes
    /// the DOM still has is what makes a structural change cost the host chain instead of the page.
    void sync_children(html::Node* node, const std::unordered_set<html::Node*>& reattach,
                       LayoutContext& cx);

private:
    Box* make();
    void build(html::Node* node, Box* parent);
    /// Every box under `box`, in pre-order. Used when a subtree leaves the tree.
    void collect_subtree(Box* box, std::vector<Box*>& out) const;
    /// Takes `box` and everything under it out of `by_node_` and hands the boxes to
    /// `release_detached`: they leave the tree now and the memory goes once the frame has written
    /// its new pins.
    void forget_subtree_boxes(Box& box);
    /// Rebuilds `box`'s children from `node`, keeping `box` itself. `build` does this for a box it
    /// has just made; this does it for a box that already exists and changed its formatting
    /// context, where the anonymous and table wrappers have to be made again.
    void rebuild_subtree(Box& box, html::Node* node);
    /// True when `node` or anything under it is `display: contents`: the child match cannot
    /// express a subtree that flattens into its grandparent, so the host falls back to `relower`.
    bool subtree_has_contents(html::Node* node) const;
    /// The nearest ancestor of `node`, including it, that has a box.
    Box* nearest_box_ancestor(html::Node* node) const;
    /// True when the boxes the host has are exactly the boxes this node's children ask for, in
    /// order, with nothing to add, drop, rebuild or re-intern.
    bool children_match(html::Node* node, Box& host,
                        const std::unordered_set<html::Node*>& reattach) const;
    /// CSS Pseudo-Elements: builds the box for `node`'s generated `::before` / `::after`, at the
    /// start or the end of `box`'s children.
    void build_pseudo(html::Node* node, Box& box, html::PseudoElement which, bool first);
    /// The characters the element itself paints: its own text, or the placeholder of a text
    /// control that holds no value.
    void append_element_text(html::Node* node, Box& box);
    void wrap_inlines(Box& box);
    /// CSS 2.1 section 17.2.1: wraps the children that cannot be children of this table part in
    /// the anonymous boxes that can (a row around cells, a cell around anything else).
    void fixup_table(Box& box);
    /// CSS 2.1 section 17.6.2: in the collapsing border model a grid line carries one border. The
    /// winner (wider wins; at equal width the box closer to the cell wins) is halved onto the two
    /// boxes that meet there, so a collapsed table lays out like a separate one with zero spacing.
    void resolve_collapsed_borders(Box& table);
    /// Re-derives a box's interned LayoutStyle from its node's computed style.
    void refresh_style(Box& box);
    /// The boxes a style change has to re-derive: this one, the text / anonymous boxes that take
    /// their style from it, its generated `::before` / `::after`, and - inside a collapsing table -
    /// every box the shared-border resolution rewrites. `mark_needs_layout` and
    /// `refresh_paint_style` differ only in what they do *after* this.
    void refresh_style_closure(Box& box, html::Node* node);
    /// Interns `node`'s style for `box`, and records whether that intern resolved a viewport unit
    /// (`Box::depends_on_viewport`, which a window resize reads). Every intern of the tree build
    /// goes through here, so this is also where the rebuild's style resolution is measured.
    void intern_into(Box& box, const html::Node* node, BoxRole role, DisplayOuter outer,
                     DisplayInner inner);

    std::vector<std::unique_ptr<Box>> owned_;
    /// Boxes `detach` took out of the tree, waiting for the frame to write its new pins.
    std::vector<Box*> detached_;
    std::unordered_map<html::Node*, Box*> by_node_;
    Box* root_ = nullptr;
    Viewport viewport_;
    std::string resource_base_;
};

}  // namespace layout
}  // namespace zui

#endif
