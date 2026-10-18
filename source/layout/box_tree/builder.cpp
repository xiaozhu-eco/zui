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

/// Remember a `position: fixed` box on itself and on ancestors. A later hit outside a clipping
/// ancestor still walks this chain (`world_hit.cpp`). The bit stays set after the fixed box is
/// removed; the walk then finds no box and costs only the depth of the old chain.
void note_fixed_subtree(Box* box) {
    if (box == nullptr) {
        return;
    }
    bool marked = box->style_ptr != nullptr && box->style().position_fixed;
    if (!marked) {
        for (const Box* child : box->children) {
            if (child != nullptr && child->subtree_has_fixed) {
                marked = true;
                break;
            }
        }
    }
    if (!marked) {
        return;
    }
    box->subtree_has_fixed = true;
    for (Box* ancestor = box->parent; ancestor != nullptr && !ancestor->subtree_has_fixed;
         ancestor = ancestor->parent) {
        ancestor->subtree_has_fixed = true;
    }
}

void BoxTree::build_pseudo(html::Node* node, Box& box, html::PseudoElement which, bool first) {
    if (node == nullptr || which == html::PseudoElement::Placeholder) {
        return;
    }
    html::Node* generated = node->pseudo_node[static_cast<int>(which)].get();
    if (generated == nullptr) {
        return;
    }
    const size_t before = box.children.size();
    build(generated, &box);
    // The boxes just appended are the generated element and its contents: they carry a pseudo node
    // rather than a child of the element, so a later child match has to recognise them.
    for (std::size_t i = before; i < box.children.size(); ++i) {
        if (box.children[i] != nullptr) {
            box.children[i]->generated = which;
        }
    }
    if (first && box.children.size() > before) {
        std::rotate(box.children.begin(),
                    box.children.begin() + static_cast<std::ptrdiff_t>(before), box.children.end());
    }
}

/// The characters the element itself paints. A text control with no value paints its placeholder
/// instead; Chromium renders that as a `::placeholder` run inside the control, so the run is built
/// from the generated element and keeps its own colour - and its line box, which is what gives the
/// control a baseline at all.
void BoxTree::append_element_text(html::Node* node, Box& box) {
    zui::trace::Zone zone("box.text", zui::trace::Category::Engine);
    if (node == nullptr || box.is_replaced) {
        return;
    }
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
    if (characters.empty()) {
        return;
    }
    for (Box* child : box.children) {
        if (child != nullptr && child->role == BoxRole::Text) {
            return;
        }
    }
    Box* text = make();
    text->parent = &box;
    text->role = BoxRole::Text;
    text->text = std::move(characters);
    // A generated node keeps its own style, so the painter must not fall back to the parent's
    // colour for this run. A real element's own text keeps the old shape (no node on the box).
    text->node = source == node ? nullptr : source;
    intern_into(*text, source, BoxRole::Text, DisplayOuter::Inline, DisplayInner::Flow);
    text->depends_on_font = true;
    if (box.style().inner == DisplayInner::Flex) {
        text->is_flex_item = true;
    }
    if (box.style().inner == DisplayInner::Grid) {
        text->is_grid_item = true;
    }
    box.children.push_back(text);
}


