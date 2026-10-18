#include "html/selection.h"
#include "html/document_internal.h"
#include "html/node_query.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace zui {
namespace html {
namespace {

std::size_t text_len(const Node* node) {
    if (node == nullptr) {
        return 0;
    }
    if (node->tag == "br") {
        return 1;
    }
    return node->text.size();
}

void clamp_point(BoundaryPoint& p) {
    if (p.node == nullptr) {
        p.offset = 0;
        return;
    }
    const std::size_t n = text_len(p.node);
    if (p.offset > n) {
        p.offset = n;
    }
}

int index_in_parent(const Node* node) {
    if (node == nullptr || node->parent == nullptr) {
        return 0;
    }
    int i = 0;
    for (const auto& ch : node->parent->children) {
        if (ch.get() == node) {
            return i;
        }
        ++i;
    }
    return 0;
}

bool is_ancestor(const Node* anc, const Node* node) {
    for (const Node* p = node; p != nullptr; p = p->parent) {
        if (p == anc) {
            return true;
        }
    }
    return false;
}

const Node* child_on_path(const Node* anc, const Node* desc) {
    const Node* cur = desc;
    while (cur != nullptr && cur->parent != anc) {
        cur = cur->parent;
    }
    return cur;
}

void collect_ancestors(const Node* node, std::vector<const Node*>& out) {
    out.clear();
    for (const Node* p = node; p != nullptr; p = p->parent) {
        out.push_back(p);
    }
}

}  // namespace

int compare_points(const BoundaryPoint& a, const BoundaryPoint& b) {
    if (a.node == nullptr || b.node == nullptr) {
        if (a.node == b.node) {
            return 0;
        }
        return a.node == nullptr ? -1 : 1;
    }
    if (a.node == b.node) {
        if (a.offset < b.offset) {
            return -1;
        }
        if (a.offset > b.offset) {
            return 1;
        }
        return 0;
    }
    if (is_ancestor(a.node, b.node)) {
        const Node* child = child_on_path(a.node, b.node);
        const int index = child != nullptr ? index_in_parent(child) : 0;
        if (static_cast<std::size_t>(index) >= a.offset) {
            return -1;
        }
        return 1;
    }
    if (is_ancestor(b.node, a.node)) {
        const Node* child = child_on_path(b.node, a.node);
        const int index = child != nullptr ? index_in_parent(child) : 0;
        if (static_cast<std::size_t>(index) >= b.offset) {
            return 1;
        }
        return -1;
    }
    std::vector<const Node*> aa;
    std::vector<const Node*> bb;
    collect_ancestors(a.node, aa);
    collect_ancestors(b.node, bb);
    const Node* lca = nullptr;
    std::size_t i = aa.size();
    std::size_t j = bb.size();
    while (i > 0 && j > 0 && aa[i - 1] == bb[j - 1]) {
        lca = aa[i - 1];
        --i;
        --j;
    }
    (void)lca;
    const Node* ca = i > 0 ? aa[i - 1] : a.node;
    const Node* cb = j > 0 ? bb[j - 1] : b.node;
    const int ia = index_in_parent(ca);
    const int ib = index_in_parent(cb);
    if (ia < ib) {
        return -1;
    }
    if (ia > ib) {
        return 1;
    }
    return 0;
}

std::string range_edit_text(const Node* node) {
    std::string edit;
    const auto walk = [&](auto&& self, const Node* current) -> void {
        if (current == nullptr) {
            return;
        }
        if (current->tag == "#text") {
            edit += current->text;
            return;
        }
        if (current->tag == "br") {
            edit.push_back('\n');
            return;
        }
        if (current->children.empty()) {
            if ((is_text_entry(current) || is_editing_host(current)) && !current->value.empty()) {
                edit += current->value;
            } else {
                edit += current->text;
            }
            return;
        }
        for (const auto& child : current->children) {
            self(self, child.get());
        }
    };
    walk(walk, node);
    return edit;
}

bool flatten_to_point(Node* host, std::size_t offset, BoundaryPoint& out) {
    out = BoundaryPoint{};
    if (host == nullptr) {
        return false;
    }
    if (is_text_entry(host) || (is_editing_host(host) && host->children.empty())) {
        out.node = host;
        out.offset = std::min(offset, text_len(host));
        return true;
    }
    std::size_t at = 0;
    bool found = false;
    const auto walk = [&](auto&& self, Node* current) -> void {
        if (found || current == nullptr) {
            return;
        }
        if (current->tag == "#text") {
            const std::size_t n = current->text.size();
            if (offset <= at + n) {
                out.node = current;
                out.offset = offset - at;
                found = true;
                return;
            }
            at += n;
            return;
        }
        if (current->tag == "br") {
            if (offset <= at) {
                out.node = current;
                out.offset = 0;
                found = true;
                return;
            }
            if (offset == at + 1) {
                out.node = current;
                out.offset = 1;
                found = true;
                return;
            }
            at += 1;
            return;
        }
        if (current->children.empty()) {
            const std::size_t n = current->text.size();
            if (offset <= at + n) {
                out.node = current;
                out.offset = offset - at;
                found = true;
                return;
            }
            at += n;
            return;
        }
        for (auto& child : current->children) {
            self(self, child.get());
            if (found) {
                return;
            }
        }
    };
    walk(walk, host);
    if (!found) {
        out.node = host;
        out.offset = range_edit_text(host).size();
        if (!host->children.empty()) {
            Node* last = host->children.back().get();
            while (last != nullptr && !last->children.empty()) {
                last = last->children.back().get();
            }
            if (last != nullptr) {
                out.node = last;
                out.offset = text_len(last);
            }
        }
    }
    return out.node != nullptr;
}

std::size_t point_to_flatten(const Node* host, const BoundaryPoint& p) {
    if (host == nullptr || p.node == nullptr) {
        return 0;
    }
    if (p.node == host) {
        return std::min(p.offset, text_len(host));
    }
    std::size_t at = 0;
    std::size_t result = 0;
    bool found = false;
    const auto walk = [&](auto&& self, const Node* current) -> void {
        if (found || current == nullptr) {
            return;
        }
        if (current == p.node) {
            result = at + std::min(p.offset, text_len(current));
            found = true;
            return;
        }
        if (current->tag == "#text") {
            at += current->text.size();
            return;
        }
        if (current->tag == "br") {
            at += 1;
            return;
        }
        if (current->children.empty()) {
            at += current->text.size();
            return;
        }
        for (const auto& child : current->children) {
            self(self, child.get());
            if (found) {
                return;
            }
        }
    };
    walk(walk, host);
    return found ? result : at;
}

bool descendant_flatten_span(const Node* host, const Node* descendant, std::size_t& begin,
                             std::size_t& end) {
    begin = 0;
    end = 0;
    if (host == nullptr || descendant == nullptr) {
        return false;
    }
    if (host == descendant) {
        end = range_edit_text(host).size();
        return end > 0 || is_text_entry(host) || is_editing_host(host);
    }
    std::size_t at = 0;
    bool found = false;
    const auto walk = [&](auto&& self, const Node* current) -> void {
        if (found || current == nullptr) {
            return;
        }
        if (current == descendant) {
            begin = at;
            if (current->tag == "br") {
                end = at + 1;
            } else if (current->children.empty() || current->tag == "#text") {
                end = at + current->text.size();
            } else {
                const std::string inner = range_edit_text(current);
                end = at + inner.size();
            }
            found = true;
            return;
        }
        if (current->tag == "#text") {
            at += current->text.size();
            return;
        }
        if (current->tag == "br") {
            at += 1;
            return;
        }
        if (current->children.empty()) {
            at += current->text.size();
            return;
        }
        for (const auto& child : current->children) {
            self(self, child.get());
            if (found) {
                return;
            }
        }
    };
    walk(walk, host);
    return found;
}

void set_selection(Document& doc, BoundaryPoint anchor, BoundaryPoint focus) {
    clamp_point(anchor);
    clamp_point(focus);
    SelectionRange next;
    next.anchor = anchor;
    next.focus = focus;
    if (anchor.node != nullptr && anchor.node == focus.node) {
        next.node = anchor.node;
        next.start = std::min(anchor.offset, focus.offset);
        next.end = std::max(anchor.offset, focus.offset);
    }
    if (doc.selection.anchor.node == next.anchor.node &&
        doc.selection.anchor.offset == next.anchor.offset &&
        doc.selection.focus.node == next.focus.node &&
        doc.selection.focus.offset == next.focus.offset && doc.selection.node == next.node &&
        doc.selection.start == next.start && doc.selection.end == next.end) {
        return;
    }
    const SelectionRange previous = doc.selection;
    doc.selection = next;
    const bool prev_caret_only = previous.node != nullptr && previous.start == previous.end &&
                                 previous.anchor.node == previous.focus.node;
    const bool next_caret_only = next.node != nullptr && next.start == next.end &&
                                 next.anchor.node == next.focus.node;
    if (prev_caret_only && next_caret_only && previous.node == next.node) {
        return;
    }
    note_paint_change(doc);
}

bool selection_slice(const Node* owning, std::size_t run_begin, std::size_t run_end,
                     const SelectionRange& sel, std::size_t& out_a, std::size_t& out_b) {
    out_a = 0;
    out_b = 0;
    if (owning == nullptr || run_begin >= run_end) {
        return false;
    }
    if (sel.node == owning && sel.start < sel.end) {
        out_a = std::max(sel.start, run_begin);
        out_b = std::min(sel.end, run_end);
        return out_a < out_b;
    }
    if (sel.node != nullptr && sel.node != owning && sel.start < sel.end) {
        std::size_t base = 0;
        std::size_t span_end = 0;
        if (descendant_flatten_span(sel.node, owning, base, span_end) && span_end > base) {
            const std::size_t local_start = sel.start > base ? sel.start - base : 0;
            const std::size_t local_end = sel.end > base ? sel.end - base : 0;
            out_a = std::max(local_start, run_begin);
            out_b = std::min(local_end, run_end);
            return out_a < out_b;
        }
    }
    const BoundaryPoint start = selection_start_point(sel);
    const BoundaryPoint end = selection_end_point(sel);
    if (start.node == nullptr || end.node == nullptr ||
        (start.node == end.node && start.offset == end.offset)) {
        return false;
    }
    const std::size_t n = owning->text.size();
    const std::size_t lo = std::min(run_begin, n);
    const std::size_t hi = std::min(run_end, n);
    std::size_t a = hi;
    std::size_t b = lo;
    for (std::size_t i = lo; i < hi; ++i) {
        const BoundaryPoint p{const_cast<Node*>(owning), i};
        if (compare_points(start, p) <= 0 && compare_points(p, end) < 0) {
            if (a > i) {
                a = i;
            }
            b = i + 1;
        }
    }
    if (a >= b) {
        return false;
    }
    out_a = a;
    out_b = b;
    return true;
}

}  // namespace html
}  // namespace zui
