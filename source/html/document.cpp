#include "html/document.h"
#include "html/document_internal.h"
#include "html/selection.h"

#include <cstdlib>
#include <lexbor/dom/interface.h>
#include <lexbor/dom/interfaces/attr.h>
#include <lexbor/dom/interfaces/document.h>
#include <lexbor/dom/interfaces/element.h>
#include <lexbor/dom/interfaces/node.h>
#include <lexbor/html/interface.h>
#include <lexbor/html/interfaces/document.h>
#include <lexbor/html/interfaces/template_element.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace zui {
namespace html {

Node* find(Document& doc, std::string_view id) {
    auto it = doc.ids.find(std::string(id));
    if (it == doc.ids.end()) {
        return nullptr;
    }
    return it->second;
}

Node* find_tag(Node* node, std::string_view tag) {
    if (node == nullptr) {
        return nullptr;
    }
    if (node->tag == tag) {
        return node;
    }
    for (auto& ch : node->children) {
        if (Node* inner = find_tag(ch.get(), tag)) {
            return inner;
        }
    }
    return nullptr;
}

void unregister_ids(Document& doc, Node* node) {
    if (node == nullptr) {
        return;
    }
    if (!node->id.empty()) {
        auto it = doc.ids.find(node->id);
        if (it != doc.ids.end() && it->second == node) {
            doc.ids.erase(it);
        }
    }
    for (auto& ch : node->children) {
        unregister_ids(doc, ch.get());
    }
}

void collect_ids(Node* node, std::vector<std::string>& ids) {
    if (node == nullptr) {
        return;
    }
    if (!node->id.empty()) {
        ids.push_back(node->id);
    }
    for (auto& ch : node->children) {
        collect_ids(ch.get(), ids);
    }
}

void adopt_ids(Document& doc, Node* node) {
    if (node == nullptr) {
        return;
    }
    if (!node->id.empty()) {
        doc.ids[node->id] = node;
    }
    for (auto& ch : node->children) {
        adopt_ids(doc, ch.get());
    }
}

bool remove_node(Document& doc, Node* node) {
    if (node == nullptr || node->parent == nullptr) {
        return false;
    }
    Node* parent = node->parent;
    note_dom_change(doc);
    unregister_ids(doc, node);
    auto& kids = parent->children;
    for (auto it = kids.begin(); it != kids.end(); ++it) {
        if (it->get() == node) {
            kids.erase(it);
            return true;
        }
    }
    return false;
}

bool remove_tree(Document& doc, std::string_view id) { return remove_node(doc, find(doc, id)); }

void collect_nodes_with_id(Node* root, std::string_view id, std::vector<Node*>& out) {
    if (root == nullptr || id.empty()) {
        return;
    }
    if (root->id == id) {
        out.push_back(root);
    }
    for (auto& ch : root->children) {
        collect_nodes_with_id(ch.get(), id, out);
    }
}

void collect_descendants_with_ids(Node* node, const std::unordered_set<std::string>& want,
                                  std::vector<Node*>& out) {
    if (node == nullptr) {
        return;
    }
    if (!node->id.empty() && want.count(node->id) != 0) {
        out.push_back(node);
    }
    for (auto& ch : node->children) {
        collect_descendants_with_ids(ch.get(), want, out);
    }
}


namespace {

/// Where @p index sits in @p next after @p old is replaced by it. A caret in the shared prefix
/// stays. A caret in the shared suffix keeps its distance from the end. A caret inside the
/// replaced span follows the edit to the end of that span. An identical string leaves the index.
std::size_t map_text_index(std::string_view old, std::string_view next, std::size_t index) {
    if (index > old.size()) {
        index = old.size();
    }
    std::size_t prefix = 0;
    const std::size_t limit = std::min(old.size(), next.size());
    while (prefix < limit && old[prefix] == next[prefix]) {
        ++prefix;
    }
    std::size_t suffix = 0;
    while (suffix < old.size() - prefix && suffix < next.size() - prefix &&
           old[old.size() - 1 - suffix] == next[next.size() - 1 - suffix]) {
        ++suffix;
    }
    if (index <= prefix) {
        return index;
    }
    if (index >= old.size() - suffix) {
        return next.size() - (old.size() - index);
    }
    return next.size() - suffix;
}

bool parse_size(std::string_view text, std::size_t& out) {
    if (text.empty()) {
        return false;
    }
    std::size_t value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10u + static_cast<std::size_t>(c - '0');
    }
    out = value;
    return true;
}

void place_caret(Document& doc, Node* node, std::size_t caret) {
    if (caret > node->text.size()) {
        caret = node->text.size();
    }
    node->caret = static_cast<int>(caret);
    if (doc.selection.node == node) {
        set_selection(doc, node, caret, caret);
    }
}

/// Replace a text entry's string and keep the caret on the same character.
void assign_entry_text(Document& doc, Node* node, std::string_view value) {
    const std::string old = node->value;
    const std::size_t old_caret =
        static_cast<std::size_t>(std::max(node->caret, 0));
    node->value.assign(value.begin(), value.end());
    node->text = node->value;
    place_caret(doc, node, map_text_index(old, node->value, old_caret));
}

}  // namespace

