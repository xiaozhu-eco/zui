#include "html/selection.h"
#include "html/node_query.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace zui {
namespace html {
namespace {

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

std::size_t child_flatten_len(const Node* node) {
    if (node == nullptr) {
        return 0;
    }
    if (node->tag == "#text") {
        return node->text.size();
    }
    if (node->tag == "br") {
        return 1;
    }
    if (node->children.empty()) {
        return node->text.size();
    }
    return range_edit_text(node).size();
}

Node* make_text_node(Node* parent, std::string_view text) {
    auto n = std::make_unique<Node>();
    n->tag = "#text";
    n->text.assign(text.begin(), text.end());
    n->parent = parent;
    n->damage = parent != nullptr ? parent->damage : nullptr;
    Node* raw = n.get();
    if (parent != nullptr) {
        parent->children.push_back(std::move(n));
    }
    return raw;
}

Node* split_text(Node* text, std::size_t at) {
    if (text == nullptr || text->tag != "#text" || text->parent == nullptr) {
        return text;
    }
    if (at == 0 || at >= text->text.size()) {
        return text;
    }
    auto right = std::make_unique<Node>();
    right->tag = "#text";
    right->text = text->text.substr(at);
    right->parent = text->parent;
    right->damage = text->damage;
    text->text.resize(at);
    Node* parent = text->parent;
    const int idx = index_in_parent(text);
    parent->children.insert(parent->children.begin() + idx + 1, std::move(right));
    return parent->children[static_cast<std::size_t>(idx) + 1].get();
}

void remove_node(Node* node) {
    if (node == nullptr || node->parent == nullptr) {
        return;
    }
    Node* parent = node->parent;
    auto& kids = parent->children;
    kids.erase(std::remove_if(kids.begin(), kids.end(),
                              [node](const std::unique_ptr<Node>& p) { return p.get() == node; }),
               kids.end());
}

void split_at_flatten(Node* host, std::size_t offset) {
    BoundaryPoint p;
    if (!flatten_to_point(host, offset, p) || p.node == nullptr) {
        return;
    }
    if (p.node->tag == "#text") {
        split_text(p.node, p.offset);
    }
}

void unwrap_sole_child(Node* host) {
    if (host == nullptr || host->children.size() != 1 || host->children[0] == nullptr) {
        return;
    }
    auto inner = std::move(host->children[0]->children);
    host->children.clear();
    for (auto& ch : inner) {
        if (ch) {
            ch->parent = host;
        }
        host->children.push_back(std::move(ch));
    }
}

void wrap_child_range(Node* parent, std::size_t first, std::size_t last, const char* tag) {
    if (parent == nullptr || tag == nullptr || first >= last || last > parent->children.size()) {
        return;
    }
    auto wrap = std::make_unique<Node>();
    wrap->tag = tag;
    wrap->parent = parent;
    wrap->damage = parent->damage;
    Node* w = wrap.get();
    std::vector<std::unique_ptr<Node>> taken;
    taken.reserve(last - first);
    for (std::size_t i = first; i < last; ++i) {
        if (parent->children[i]) {
            parent->children[i]->parent = w;
        }
        taken.push_back(std::move(parent->children[i]));
    }
    parent->children.erase(parent->children.begin() + static_cast<std::ptrdiff_t>(first),
                           parent->children.begin() + static_cast<std::ptrdiff_t>(last));
    for (auto& ch : taken) {
        w->children.push_back(std::move(ch));
    }
    parent->children.insert(parent->children.begin() + static_cast<std::ptrdiff_t>(first),
                            std::move(wrap));
}

void wrap_contained(Node* parent, std::size_t from, std::size_t to, const char* tag,
                    std::size_t parent_begin) {
    if (parent == nullptr || tag == nullptr) {
        return;
    }
    std::size_t at = parent_begin;
    for (std::size_t i = 0; i < parent->children.size(); ++i) {
        Node* ch = parent->children[i].get();
        const std::size_t len = child_flatten_len(ch);
        const std::size_t a = at;
        const std::size_t b = at + len;
        if (a < to && b > from && ch != nullptr && ch->tag != "#text" && ch->tag != "br" &&
            !ch->children.empty() && !(a >= from && b <= to)) {
            wrap_contained(ch, from, to, tag, a);
        }
        at += len;
    }
    at = parent_begin;
    std::vector<std::pair<std::size_t, std::size_t>> runs;
    std::size_t run_start = static_cast<std::size_t>(-1);
    for (std::size_t i = 0; i < parent->children.size(); ++i) {
        Node* ch = parent->children[i].get();
        const std::size_t len = child_flatten_len(ch);
        const std::size_t a = at;
        const std::size_t b = at + len;
        const bool inside = len > 0 && a >= from && b <= to;
        if (inside) {
            if (run_start == static_cast<std::size_t>(-1)) {
                run_start = i;
            }
        } else if (run_start != static_cast<std::size_t>(-1)) {
            runs.push_back({run_start, i});
            run_start = static_cast<std::size_t>(-1);
        }
        at += len;
    }
    if (run_start != static_cast<std::size_t>(-1)) {
        runs.push_back({run_start, parent->children.size()});
    }
    for (std::size_t r = runs.size(); r > 0; --r) {
        wrap_child_range(parent, runs[r - 1].first, runs[r - 1].second, tag);
    }
}

void delete_contained(Node* parent, std::size_t from, std::size_t to, std::size_t parent_begin) {
    if (parent == nullptr) {
        return;
    }
    std::size_t at = parent_begin;
    std::vector<Node*> doomed;
    for (auto& ch : parent->children) {
        Node* node = ch.get();
        const std::size_t len = child_flatten_len(node);
        const std::size_t a = at;
        const std::size_t b = at + len;
        if (len > 0 && a >= from && b <= to) {
            doomed.push_back(node);
        } else if (a < to && b > from && node != nullptr && node->tag != "#text" &&
                   node->tag != "br" && !node->children.empty()) {
            delete_contained(node, from, to, a);
        }
        at += len;
    }
    for (Node* node : doomed) {
        remove_node(node);
    }
}

void prune_empty_inlines(Node* node) {
    if (node == nullptr) {
        return;
    }
    for (auto it = node->children.begin(); it != node->children.end();) {
        Node* ch = it->get();
        prune_empty_inlines(ch);
        if (ch != nullptr && ch->tag != "#text" && ch->tag != "br" && ch->children.empty() &&
            ch->text.empty() && !is_text_entry(ch) && !is_editing_host(ch)) {
            it = node->children.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace

void refresh_edit_value(Node* host) {
    if (host == nullptr) {
        return;
    }
    host->value = range_edit_text(host);
    if (is_text_entry(host) || is_editing_host(host)) {
        host->text = host->value;
    }
}

void insert_text_at(Node* host, std::size_t flatten_at, std::string_view text) {
    if (host == nullptr || text.empty()) {
        return;
    }
    if (is_text_entry(host) || host->children.empty()) {
        if (flatten_at > host->value.size()) {
            flatten_at = host->value.size();
        }
        host->value.insert(flatten_at, text.data(), text.size());
        host->text = host->value;
        note_dom_change(host);
        return;
    }
    BoundaryPoint p;
    if (!flatten_to_point(host, flatten_at, p) || p.node == nullptr) {
        make_text_node(host, text);
        refresh_edit_value(host);
        note_dom_change(host);
        return;
    }
    if (p.node->tag == "#text") {
        if (p.offset > p.node->text.size()) {
            p.offset = p.node->text.size();
        }
        p.node->text.insert(p.offset, text.data(), text.size());
    } else if (p.node->tag == "br") {
        Node* parent = p.node->parent != nullptr ? p.node->parent : host;
        auto n = std::make_unique<Node>();
        n->tag = "#text";
        n->text.assign(text.begin(), text.end());
        n->parent = parent;
        n->damage = parent->damage;
        const int idx = index_in_parent(p.node);
        const int at = p.offset == 0 ? idx : idx + 1;
        parent->children.insert(parent->children.begin() + at, std::move(n));
    } else {
        if (p.offset > p.node->text.size()) {
            p.offset = p.node->text.size();
        }
        p.node->text.insert(p.offset, text.data(), text.size());
    }
    refresh_edit_value(host);
    note_dom_change(host);
}

void delete_flatten_range(Node* host, std::size_t from, std::size_t to) {
    if (host == nullptr || from >= to) {
        return;
    }
    if (is_text_entry(host) || host->children.empty()) {
        if (to > host->value.size()) {
            to = host->value.size();
        }
        if (from > to) {
            from = to;
        }
        host->value.erase(from, to - from);
        host->text = host->value;
        note_dom_change(host);
        return;
    }
    const std::string full = range_edit_text(host);
    if (to > full.size()) {
        to = full.size();
    }
    if (from >= to) {
        return;
    }
    split_at_flatten(host, to);
    split_at_flatten(host, from);
    delete_contained(host, from, to, 0);
    prune_empty_inlines(host);
    if (host->children.empty()) {
        // range_edit_text would otherwise reuse the stale value on an empty editing host.
        host->value.clear();
        host->text.clear();
    } else {
        refresh_edit_value(host);
    }
    note_dom_change(host);
}

void wrap_flatten_range(Node* host, std::size_t from, std::size_t to, const char* tag) {
    if (host == nullptr || tag == nullptr || from >= to) {
        return;
    }
    if (host->children.empty()) {
        const std::string full = range_edit_text(host);
        if (full.empty()) {
            return;
        }
        make_text_node(host, full);
    }
    const std::string full = range_edit_text(host);
    if (to > full.size()) {
        to = full.size();
    }
    if (from >= to) {
        return;
    }
    const bool whole = from == 0 && to == full.size();
    if (whole && host->children.size() == 1 && host->children[0] != nullptr &&
        host->children[0]->tag == tag) {
        unwrap_sole_child(host);
        refresh_edit_value(host);
        note_dom_change(host);
        return;
    }
    split_at_flatten(host, to);
    split_at_flatten(host, from);
    wrap_contained(host, from, to, tag, 0);
    refresh_edit_value(host);
    note_dom_change(host);
}

}  // namespace html
}  // namespace zui
