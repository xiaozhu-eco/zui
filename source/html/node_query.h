#ifndef ZUI_HTML_NODE_QUERY_H
#define ZUI_HTML_NODE_QUERY_H

// Tag / type / class queries on html::Node. Included from document.h after the
// types exist, so callers of document.h keep seeing these names.

#include <cctype>
#include <string>
#include <string_view>

namespace zui {
namespace html {

/// CSS Lists / Display: a list item is any element whose computed `display` is `list-item`.
inline bool is_list_item(const Node* n) {
    if (n == nullptr) {
        return false;
    }
    const std::string* d = n->style("display");
    return d != nullptr && *d == "list-item";
}

inline bool is_element(const Node* n) {
    return n != nullptr && !n->tag.empty() && n->tag != "#text";
}

inline bool input_type_is(const Node* n, std::string_view t) {
    if (n == nullptr || n->tag != "input") {
        return false;
    }
    auto it = n->attrs.find("type");
    return it != n->attrs.end() && it->second == t;
}

inline bool is_checkbox(const Node* n) { return input_type_is(n, "checkbox"); }

inline bool is_radio(const Node* n) { return input_type_is(n, "radio"); }

inline bool is_range(const Node* n) { return input_type_is(n, "range"); }

inline bool is_file(const Node* n) { return input_type_is(n, "file"); }

inline bool is_progress(const Node* n) { return n != nullptr && n->tag == "progress"; }

inline bool is_select(const Node* n) { return n != nullptr && n->tag == "select"; }

inline bool is_option(const Node* n) { return n != nullptr && n->tag == "option"; }

inline bool select_is_open(const Node* select) {
    return is_select(select) && select->attrs.find("open") != select->attrs.end();
}

/// Whether this node is a descendant of @p ancestor, including itself.
inline bool node_contains(const Node* ancestor, const Node* node) {
    for (const Node* cursor = node; cursor != nullptr; cursor = cursor->parent) {
        if (cursor == ancestor) {
            return true;
        }
    }
    return false;
}

/// The last open modal `<dialog>` in tree order, or nullptr. `modal` without `open`, and
/// declarative `open` without `modal`, are not traps.
inline Node* top_modal_dialog(Node* node) {
    if (node == nullptr) {
        return nullptr;
    }
    const bool trap = node->tag == "dialog" && node->modal &&
                      node->attrs.find("open") != node->attrs.end();
    Node* found = trap ? node : nullptr;
    for (auto& child : node->children) {
        if (Node* nested = top_modal_dialog(child.get())) {
            found = nested;
        }
    }
    return found;
}

inline bool in_top_layer(const Node* node) {
    if (node == nullptr) {
        return false;
    }
    if (node->modal) {
        return true;
    }
    return is_option(node) && select_is_open(node->parent);
}

/// HTML button layout. A `<button>`, and an `<input>` of type `button` / `submit` / `reset`,
/// uses the fit-content inline size when `width` is `auto`.
inline bool is_button_box(const Node* n) {
    if (n == nullptr) {
        return false;
    }
    if (n->tag == "button") {
        return true;
    }
    return input_type_is(n, "button") || input_type_is(n, "submit") ||
           input_type_is(n, "reset");
}

/// HTML text-entry `<input>` types whose preferred inline size comes from the `size` attribute
/// (default 20). Missing or unknown `type` is text. Checkbox / radio / button / file / … are out.
inline bool is_text_field(const Node* n) {
    if (n == nullptr || n->tag != "input") {
        return false;
    }
    const auto it = n->attrs.find("type");
    if (it == n->attrs.end() || it->second.empty()) {
        return true;
    }
    const std::string& t = it->second;
    return t == "text" || t == "search" || t == "tel" || t == "url" || t == "email" ||
           t == "password";
}

/// Line boxes a text control's inner editor holds when measured: 1 for a text `<input>`,
/// `rows` (default 2) for a `<textarea>`, 0 for a control with no text editor.
inline unsigned text_editor_rows(const Node* n) {
    if (n == nullptr) {
        return 0;
    }
    if (n->tag == "textarea") {
        const auto it = n->attrs.find("rows");
        if (it != n->attrs.end() && !it->second.empty()) {
            unsigned value = 0;
            bool digits = true;
            for (unsigned char c : it->second) {
                if (c < '0' || c > '9') {
                    digits = false;
                    break;
                }
                value = value * 10 + static_cast<unsigned>(c - '0');
                if (value > 512) {
                    digits = false;
                    break;
                }
            }
            if (digits && value > 0) {
                return value;
            }
        }
        return 2;
    }
    if (n->tag != "input") {
        return 0;
    }
    const auto it = n->attrs.find("type");
    if (it == n->attrs.end() || it->second.empty()) {
        return 1;
    }
    const std::string& t = it->second;
    if (t == "text" || t == "search" || t == "tel" || t == "url" || t == "email" ||
        t == "password" || t == "number") {
        return 1;
    }
    return 0;
}

/// `size` attribute for a text field, or 20 when absent / non-positive (HTML default).
inline int text_field_size(const Node* n) {
    if (n == nullptr) {
        return 20;
    }
    const auto it = n->attrs.find("size");
    if (it == n->attrs.end() || it->second.empty()) {
        return 20;
    }
    int value = 0;
    for (unsigned char c : it->second) {
        if (c < '0' || c > '9') {
            return 20;
        }
        value = value * 10 + (c - '0');
        if (value > 10000) {
            return 20;
        }
    }
    return value > 0 ? value : 20;
}

inline bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

inline bool has_class_token(std::string_view classes, std::string_view token) {
    size_t i = 0;
    while (i < classes.size()) {
        while (i < classes.size() && classes[i] == ' ') {
            ++i;
        }
        const size_t j = classes.find(' ', i);
        const size_t n = (j == std::string_view::npos ? classes.size() : j) - i;
        if (n == token.size() && classes.substr(i, n) == token) {
            return true;
        }
        if (j == std::string_view::npos) {
            break;
        }
        i = j + 1;
    }
    return false;
}

/// `<textarea>`, or `<input>` whose type edits a string (text, search, password, email, url, tel,
/// or omitted). File, checkbox, and the other inputs are not text entries.
inline bool is_text_entry(const Node* node) {
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
    if (it == node->attrs.end() || it->second.empty()) {
        return true;
    }
    const std::string& type = it->second;
    return type == "text" || type == "search" || type == "password" || type == "email" ||
           type == "url" || type == "tel" || type == "date" || type == "time";
}

/// Text entry or `contenteditable` other than the literal `false`.
inline bool is_editing_host(const Node* node) {
    if (is_text_entry(node)) {
        return true;
    }
    if (node == nullptr) {
        return false;
    }
    const auto it = node->attrs.find("contenteditable");
    if (it == node->attrs.end()) {
        return false;
    }
    return it->second != "false";
}

}  // namespace html
}  // namespace zui

#endif