bool set_field(Document& doc, std::string_view id, std::string_view field, std::string_view value) {
    Node* node = find(doc, id);
    if (node == nullptr) {
        return false;
    }
    if (field == "text") {
        if (is_text_entry(node)) {
            assign_entry_text(doc, node, value);
            return true;
        }
        node->text.assign(value.begin(), value.end());
        if (node->tag == "input" || node->tag == "textarea") {
            node->value = node->text;
            node->caret = static_cast<int>(node->value.size());
        } else {
            // Mixed-content elements keep characters on a `#text` child; layout reads that child,
            // so A-tier set(id,"text") has to update it too (confirm button labels, etc.).
            for (auto& ch : node->children) {
                if (ch != nullptr && ch->tag == "#text") {
                    ch->text = node->text;
                    break;
                }
            }
        }
        return true;
    }
    if (field == "value") {
        if (node->tag == "select") {
            return select_set_value(node, value);
        }
        if (is_text_entry(node)) {
            assign_entry_text(doc, node, value);
            return true;
        }
        node->value.assign(value.begin(), value.end());
        if (node->tag == "input" || node->tag == "textarea") {
            node->text = node->value;
            node->caret = static_cast<int>(node->value.size());
        }
        return true;
    }
    if (field == "insert") {
        if (!is_editing_host(node)) {
            return false;
        }
        refresh_edit_value(node);
        std::size_t caret = static_cast<std::size_t>(std::max(node->caret, 0));
        if (caret > node->value.size()) {
            caret = node->value.size();
        }
        std::size_t from = caret;
        std::size_t to = caret;
        const SelectionRange& sel = doc.selection;
        if (sel.node == node && sel.end <= node->value.size()) {
            from = sel.start;
            to = sel.end;
        } else if (sel.node != node && node->select_anchor != caret) {
            from = std::min(node->select_anchor, caret);
            to = std::max(node->select_anchor, caret);
            if (to > node->value.size()) {
                to = node->value.size();
            }
        }
        if (from < to) {
            delete_flatten_range(node, from, to);
        }
        insert_text_at(node, from, value);
        place_caret(doc, node, from + value.size());
        node->select_anchor = static_cast<std::size_t>(node->caret);
        return true;
    }
    if (field == "select") {
        const std::size_t comma = value.find(',');
        std::size_t start = 0;
        std::size_t end = 0;
        if (comma == std::string_view::npos || !parse_size(value.substr(0, comma), start) ||
            !parse_size(value.substr(comma + 1), end)) {
            return false;
        }
        if (start > end) {
            std::swap(start, end);
        }
        // An editing host stores the range on its own value. A laid-out element that is not an
        // editing host stores the same `start,end` as innerText offsets: BoundaryPoints on the
        // text the layout already shaped. `insert` stays rejected on that host.
        if (is_editing_host(node)) {
            refresh_edit_value(node);
            node->text = node->value;
            if (end > node->text.size()) {
                end = node->text.size();
            }
            if (start > end) {
                start = end;
            }
            node->caret = static_cast<int>(end);
            node->select_anchor = start;
            set_selection(doc, node, start, end);
            return true;
        }
        const std::string edit = range_edit_text(node);
        if (end > edit.size()) {
            end = edit.size();
        }
        if (start > end) {
            start = end;
        }
        BoundaryPoint anchor;
        BoundaryPoint focus;
        flatten_to_point(node, start, anchor);
        flatten_to_point(node, end, focus);
        node->select_anchor = start;
        node->caret = static_cast<int>(end);
        set_selection(doc, anchor, focus);
        return true;
    }
    if (field == "checked") {
        node->checked = !(value == "false" || value == "0" || value.empty());
        // A radio's value attribute is its submission value (a colour name, "on", …).
        // checked is the separate state. Checkbox keeps the frozen "true"/"false" payload.
        if (!is_radio(node)) {
            node->value = node->checked ? "true" : "false";
        }
        return true;
    }
    if (field == "visible") {
        node->visible = !(value == "false" || value == "0");
        // Author CSS keys off the HTML `hidden` attribute (`[hidden] { display: none }`),
        // so flipping the A-tier visible field has to keep that attribute in sync or the
        // element stays display:none forever.
        if (node->visible) {
            node->attrs.erase("hidden");
        } else {
            node->attrs["hidden"] = "";
        }
        return true;
    }
    if (field == "disabled") {
        node->disabled = !(value == "false" || value == "0" || value.empty());
        // Boolean HTML attribute: present means true. Keep attrs in sync so :disabled restyles.
        if (node->disabled) {
            node->attrs["disabled"] = "";
        } else {
            node->attrs.erase("disabled");
        }
        return true;
    }
    if (field == "src") {
        node->attrs["src"].assign(value.begin(), value.end());
        return true;
    }
    if (field == "href") {
        node->attrs["href"].assign(value.begin(), value.end());
        return true;
    }
    // The placeholder is a content attribute of a form control, like `src` / `href`: it is what
    // the control paints while it is empty, and the cascade rebuilds the pseudo run from it.
    if (field == "placeholder") {
        node->attrs["placeholder"].assign(value.begin(), value.end());
        return true;
    }
    if (field == "scroll_top" || field == "scroll_y") {
        char* end = nullptr;
        const float value_px = std::strtof(std::string(value).c_str(), &end);
        if (end == std::string(value).c_str()) {
            return false;
        }
        node->scroll_y = value_px < 0.f ? 0.f : value_px;
        return true;
    }
    if (field == "scroll_left" || field == "scroll_x") {
        char* end = nullptr;
        const float value_px = std::strtof(std::string(value).c_str(), &end);
        if (end == std::string(value).c_str()) {
            return false;
        }
        node->scroll_x = value_px < 0.f ? 0.f : value_px;
        return true;
    }
    const bool data_attr = field.size() > 5 && field.compare(0, 5, "data-") == 0;
    const bool aria_attr = field.size() > 5 && field.compare(0, 5, "aria-") == 0;
    if (data_attr || aria_attr) {
        node->attrs[std::string(field)].assign(value.begin(), value.end());
        return true;
    }
    return false;
}

