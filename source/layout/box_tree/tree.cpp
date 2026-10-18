#include "layout/box_tree/tree.h"

#include "perf/perf_budget.h"
#include "image/image.h"
#include "layout/box_tree/detail.h"
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

namespace box_tree_detail {

std::string resolve_replaced_src(const std::string& src, const std::string& resource_base) {
    if (src.empty()) {
        return {};
    }
    const bool remote = src.find("://") != std::string::npos;
    const std::filesystem::path path(src);
    if (!remote && !path.is_absolute() && !resource_base.empty()) {
        return (std::filesystem::path(resource_base) / path).lexically_normal().string();
    }
    return src;
}

DisplayOuter outer_of(const html::Node* node) {
    if (node == nullptr) {
        return DisplayOuter::Block;
    }
    const std::string* d = node->style("display");
    if (d == nullptr) {
        return tag_is_inline(node->tag) ? DisplayOuter::Inline : DisplayOuter::Block;
    }
    if (*d == "none") {
        return DisplayOuter::None;
    }
    // CSS Ruby: ruby/ruby-text stay inline-level. `display:ruby` is also marked atomic_inline so
    // layout_ruby can stack annotations above the base.
    if (*d == "ruby" || *d == "ruby-text" || *d == "ruby-base" || *d == "ruby-base-container" ||
        *d == "ruby-text-container") {
        return DisplayOuter::Inline;
    }
    if (d->find("inline") != std::string::npos && d->find("block") == std::string::npos &&
        *d != "inline-block") {
        return DisplayOuter::Inline;
    }
    if (*d == "inline-block" || *d == "inline-flex" || *d == "inline-grid") {
        return DisplayOuter::Inline;
    }
    return DisplayOuter::Block;
}

DisplayOuter used_outer(const html::Node* node, const Box* parent) {
    const DisplayOuter specified = outer_of(node);
    // CSS Display 3 section 2.7: a flex or grid item is blockified. `build` writes that used
    // value onto the box. Callers that compare a live box with the element's specified display
    // have to use the same value, or every kept inline-block item looks like a new formatting
    // context the next time its parent syncs.
    if (specified != DisplayOuter::None && parent != nullptr && parent->style_ptr != nullptr &&
        (parent->style().inner == DisplayInner::Flex ||
         parent->style().inner == DisplayInner::Grid)) {
        return DisplayOuter::Block;
    }
    return specified;
}

/// The controls that have an inner text editor. A checkbox can carry a `placeholder` attribute in
/// the DOM but never shows it, so the attribute alone is not enough.
bool shows_placeholder(const html::Node* node) {
    if (node == nullptr) {
        return false;
    }
    if (node->tag == "textarea") {
        return true;
    }
    if (node->tag != "input") {
        return false;
    }
    const auto it = node->attrs.find("type");
    const std::string type = it == node->attrs.end() ? std::string() : it->second;
    return type.empty() || type == "text" || type == "search" || type == "email" || type == "url" ||
           type == "tel" || type == "password" || type == "number";
}

DisplayInner inner_of(const html::Node* node) {
    if (node == nullptr) {
        return DisplayInner::Flow;
    }
    const std::string* d = node->style("display");
    // A table box is the one table part that gets an algorithm of its own: the rows, cells and
    // captions stay flow boxes that the table lays out (CSS 2.1 section 17.2). The tag fallback
    // mirrors `intern_style`'s, because the UA style only supplies the default `display`.
    if ((d != nullptr && (*d == "table" || *d == "inline-table")) || node->tag == "table") {
        return DisplayInner::Table;
    }
    if (d != nullptr && d->find("flex") != std::string::npos) {
        return DisplayInner::Flex;
    }
    if (d != nullptr && d->find("grid") != std::string::npos) {
        return DisplayInner::Grid;
    }
    // CSS Multi-column Layout section 3: a box is a multi-column container when `column-count` or
    // `column-width` is not `auto`.
    if (const std::string* count = node->style("column-count")) {
        if (*count != "auto") {
            return DisplayInner::Multicol;
        }
    }
    if (const std::string* width = node->style("column-width")) {
        if (*width != "auto") {
            return DisplayInner::Multicol;
        }
    }
    if (const std::string* columns = node->style("columns")) {
        if (*columns != "auto") {
            return DisplayInner::Multicol;
        }
    }
    return DisplayInner::Flow;
}

/// A closed `<select>` renders nothing from its light DOM: Chromium swaps the subtree for a
/// shadow listbox, so an `option` keeps `display: block` (measured: a closed select's option
/// reports a zero rect while its computed display is block) but never gets a box. The engine has
/// no shadow tree, so the stand-in is to skip the subtree while the control is closed; opening it
/// (`select[open]`, Interaction's popup stand-in) builds the option boxes. Every site that walks
/// a select's children has to ask this - a rebuild that ignored it left the options in flow
/// inside the closed control, which grew it to the whole list (the contacts dialog's group
/// field after the popup was dismissed).
bool select_light_dom_hidden(const html::Node* node) {
    return html::is_select(node) && !html::select_is_open(node);
}

}  // namespace box_tree_detail

