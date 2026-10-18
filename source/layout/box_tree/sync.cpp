#include "layout/box_tree/tree.h"
#include "layout/box_tree/detail.h"

#include "perf/perf_budget.h"
#include "image/image.h"
#include "layout/cache/layout_context.h"
#include "layout/cache/layout_invalidation.h"
#include "layout/intern_grid.h"
#include "layout/intern_style.h"
#include "layout/paint_map_internal.h"
#include "layout/style_table.h"
#include "trace/trace.h"

#include <string_view>

#include <algorithm>
#include <filesystem>

namespace zui {
namespace layout {

using namespace box_tree_detail;
void BoxTree::mark_needs_layout(html::Node* node, LayoutContext& cx) {
    Box* box = principal(node);
    if (box == nullptr) {
        return;
    }
    refresh_style_closure(*box, node);
    LayoutInvalidation::mark(*box, cx);
}

void BoxTree::refresh_paint_style(html::Node* node) {
    Box* box = principal(node);
    if (box == nullptr) {
        return;
    }
    refresh_style_closure(*box, node);
}

void BoxTree::refresh_style_closure(Box& box, html::Node* node) {
    // A style change re-interns this box's LayoutStyle. The layout key holds the style *pointer*
    // (LAYOUT.md section 2), so without this a changed `width` never reaches the key and the box
    // keeps its old geometry: measured with a `set_style` step, the computed style said 400px
    // while the laid-out border box stayed 200.
    refresh_style(box);
    // Text and anonymous boxes derive their style from this element, including a text run wrapped
    // in an anonymous block. Element children wait for their own mark: inheritance already listed
    // them when their computed style moved.
    const auto refresh_derived = [this](auto&& self, Box& host) -> void {
        for (Box* child : host.children) {
            if (child == nullptr || child->role == BoxRole::Element) {
                continue;
            }
            refresh_style(*child);
            if (child->role == BoxRole::Anonymous) {
                self(self, *child);
            }
        }
    };
    refresh_derived(refresh_derived, box);
    // A generated element has a node of its own, so the loop above does not reach it.
    for (int i = 0; i < static_cast<int>(html::PseudoElement::Count); ++i) {
        html::Node* generated = node->pseudo_node[i].get();
        if (generated == nullptr) {
            continue;
        }
        if (Box* pseudo_box = principal(generated)) {
            refresh_style(*pseudo_box);
            refresh_derived(refresh_derived, *pseudo_box);
        }
    }
    // A collapsing table resolves its shared borders onto the boxes around each grid line, so a
    // change anywhere inside it has to redo that resolution. It runs on the authored borders, which
    // means re-interning the whole table from its nodes first - the resolution is not idempotent
    // because it rewrites the very borders it read.
    for (Box* up = &box; up != nullptr; up = up->parent) {
        if (up->style_ptr == nullptr) {
            continue;
        }
        const TablePart part = up->style().table_part;
        if ((part != TablePart::Table && part != TablePart::InlineTable) ||
            !up->style().border_collapse) {
            continue;
        }
        std::vector<Box*> stack{up};
        while (!stack.empty()) {
            Box* current = stack.back();
            stack.pop_back();
            refresh_style(*current);
            for (Box* child : current->children) {
                if (child != nullptr) {
                    stack.push_back(child);
                }
            }
        }
        resolve_collapsed_borders(*up);
        break;
    }
}

void BoxTree::refresh_style(Box& box) {
    if (box.style_ptr == nullptr) {
        return;
    }
    // `builder` interns a text run and an anonymous wrapper from the element that owns them.
    // The run's own node is null (the element's characters) or a `#text` node, and `#text` is
    // not cascaded, so its missing `font-size` is the 16px initial. Re-interning from that node
    // left the advances at 16px while paint used the element's new size: growing the reader face
    // drew 23px glyphs on 16px advances and the characters overlapped.
    const html::Node* source = box.node;
    const bool generated = source != nullptr && source->computed != nullptr &&
                           source->tag.size() >= 2 && source->tag[0] == ':' && source->tag[1] == ':';
    if (box.role != BoxRole::Element && !generated) {
        source = nullptr;
        for (const Box* up = box.parent; up != nullptr; up = up->parent) {
            if (up->node != nullptr && up->node->computed != nullptr && up->node->tag != "#text") {
                source = up->node;
                break;
            }
        }
    }
    if (source == nullptr) {
        return;
    }
    const LayoutStyle& current = *box.style_ptr;
    intern_into(box, source, box.role, current.outer, current.inner);
}

void BoxTree::reapply_viewport_units(LayoutContext& cx) {
    if (root_ == nullptr) {
        return;
    }
    std::vector<Box*> changed;
    std::vector<Box*> stack{root_};
    while (!stack.empty()) {
        Box* box = stack.back();
        stack.pop_back();
        if (box == nullptr) {
            continue;
        }
        // Text runs with no node of their own were interned from the element. Refreshing the
        // element covers the lengths it owns; a run that baked a viewport unit itself is refreshed
        // only when it still has a node (generated placeholder text).
        if (box->depends_on_viewport && box->node != nullptr && box->style_ptr != nullptr) {
            const std::shared_ptr<const LayoutStyle> before = box->style_ptr;
            refresh_style(*box);
            if (box->style_ptr != before) {
                changed.push_back(box);
            }
        }
        // `w` descriptors, `sizes`, and `<picture>` media conditions are resolved against the
        // viewport. A plain `<img src>` has nothing to re-select. An `x`-only srcset comes back
        // as the same URL, so the content version stays put.
        if (box->node != nullptr && box->node->tag == "img" &&
            (box->node->attrs.find("srcset") != box->node->attrs.end() ||
             box->node->attrs.find("sizes") != box->node->attrs.end() ||
             (box->node->parent != nullptr && box->node->parent->tag == "picture"))) {
            const std::string url =
                select_img_url(box->node, viewport_.width, viewport_.height, 1.f);
            std::string resolved = resolve_replaced_src(url, resource_base_);
            if (resolved != box->replaced_src) {
                if (!resolved.empty()) {
                    image::invalidate(resolved);
                }
                if (!box->replaced_src.empty()) {
                    image::invalidate(box->replaced_src);
                }
                box->replaced_src = std::move(resolved);
                box->replaced_seen = false;
                box->content += 1;
                LayoutInvalidation::mark(*box, cx);
            }
        }
        for (Box* child : box->children) {
            stack.push_back(child);
        }
    }
    // Collapsing borders are written onto the interned style. A viewport length inside that table
    // has to be re-interned from the authored borders before the shared edges are resolved again.
    for (Box* box : changed) {
        for (Box* up = box; up != nullptr; up = up->parent) {
            if (up->style_ptr == nullptr) {
                continue;
            }
            const TablePart part = up->style().table_part;
            if ((part != TablePart::Table && part != TablePart::InlineTable) ||
                !up->style().border_collapse) {
                continue;
            }
            std::vector<Box*> table_stack{up};
            while (!table_stack.empty()) {
                Box* current = table_stack.back();
                table_stack.pop_back();
                refresh_style(*current);
                for (Box* child : current->children) {
                    if (child != nullptr) {
                        table_stack.push_back(child);
                    }
                }
            }
            resolve_collapsed_borders(*up);
            break;
        }
    }
    for (Box* box : changed) {
        LayoutInvalidation::mark(*box, cx);
    }
}

void BoxTree::sync_text(html::Node* node, LayoutContext& cx) {
    Box* box = principal(node);
    if (box == nullptr || node == nullptr) {
        return;
    }
    box->text = node->text;
    box->content += 1;
    // The characters this element paints, and the node whose font and colour they carry: its own
    // text, or - while a text control is empty - the placeholder run `append_element_text` built.
    // Typing into the control swaps the run over to the element's own text, and clearing the value
    // swaps it back; without the swap the placeholder kept painting and the typed text never
    // appeared.
    html::Node* source = node;
    std::string characters = node->text;
    if (characters.empty() && shows_placeholder(node)) {
        html::Node* generated =
            node->pseudo_node[static_cast<int>(html::PseudoElement::Placeholder)].get();
        if (generated != nullptr && !generated->text.empty()) {
            source = generated;
            characters = generated->text;
        }
    }
    Box* carried = nullptr;
    for (Box* child : box->children) {
        if (child == nullptr || child->role != BoxRole::Text) {
            continue;
        }
        // The element's own run: either the plain one, or the placeholder run, which is the one
        // holding a generated node.
        const bool own = child->node == nullptr || (child->node->tag.rfind("::", 0) == 0);
        if (own) {
            carried = child;
            break;
        }
    }
    if (carried != nullptr) {
        html::Node* wanted = source == node ? nullptr : source;
        if (carried->node != wanted) {
            carried->node = wanted;
            intern_into(*carried, source, BoxRole::Text, DisplayOuter::Inline, DisplayInner::Flow);
        }
        carried->text = std::move(characters);
        carried->content += 1;
        LayoutInvalidation::mark(*carried, cx);
        return;
    }
    // The element was empty when the tree was built, so it has no text box to carry the run
    // (`append_element_text` skips an empty string). An application that fills an empty element -
    // the usual `text(id, value)` on a placeholder `<h2>` - has to get one here, or the characters
    // are never laid out: the element kept a zero box and no line. It goes where the build-time
    // path puts the element's own text, which is before the `::after` box.
    const std::size_t children_before = box->children.size();
    append_element_text(node, *box);
    if (box->children.size() > children_before) {
        Box* text = box->children.back();
        const auto after =
            std::find_if(box->children.begin(), box->children.end(), [text](const Box* child) {
                return child != text && child != nullptr &&
                       child->generated == html::PseudoElement::After;
            });
        if (after != box->children.end()) {
            box->children.pop_back();
            box->children.insert(after, text);
        }
    }
    LayoutInvalidation::mark(*box, cx);
}

void BoxTree::sync_replaced_src(html::Node* node, LayoutContext& cx) {
    Box* box = principal(node);
    if (box == nullptr || node == nullptr || !box->is_replaced) {
        return;
    }
    std::string src;
    if (node->tag == "img") {
        src = select_img_url(node, viewport_.width, viewport_.height, 1.f);
    } else {
        auto it = node->attrs.find("src");
        if (it != node->attrs.end()) {
            src = it->second;
        }
    }
    std::string resolved = resolve_replaced_src(src, resource_base_);
    if (!resolved.empty()) {
        image::invalidate(resolved);
    }
    if (!box->replaced_src.empty() && box->replaced_src != resolved) {
        image::invalidate(box->replaced_src);
    }
    box->replaced_src = std::move(resolved);
    box->replaced_seen = false;
    box->content += 1;
    LayoutInvalidation::mark(*box, cx);
}

void BoxTree::detach(html::Node* node) {
    Box* box = principal(node);
    if (box == nullptr || box->parent == nullptr) {
        return;
    }
    auto& kids = box->parent->children;
    kids.erase(std::remove(kids.begin(), kids.end(), box), kids.end());
    forget_subtree_boxes(*box);
}

void BoxTree::forget_subtree_boxes(Box& box) {
    // The whole subtree loses its boxes, so every one of them loses its mapping - not only the box
    // this was called with. Leaving a descendant mapped would answer a later `principal()` with a
    // box the tree no longer has, and a freed `html::Node*` is a key a new node can be allocated
    // onto. Boxes without a node of their own (anonymous boxes, an element's own text run) were
    // never mapped and are simply collected here.
    //
    // Callers that keep a descendant alive must reparent it out of this subtree first
    // (`sync_children` does): otherwise `collect_subtree` would queue a live box into `detached_`.
    std::vector<Box*> boxes;
    collect_subtree(&box, boxes);
    for (Box* current : boxes) {
        if (current == nullptr || current->node == nullptr) {
            continue;
        }
        const auto it = by_node_.find(current->node);
        if (it != by_node_.end() && it->second == current) {
            by_node_.erase(it);
        }
    }
    // The boxes stay alive until the frame that follows has written its new pins: a slot or a pin
    // of a result this frame replaces may still name them (see `release_detached`).
    detached_.insert(detached_.end(), boxes.begin(), boxes.end());
}

/// Moves @p child under @p new_parent: unlink from the previous parent's `children`, then set
/// `child->parent`. `sync_children` must do this before it forgets an old anonymous wrapper that
/// still listed the kept element boxes.

void BoxTree::collect_subtree(Box* box, std::vector<Box*>& out) const {
    if (box == nullptr) {
        return;
    }
    out.push_back(box);
    for (Box* child : box->children) {
        collect_subtree(child, out);
    }
}

bool BoxTree::release_detached(LayoutContext& cx, bool& live_lost) {
    if (detached_.empty()) {
        live_lost = false;
        return false;
    }
    if (root_ != nullptr) {
        live_lost = cx.forget_boxes(*root_, detached_);
    } else {
        live_lost = false;
    }
    const std::unordered_set<const Box*> gone(detached_.begin(), detached_.end());
    // The placement walk keeps the containment entry of every box it skips, so the entries of the
    // boxes about to be freed have to go with them (docs/PERF_REFACTOR.md stage P).
    paint_map_detail::forget_frames(gone);
    perf::note_boxes_destroyed(gone.size());
    detached_.clear();
    owned_.erase(std::remove_if(owned_.begin(), owned_.end(),
                                [&gone](const std::unique_ptr<Box>& box) {
                                    return box != nullptr && gone.count(box.get()) != 0;
                                }),
                 owned_.end());
    return true;
}

void BoxTree::relower(html::Node* node, LayoutContext& cx) {
    if (node == nullptr) {
        return;
    }
    Box* box = principal(node);
    if (box == nullptr) {
        // `display: none` never had a box. Leaving `none` (the reader opening) has to lower
        // into the ancestor that already has one; rebuilding only this node no-ops.
        if (outer_of(node) == DisplayOuter::None) {
            return;
        }
        const std::string* display = node->style("display");
        if (display != nullptr && *display == "contents") {
            for (auto& ch : node->children) {
                relower(ch.get(), cx);
            }
            return;
        }
        // A descendant of `display: none` has no box yet. Walking up to the first visible
        // ancestor rebuilds that ancestor (PERF.md I4). The work waits until the subtree is shown.
        for (html::Node* hidden = node->parent; hidden != nullptr; hidden = hidden->parent) {
            if (outer_of(hidden) == DisplayOuter::None) {
                return;
            }
        }
        html::Node* host = node->parent;
        while (host != nullptr && principal(host) == nullptr) {
            host = host->parent;
        }
        if (host != nullptr) {
            relower(host, cx);
        }
        return;
    }
    if (outer_of(node) == DisplayOuter::None) {
        Box* parent = box->parent;
        if (parent != nullptr) {
            auto& kids = parent->children;
            kids.erase(std::remove(kids.begin(), kids.end(), box), kids.end());
            // The child list changed without a dirty mark, which is the other input of the
            // settled `subtree_dirty` (see `LayoutContext::note_tree_edit`).
            cx.note_tree_edit();
        }
        forget_subtree_boxes(*box);
        // The parent's stored results recorded the child that just left, so it is the one that has
        // to miss - the box itself is on its way out of the tree.
        if (parent != nullptr) {
            LayoutInvalidation::drop_chain(*parent, cx);
        }
        return;
    }
    for (Box* child : box->children) {
        if (child != nullptr) {
            forget_subtree_boxes(*child);
        }
    }
    cx.note_tree_edit();
    rebuild_subtree(*box, node);
    // This host is the minimal enclosing ancestor LAYOUT.md section 6 asks for, and the slots it
    // had were interned around the children that just left. A `set_model` that does not change
    // text would otherwise be answered by the geometry of the old child list.
    LayoutInvalidation::drop_chain(*box, cx);
}


void BoxTree::sync_children(html::Node* node, const std::unordered_set<html::Node*>& reattach,
                            LayoutContext& cx) {
    Box* host = principal(node);
    if (host == nullptr || subtree_has_contents(node)) {
        // The host has no box of its own, or something under it flattens into this level: the child
        // match cannot express either one, so the subtree is lowered again and whichever ancestor
        // ends up rebuilding is the box that has to miss.
        relower(node, cx);
        return;
    }
    if (select_light_dom_hidden(node)) {
        // A closed select keeps no light-DOM child boxes (see `select_light_dom_hidden`). The
        // options built while it was open are reattached on close (their `position` leaves the
        // `select[open]` rule), so the child match below would keep them and the closed control
        // grew to the whole list.
        if (!host->children.empty()) {
            cx.note_tree_edit();
            for (Box* child : host->children) {
                if (child != nullptr) {
                    forget_subtree_boxes(*child);
                }
            }
            host->children.clear();
        }
        return;
    }
    if (children_match(node, *host, reattach)) {
        // The DOM asks for the list the host already has: nothing to add, drop or rebuild, and no
        // reattached child. This is the `set visible` frame, and it invalidates nothing.
        return;
    }

    const bool host_reattached = reattach.count(node) != 0;
    // From here on the host's child list is rebuilt, which is a tree edit even when the walk below
    // ends up keeping every box (`LayoutContext::note_tree_edit`).
    cx.note_tree_edit();
    std::vector<Box*> previous = std::move(host->children);
    host->children.clear();
    std::vector<Box*> generated;
    for (Box* child : previous) {
        if (child != nullptr && child->generated != html::PseudoElement::Count) {
            generated.push_back(child);
        }
    }
    bool rebuilt = false;   // the tree changed, so the closing passes run again
    bool restyled = false;  // a kept box interned a different style

    if (host_reattached) {
        // Generated content belongs to the element, not to a DOM child, so the match below cannot
        // see it: it leaves and is made again in the order `build` makes it.
        for (Box* child : generated) {
            forget_subtree_boxes(*child);
        }
        generated.clear();
        build_pseudo(node, *host, html::PseudoElement::Before, true);
        build_pseudo(node, *host, html::PseudoElement::Marker, true);
        rebuilt = true;
    }

    for (auto& entry : node->children) {
        html::Node* child = entry.get();
        if (child == nullptr) {
            continue;
        }
        Box* kept = principal(child);
        if (!node_needs_a_box(*child)) {
            // `display: none` is gone even though the node is still in the DOM, and an empty text
            // node never had a box.
            if (kept != nullptr) {
                forget_subtree_boxes(*kept);
                rebuilt = true;
            }
            continue;
        }
        if (kept == nullptr) {
            build(child, host);
            rebuilt = true;
            continue;
        }
        if (child->tag != "#text") {
            // `style().outer` is the blockified value. A grid item `<button>` is `inline-block`
            // in CSS and `Block` on the box; comparing the specified display rebuilt the inner
            // `cell-inner` on every `set_model` while the button's cached result still named the
            // detached span, and `forget_boxes` then cleared the pin. The cell kept its grid
            // position and lost its border box, so hit testing missed it.
            const bool context_changed = used_outer(child, host) != kept->style().outer ||
                                         inner_of(child) != kept->style().inner;
            if (context_changed) {
                // Same node, different formatting context: the box stays, everything under it is
                // made again, because the anonymous and table wrappers follow the inner display.
                for (Box* old : kept->children) {
                    if (old != nullptr) {
                        forget_subtree_boxes(*old);
                    }
                }
                rebuild_subtree(*kept, child);
                // The box's slots still name the children just detached. Drop them here, or the
                // next ask is a cache hit and `forget_boxes` clears the pin after placement.
                LayoutInvalidation::drop_chain(*kept, cx);
                rebuilt = true;
            } else if (reattach.count(child) != 0) {
                // `flush_style` never marks a reattach, so the style is interned here: a `position`
                // change keeps the box and only what reads the used values moves. New generated
                // content is not a DOM child either, so that case rebuilds this child alone.
                const std::shared_ptr<const LayoutStyle> before = kept->style_ptr;
                const auto generated_of = [](const Box& box) {
                    return box.generated != html::PseudoElement::Count;
                };
                const bool generated_changed = std::any_of(
                    kept->children.begin(), kept->children.end(),
                    [&generated_of](Box* box) { return box != nullptr && generated_of(*box); });
                if (generated_changed) {
                    for (Box* old : kept->children) {
                        if (old != nullptr) {
                            forget_subtree_boxes(*old);
                        }
                    }
                    rebuild_subtree(*kept, child);
                    LayoutInvalidation::drop_chain(*kept, cx);
                    rebuilt = true;
                } else {
                    refresh_style(*kept);
                    if (kept->style_ptr != before) {
                        restyled = true;
                    }
                }
            }
        }
        // Reparent before the host list grows and before old anonymous wrappers are forgotten:
        // otherwise `collect_subtree` on those wrappers still walks this kept box into `detached_`.
        reparent_box(kept, host);
        host->children.push_back(kept);
    }

    if (!generated.empty()) {
        // Keep Marker → Before → content → After. Insert Before first, then Marker at begin
        // so Marker ends up in front; After (and any other generated) go at the end.
        for (Box* child : generated) {
            reparent_box(child, host);
            if (child->generated == html::PseudoElement::Before) {
                host->children.insert(host->children.begin(), child);
            }
        }
        for (Box* child : generated) {
            if (child->generated == html::PseudoElement::Marker) {
                host->children.insert(host->children.begin(), child);
            }
        }
        for (Box* child : generated) {
            if (child->generated != html::PseudoElement::Before &&
                child->generated != html::PseudoElement::Marker) {
                host->children.push_back(child);
            }
        }
    }

    {
        const std::unordered_set<const Box*> live(host->children.begin(), host->children.end());
        for (Box* old : previous) {
            if (old != nullptr && old->generated == html::PseudoElement::Count &&
                live.count(old) == 0) {
                // Children that stayed live were unlinked above; only the wrapper itself remains.
                forget_subtree_boxes(*old);
                rebuilt = true;
            }
        }
    }

    if (rebuilt) {
        wrap_inlines(*host);
        fixup_table(*host);
        mark_table_structure(*host);
        if (host->style_ptr != nullptr && (host->style().table_part == TablePart::Table ||
                                           host->style().table_part == TablePart::InlineTable)) {
            resolve_collapsed_borders(*host);
        }
        zero_collapsed_row(*host);
        append_element_text(node, *host);
    }
    if (host_reattached) {
        build_pseudo(node, *host, html::PseudoElement::After, false);
    }

    if (rebuilt || restyled) {
        // This host is the minimal enclosing ancestor LAYOUT.md section 6 asks for. Only the drop:
        // the flags it sets carry the frame down to this host, and the contribution walk `mark`
        // runs would lay the host out early and settle them before the root has been asked - which
        // is exactly what a fixed-position child needs to be collected.
        LayoutInvalidation::drop_chain(*host, cx);
    }
}

}  // namespace layout
}  // namespace zui