bool add_class(Document& doc, std::string_view id, std::string_view class_name) {
    Node* node = find(doc, id);
    if (node == nullptr || class_name.empty()) {
        return node != nullptr;
    }
    if (has_class_token(node->class_name, class_name)) {
        return true;
    }
    if (!node->class_name.empty()) {
        node->class_name.push_back(' ');
    }
    node->class_name.append(class_name.data(), class_name.size());
    node->attrs["class"] = node->class_name;
    return true;
}

bool remove_class(Document& doc, std::string_view id, std::string_view class_name) {
    Node* node = find(doc, id);
    if (node == nullptr) {
        return false;
    }
    if (class_name.empty() || node->class_name.empty()) {
        return true;
    }
    std::string next;
    next.reserve(node->class_name.size());
    size_t i = 0;
    while (i < node->class_name.size()) {
        while (i < node->class_name.size() && node->class_name[i] == ' ') {
            ++i;
        }
        const size_t j = node->class_name.find(' ', i);
        const size_t n = (j == std::string::npos ? node->class_name.size() : j) - i;
        if (n > 0 && !(n == class_name.size() &&
                       node->class_name.compare(i, n, class_name.data(), n) == 0)) {
            if (!next.empty()) {
                next.push_back(' ');
            }
            next.append(node->class_name, i, n);
        }
        if (j == std::string::npos) {
            break;
        }
        i = j + 1;
    }
    node->class_name = std::move(next);
    node->attrs["class"] = node->class_name;
    return true;
}


void set_selection(Document& doc, Node* node, std::size_t start, std::size_t end) {
    if (node == nullptr) {
        set_selection(doc, BoundaryPoint{}, BoundaryPoint{});
        return;
    }
    const std::size_t n = node->text.size();
    std::size_t a = start;
    std::size_t b = end;
    if (a > n) {
        a = n;
    }
    if (b > n) {
        b = n;
    }
    set_selection(doc, BoundaryPoint{node, a}, BoundaryPoint{node, b});
}

void clear_selection(Document& doc) { set_selection(doc, nullptr, 0, 0); }

}  // namespace html
}  // namespace zui