void BoxTree::build(html::Node* node, Box* parent) {
    if (node == nullptr) {
        return;
    }
    if (!node->tag.empty() && node->tag != "#text") {
        const DisplayOuter specified = outer_of(node);
        if (specified == DisplayOuter::None) {
            return;
        }
        if (node->style("display") != nullptr && *node->style("display") == "contents") {
            for (auto& ch : node->children) {
                build(ch.get(), parent);
            }
            return;
        }
        const DisplayOuter outer = used_outer(node, parent);
        Box* box = make();
        box->parent = parent;
        box->node = node;
        box->role = BoxRole::Element;
        const DisplayInner inner = inner_of(node);
        intern_into(*box, node, BoxRole::Element, outer, inner);
        // `<img>` and generated `::before`/`::after` with `content: url(...)` (attrs["src"]).
        const bool content_image = node->attrs.find("src") != node->attrs.end() &&
                                   !node->attrs.at("src").empty() &&
                                   (node->tag == "img" || node->tag.rfind("::", 0) == 0);
        const bool image_backed = node->tag == "img" || content_image;
        // The embedded elements are replaced boxes too. `width` / `height` only apply to a replaced
        // element, so while they were plain inlines the UA defaults were ignored: `audio` came out
        // 0x22 (a line box) where the browser has its UA 300x54, and `iframe`/`video`/`canvas`
        // ignored the page's own sizes. Only the image-backed ones carry `replaced_src` - an
        // `iframe src=about:blank` is not an image to decode.
        const auto replaced_element = [](const std::string& tag) {
            return tag == "iframe" || tag == "embed" || tag == "object" || tag == "video" ||
                   tag == "canvas" || tag == "audio";
        };
        box->is_replaced = image_backed || replaced_element(node->tag);
        {
            // `display: inline` is the only inline-level value that stays a pure inline box: an
            // inline-block / inline-flex / inline-grid (and a replaced inline) establishes a box
            // of its own.
            const std::string* d = node->style("display");
            const std::string display = d != nullptr ? *d : std::string();
            box->atomic_inline =
                outer == DisplayOuter::Inline &&
                (inner != DisplayInner::Flow || box->is_replaced || display == "inline-block" ||
                 display == "inline-flex" || display == "inline-grid" ||
                 display == "inline-table" || display == "ruby");
        }
        if (parent != nullptr) {
            box->is_flex_item = parent->style().inner == DisplayInner::Flex;
            box->is_grid_item = parent->style().inner == DisplayInner::Grid;
            parent->children.push_back(box);
        }
        by_node_[node] = box;
        if (image_backed) {
            std::string url;
            if (node->tag == "img") {
                // `<img srcset>` / `<picture>` choose a candidate. `::before` content images keep
                // the url the cascade already wrote into `src`.
                url = select_img_url(node, viewport_.width, viewport_.height, 1.f);
            } else {
                auto it = node->attrs.find("src");
                if (it != node->attrs.end()) {
                    url = it->second;
                }
            }
            if (!url.empty()) {
                box->replaced_src = resolve_replaced_src(url, resource_base_);
            }
            // `content: url()` is a local sync decode (not a network <img>). Skip the first-frame
            // 300×150 observation protocol so a single difftest layout sees intrinsic size.
            if (content_image) {
                box->replaced_seen = true;
            }
        }
        if (root_ == nullptr) {
            root_ = box;
        }
        // CSS Lists §3.1: `::marker` is the list item's first child, before `::before`.
        // Build `::before` first, then `::marker` with `first=true` so the rotate puts marker
        // at the front (Marker → Before → content → After).
        build_pseudo(node, *box, html::PseudoElement::Before, true);
        build_pseudo(node, *box, html::PseudoElement::Marker, true);
        // With scripting enabled a `<noscript>` holds the page's markup as one raw-text child that
        // a browser does not render. Skipping it here - and the characters the loader moved onto
        // the element itself - leaves the element as an ordinary empty inline box, which is what
        // Chrome reports (measured: `build/noscript_probe.html`, pinned by `example-zdb`'s welcome
        // pane).
        const bool script_raw_text = node->tag == "noscript";
        if (!select_light_dom_hidden(node) && !script_raw_text) {
            for (auto& ch : node->children) {
                build(ch.get(), box);
            }
        }
        wrap_inlines(*box);
        fixup_table(*box);
        mark_table_structure(*box);
        if (box->style_ptr != nullptr && (box->style().table_part == TablePart::Table ||
                                          box->style().table_part == TablePart::InlineTable)) {
            resolve_collapsed_borders(*box);
        }
        zero_collapsed_row(*box);
        // Only-text elements keep their characters on the element. Layout and paint read a text box.
        // A text control with no value shows the same kind of run for its placeholder, carrying the
        // generated element's style so the run keeps its own colour (and its own line box, which is
        // what gives the control a baseline).
        if (!script_raw_text) {
            append_element_text(node, *box);
        }
        build_pseudo(node, *box, html::PseudoElement::After, false);
        note_fixed_subtree(box);
        return;
    }
    if (node->text.empty()) {
        return;
    }
    if (parent != nullptr && parent->style_ptr != nullptr &&
        (parent->style().inner == DisplayInner::Flex ||
         parent->style().inner == DisplayInner::Grid) &&
        blank_text(node->text)) {
        return;
    }
    Box* box = make();
    box->parent = parent;
    box->node = node;
    box->role = BoxRole::Text;
    box->text = node->text;
    intern_into(*box, parent != nullptr ? parent->node : nullptr, BoxRole::Text,
                DisplayOuter::Inline, DisplayInner::Flow);
    box->depends_on_font = true;
    // A text node between element children of a flex or grid container is an anonymous
    // item (CSS Flexbox §4). `append_element_text` already marks an element's own run;
    // this path is the `#text` child, and without the flag its block size stays the font
    // box. The album note "说明" was 16px instead of the 18px line, and the lightbox
    // footer came out 2px short.
    if (parent != nullptr) {
        if (parent->style_ptr != nullptr) {
            if (parent->style().inner == DisplayInner::Flex) {
                box->is_flex_item = true;
            }
            if (parent->style().inner == DisplayInner::Grid) {
                box->is_grid_item = true;
            }
        }
        parent->children.push_back(box);
    }
    by_node_[node] = box;
}

