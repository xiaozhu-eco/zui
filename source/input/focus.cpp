#include "input/detail.h"
#include "input/interaction.h"

#include "layout/point.h"
#include "layout/scrollbar_h.h"
#include "layout/scrollbar_metrics.h"
#include "layout/scrollbar_v.h"
#include "trace/trace.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zui {
namespace input {

using namespace input_detail;

namespace input_detail {
int parse_tabindex(std::string_view raw) {
    if (raw.empty()) {
        return 0;
    }
    return std::atoi(std::string(raw).c_str());
}

int tabindex_of(const html::Node* node) {
    if (node == nullptr) {
        return 0;
    }
    const auto it = node->attrs.find("tabindex");
    if (it == node->attrs.end()) {
        return 0;
    }
    return parse_tabindex(it->second);
}

bool is_sequentially_focusable(const html::Node* node) {
    if (!is_focusable(node)) {
        return false;
    }
    const auto it = node->attrs.find("tabindex");
    if (it != node->attrs.end() && parse_tabindex(it->second) < 0) {
        return false;
    }
    return true;
}

void collect_focusable(html::Node* node, std::vector<html::Node*>& out) {
    if (node == nullptr) {
        return;
    }
    if (!node->tag.empty() && node->tag != "#text" && is_sequentially_focusable(node)) {
        out.push_back(node);
    }
    for (auto& child : node->children) {
        collect_focusable(child.get(), out);
    }
}

/// Browser Tab order: positive tabindex ascending (ties = document order), then
/// tabindex 0 / naturally focusable in document order. tabindex=-1 is skipped.
std::vector<html::Node*> tab_order(html::Node* root) {
    std::vector<html::Node*> doc_order;
    collect_focusable(root, doc_order);
    std::vector<std::pair<int, html::Node*>> positive;
    std::vector<html::Node*> zero;
    positive.reserve(doc_order.size());
    zero.reserve(doc_order.size());
    for (html::Node* n : doc_order) {
        const int tb = tabindex_of(n);
        if (tb > 0) {
            positive.emplace_back(tb, n);
        } else {
            zero.push_back(n);
        }
    }
    std::stable_sort(positive.begin(), positive.end(),
                     [](const std::pair<int, html::Node*>& a,
                        const std::pair<int, html::Node*>& b) { return a.first < b.first; });
    std::vector<html::Node*> out;
    out.reserve(doc_order.size());
    for (const auto& entry : positive) {
        out.push_back(entry.second);
    }
    for (html::Node* n : zero) {
        out.push_back(n);
    }
    return out;
}


bool always_focus_visible(const html::Node* node) { return is_editable(node); }

void set_focus_within_chain(html::Node* node, bool on) {
    for (html::Node* n = node; n != nullptr; n = n->parent) {
        n->focus_within = on;
    }
}

void clear_focus_flags(html::Node* node) {
    if (node == nullptr) {
        return;
    }
    node->focused = false;
    node->focus_visible = false;
    set_focus_within_chain(node, false);
}

void apply_focus(html::Node* node, bool keyboard_modality) {
    if (node == nullptr) {
        return;
    }
    node->focused = true;
    node->focus_visible = keyboard_modality || always_focus_visible(node);
    set_focus_within_chain(node, true);
}

void report_ime_rect(const InteractionHost& host, html::Node* node, layout::CaretAffinity affinity) {
    if (!host.set_ime_rect) {
        return;
    }
    if (node == nullptr || !is_editable(node) || host.layout == nullptr ||
        host.metrics == nullptr) {
        return;
    }
    layout::Box* pb = host.layout->principal_box(node);
    if (pb == nullptr) {
        return;
    }
    const std::size_t caret =
        std::min(static_cast<std::size_t>(std::max(node->caret, 0)), node->value.size());
    const CaretRect bar = caret_rect(*pb, *host.metrics, node->value, caret, affinity);
    host.set_ime_rect(bar.x, bar.y, bar.width, bar.height);
}

}  // namespace input_detail

bool Interaction::focus(html::Node* node, bool keyboard_modality) {
    if (host_.focused == nullptr) {
        return false;
    }
    if (node != nullptr && !is_focusable(node)) {
        return false;
    }
    // An open modal dialog is inert everywhere outside it. Tab already cycles inside;
    // a direct focus request must not leave either.
    if (node != nullptr && host_.document != nullptr && host_.document->root != nullptr) {
        if (html::Node* modal = html::top_modal_dialog(host_.document->root.get())) {
            if (!html::node_contains(modal, node)) {
                return false;
            }
        }
    }
    html::Node*& focused = *host_.focused;
    if (focused == node) {
        return true;
    }
    html::Node* previous = focused;
    if (previous != nullptr) {
        // HTML: change (dirty editable) then blur when focus leaves.
        commit_editable_change(previous);
        clear_focus_flags(previous);
        fire(previous, "blur");
    }
    focused = node;
    text_.edit_dirty_ = false;
    text_.edit_.composing = std::nullopt;
    text_.edit_.format_runs.clear();
    if (focused != nullptr) {
        seed_content_editable(focused);
        text_.edit_.text = focused->value;
        if (is_content_editable(focused)) {
            text_.edit_.load_format_bytes(focused->format);
        }
        apply_focus(focused, keyboard_modality);
        if (focused->caret < 0 ||
            static_cast<std::size_t>(focused->caret) > focused->value.size()) {
            focused->caret = static_cast<int>(focused->value.size());
        }
        fire(focused, "focus");
    }
    if (host_.set_ime) {
        host_.set_ime(focused != nullptr && is_editable(focused));
    }
    report_ime_rect(host_, focused, text_.edit_.affinity);
    if (focused != nullptr && is_editable(focused)) {
        note_caret_visual(focused);
    } else {
        text_.caret_damage_valid_ = false;
    }
    // HTML keeps `selectionStart`/`selectionEnd` - and a contenteditable's document selection -
    // across a focus move, so a toolbar button or a command the palette opens can still read the
    // range. Only a pointer action that lands a caret somewhere else replaces it (pointer.cpp),
    // and `forget_subtree` drops it with the node it names.
    text_.select_node_ = nullptr;
    text_.selecting_text_ = false;
    // The scheduled roots stay the old and the new element. `:focus` / `:focus-visible` are
    // single-node state. `:focus-within` also changes every ancestor; `closure_nodes` adds those
    // ancestors as individual nodes when a loaded sheet uses the pseudo (docs/PERF.md I3). The
    // chains above are cleared and then set so a shared ancestor ends the move still inside.
    std::vector<html::Node*> focus_roots;
    if (previous != nullptr) {
        focus_roots.push_back(previous);
    }
    if (focused != nullptr) {
        focus_roots.push_back(focused);
    }
    note_restyle_roots(focus_roots);
    return true;
}
bool Interaction::blur() {
    if (host_.focused == nullptr || *host_.focused == nullptr) {
        return false;
    }
    return focus(nullptr, false);
}
void Interaction::note_restyle() {
    style_dirty_ = true;
    if (host_.restyle) {
        host_.restyle();
    }
}
void Interaction::note_restyle_roots(const std::vector<html::Node*>& roots) {
    style_dirty_ = true;
    if (host_.restyle_roots) {
        host_.restyle_roots(roots);
        return;
    }
    if (host_.restyle) {
        host_.restyle();
    }
}

}  // namespace input
}  // namespace zui