Box* BoxTree::make() {
    zui::trace::Zone zone("box.alloc", zui::trace::Category::Engine);
    owned_.push_back(std::make_unique<Box>());
    perf::note_box_created();
    return owned_.back().get();
}

void BoxTree::intern_into(Box& box, const html::Node* node, BoxRole role, DisplayOuter outer,
                          DisplayInner inner) {
    // Q1 of `布局成本结构性修复方案.md`: how much of a box-tree rebuild is resolving the authored
    // style. The zone has to wrap the `intern_style` call itself: a helper that takes the interned
    // style as a parameter evaluates it before entering the helper, and a zone in there would time
    // nothing but the assignment.
    zui::trace::Zone zone("box.intern", zui::trace::Category::Engine);
    perf::note_style_intern(node != nullptr ? std::string_view(node->id) : std::string_view{});
    box.style_ptr = intern_style(node, role, outer, inner, viewport_);
    box.depends_on_viewport = intern_used_viewport_unit();
    pin_unrendered_source(box);
}

void BoxTree::reset() {
    // The placement walk keeps a containment entry per box between frames; the boxes are about to
    // go, so their entries go too (docs/PERF_REFACTOR.md stage P).
    paint_map_detail::clear_frames();
    owned_.clear();
    by_node_.clear();
    root_ = nullptr;
}

/// CSS Pseudo-Elements: a node's `::before` / `::after` are boxes of its own, placed around the
/// boxes its real children produce. The cascade already decided whether the generated element
/// exists at all (`content` other than `none` / `normal`), so an absent node means no box.

void BoxTree::set_resource_base(std::string document_path) {
    if (document_path.empty()) {
        resource_base_.clear();
        return;
    }
    const std::filesystem::path file(document_path);
    resource_base_ = file.parent_path().string();
}

Box* BoxTree::principal(html::Node* node) const {
    auto it = by_node_.find(node);
    return it == by_node_.end() ? nullptr : it->second;
}

void BoxTree::ensure(html::Node* document_root, Viewport viewport) {
    viewport_ = viewport;
    if (root_ == nullptr && document_root != nullptr) {
        build(document_root, nullptr);
    }
}


bool collapsible_blank(const Box& box) {
    if (box.role != BoxRole::Text || box.style_ptr == nullptr || !blank_text(box.text)) {
        return false;
    }
    return box.style().white_space == WhiteSpace::Normal ||
           box.style().white_space == WhiteSpace::Nowrap;
}


void reparent_box(Box* child, Box* new_parent) {
    if (child == nullptr || child->parent == new_parent) {
        return;
    }
    if (child->parent != nullptr) {
        auto& kids = child->parent->children;
        kids.erase(std::remove(kids.begin(), kids.end(), child), kids.end());
    }
    child->parent = new_parent;
}


/// Whether this node asks for a box of its own: `display: none` never has one, and an empty text
/// node does not either.
bool node_needs_a_box(const html::Node& node) {
    if (node.tag.empty() || node.tag == "#text") {
        return !node.text.empty();
    }
    return outer_of(&node) != DisplayOuter::None;
}

}  // namespace layout
}  // namespace zui