/// A text box that is nothing but collapsible white space. `pre`-family white space is content and
/// keeps its box (CSS Text 3 section 4), so only the two values that fully collapse it answer true.

void BoxTree::wrap_inlines(Box& box) {
    zui::trace::Zone zone("wrap_inlines", zui::trace::Category::LayoutInner);
    // A multi-column container is a block container (CSS Multi-column Layout section 2) and follows
    // the same anonymous-box rules as normal flow. Leaving the check on Flow alone turned the
    // newline between two column items into a line box, so each column grew by one strut.
    if (box.style_ptr == nullptr ||
        (box.style().inner != DisplayInner::Flow && box.style().inner != DisplayInner::Multicol)) {
        return;
    }
    // CSS 2.1 section 9.2.1: a collapsible white-space run that is not adjacent to an inline-level
    // box generates no box at all - it is not an empty line, and it must not stop the neighbours'
    // margins from adjoining. The HTML layer cannot decide this, it only knows tag names and a
    // `<div>` can be `display: inline-block`, so it always emits the space and the decision lands
    // here. Without this, `<div>a</div>\n<div>b</div>` would grow an anonymous block between the
    // two - the case the old tag-name test used to prevent.
    if (box.children.size() > 1) {
        std::vector<Box*> kept;
        kept.reserve(box.children.size());
        for (std::size_t i = 0; i < box.children.size(); ++i) {
            Box* child = box.children[i];
            // CSS Text 3 section 4.1: a collapsible white-space run at the start or end of a line
            // generates nothing at all. A line edge is a neighbour that is not an in-line box: the
            // container's own edge, a block-level sibling, or an out-of-flow box - which takes no
            // part in the line it would have been in (CSS 2.1 9.2.1.1). Space on the far side of
            // one belongs to the box before it, so the scan skips out-of-flow boxes and other
            // white space. Measured: `build/sr_input_probe.html` (a `.sr` absolute file input left
            // a 16px line and grew `scrollHeight`) and `example-zmd`'s `.menu-wrap`, where
            // `<input class="hid"> <label>` came out 52.16px wide instead of the browser's 48.
            const auto neighbour_in_line = [&](std::size_t index, int step) {
                for (int i = static_cast<int>(index) + step;
                     i >= 0 && i < static_cast<int>(box.children.size()); i += step) {
                    const Box* other = box.children[static_cast<std::size_t>(i)];
                    if (other == nullptr || collapsible_blank(*other)) {
                        continue;
                    }
                    if (other->style_ptr == nullptr) {
                        // `inline_level` answers from `role == Text` first; anything else needs a
                        // style, and a box without one (an anonymous wrapper) is not inline.
                        return other->role == BoxRole::Text;
                    }
                    const LayoutStyle& other_style = other->style();
                    if (other_style.position_absolute || other_style.position_fixed) {
                        continue;
                    }
                    return inline_level(*other);
                }
                return false;
            };
            if (child != nullptr && collapsible_blank(*child) &&
                (!neighbour_in_line(i, -1) || !neighbour_in_line(i, 1))) {
                // The box is leaving the tree: forget it the same way the sync path does, or the
                // node keeps naming a box no walk will ever reach (`forget_subtree_boxes` unmaps
                // it and queues it for release at the end of the frame).
                forget_subtree_boxes(*child);
                continue;
            }
            kept.push_back(child);
        }
        box.children.swap(kept);
    }
    if (box.style().table_part != TablePart::None && box.style().table_part != TablePart::Cell &&
        box.style().table_part != TablePart::Caption) {
        // Rows, row groups and the table itself hold table parts, not inline runs; the anonymous
        // boxes they need come from `fixup_table`.
        return;
    }
    bool saw_block = false;
    bool saw_inline = false;
    for (Box* child : box.children) {
        if (child != nullptr && inline_level(*child)) {
            saw_inline = true;
        } else {
            saw_block = true;
        }
    }
    if (!saw_block || !saw_inline) {
        return;
    }
    std::vector<Box*> rebuilt;
    std::vector<Box*> run;
    auto flush = [&]() {
        if (run.empty()) {
            return;
        }
        Box* anon = make();
        anon->parent = &box;
        anon->role = BoxRole::Anonymous;
        intern_into(*anon, box.node, BoxRole::Anonymous, DisplayOuter::Block, DisplayInner::Flow);
        for (Box* child : run) {
            child->parent = anon;
            anon->children.push_back(child);
            if (child->subtree_has_fixed) {
                anon->subtree_has_fixed = true;
            }
        }
        rebuilt.push_back(anon);
        run.clear();
    };
    for (Box* child : box.children) {
        if (child != nullptr && inline_level(*child)) {
            run.push_back(child);
        } else {
            flush();
            rebuilt.push_back(child);
        }
    }
    flush();
    box.children.swap(rebuilt);
}


