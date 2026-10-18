#include "layout/box_tree/tree.h"
#include "layout/box_tree/detail.h"

#include "perf/perf_budget.h"
#include "image/image.h"
#include "layout/cache/layout_context.h"
#include "layout/cache/layout_invalidation.h"
#include "layout/intern_grid.h"
#include "layout/intern_style.h"
#include "layout/style_table.h"
#include "trace/trace.h"

#include <string_view>

#include <algorithm>
#include <filesystem>

namespace zui {
namespace layout {

using namespace box_tree_detail;
bool BoxTree::subtree_has_contents(html::Node* node) const {
    if (node == nullptr) {
        return false;
    }
    std::vector<html::Node*> stack{node};
    while (!stack.empty()) {
        html::Node* current = stack.back();
        stack.pop_back();
        const std::string* display = current->style("display");
        if (display != nullptr && *display == "contents") {
            return true;
        }
        for (auto& child : current->children) {
            if (child != nullptr) {
                stack.push_back(child.get());
            }
        }
    }
    return false;
}

Box* BoxTree::nearest_box_ancestor(html::Node* node) const {
    for (html::Node* current = node; current != nullptr; current = current->parent) {
        if (Box* box = principal(current); box != nullptr) {
            return box;
        }
    }
    return nullptr;
}

bool BoxTree::children_match(html::Node* node, Box& host,
                             const std::unordered_set<html::Node*>& reattach) const {
    // A box a DOM child owns carries that child (an element's own text run carries no node, and a
    // generated `::before` / `::after` box carries the element's pseudo node). Those two are the
    // boxes the closing passes add, and they sit between the matched ones.
    const auto from_dom_child = [node](const Box& box) {
        const html::Node* source = box.node;
        return source != nullptr && source->parent == node && source->tag.rfind("::", 0) != 0;
    };
    auto it = host.children.begin();
    for (auto& entry : node->children) {
        html::Node* child = entry.get();
        if (child == nullptr) {
            continue;
        }
        if (reattach.count(child) != 0) {
            return false;
        }
        Box* box = principal(child);
        if (box == nullptr) {
            if (node_needs_a_box(*child)) {
                return false;
            }
            continue;
        }
        // `style().outer` is the blockified value `build` stored. Comparing it with the specified
        // display made every kept inline-block flex/grid item fail this match.
        if (child->tag != "#text" &&
            (used_outer(child, &host) != box->style().outer ||
             inner_of(child) != box->style().inner)) {
            return false;
        }
        while (it != host.children.end() && !from_dom_child(**it)) {
            ++it;
        }
        if (it == host.children.end() || *it != box) {
            return false;
        }
        ++it;
    }
    while (it != host.children.end() && !from_dom_child(**it)) {
        ++it;
    }
    return it == host.children.end();
}

}  // namespace layout
}  // namespace zui