void BoxTree::rebuild_subtree(Box& box, html::Node* node) {
    zui::trace::Zone zone("rebuild_subtree", zui::trace::Category::LayoutInner);
    box.children.clear();
    // The box was interned for the old display. A block that becomes a grid keeps the new
    // inner, which is what `build` would have written for a freshly lowered element.
    {
        const DisplayOuter outer = used_outer(node, box.parent);
        const DisplayInner inner = inner_of(node);
        intern_into(box, node, box.role, outer, inner);
        const std::string* d = node->style("display");
        const std::string display = d != nullptr ? *d : std::string();
        box.atomic_inline =
            outer == DisplayOuter::Inline &&
            (inner != DisplayInner::Flow || box.is_replaced || display == "inline-block" ||
             display == "inline-flex" || display == "inline-grid" || display == "inline-table" ||
             display == "ruby");
        if (box.parent != nullptr && box.parent->style_ptr != nullptr) {
            box.is_flex_item = box.parent->style().inner == DisplayInner::Flex;
            box.is_grid_item = box.parent->style().inner == DisplayInner::Grid;
        }
    }
    build_pseudo(node, box, html::PseudoElement::Before, true);
    build_pseudo(node, box, html::PseudoElement::Marker, true);
    if (!select_light_dom_hidden(node)) {
        for (auto& ch : node->children) {
            build(ch.get(), &box);
        }
    }
    wrap_inlines(box);
    fixup_table(box);
    mark_table_structure(box);
    if (box.style_ptr != nullptr && (box.style().table_part == TablePart::Table ||
                                     box.style().table_part == TablePart::InlineTable)) {
        resolve_collapsed_borders(box);
    }
    zero_collapsed_row(box);
    append_element_text(node, box);
    build_pseudo(node, box, html::PseudoElement::After, false);
    note_fixed_subtree(&box);
}

}  // namespace layout
}  // namespace zui
