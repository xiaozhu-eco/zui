// The `<select>` control: option labels / values, the committed option and the value protocol.
// `SelectControl` owns the `Node*`; document.h keeps the free-function API as thin delegators.

#include "html/document.h"
#include "html/document_internal.h"
#include "html/document_internal.h"

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

namespace zui {
namespace html {

std::string SelectControl::option_label(const Node* option) {
    if (option == nullptr) {
        return {};
    }
    if (!option->text.empty()) {
        return option->text;
    }
    for (const auto& ch : option->children) {
        if (ch != nullptr && ch->tag == "#text" && !ch->text.empty()) {
            return ch->text;
        }
    }
    return {};
}

std::string SelectControl::option_value(const Node* option) {
    if (option == nullptr) {
        return {};
    }
    const auto it = option->attrs.find("value");
    if (it != option->attrs.end()) {
        return it->second;
    }
    return option_label(option);
}

void SelectControl::commit(Node* option) {
    Node* select = select_;
    if (!is_select(select)) {
        return;
    }
    for (auto& ch : select->children) {
        if (!is_option(ch.get())) {
            continue;
        }
        if (ch.get() == option) {
            ch->attrs["selected"] = "";
            ch->checked = true;
        } else {
            ch->attrs.erase("selected");
            ch->checked = false;
        }
    }
    if (option != nullptr) {
        select->value = option_value(option);
        select->text = option_label(option);
    } else {
        select->value.clear();
        select->text.clear();
    }
}

void SelectControl::init() {
    Node* select = select_;
    if (!is_select(select)) {
        return;
    }
    Node* chosen = nullptr;
    Node* first = nullptr;
    for (auto& ch : select->children) {
        if (!is_option(ch.get())) {
            continue;
        }
        if (first == nullptr) {
            first = ch.get();
        }
        if (ch->attrs.find("selected") != ch->attrs.end()) {
            chosen = ch.get();
            break;
        }
    }
    select_commit(select, chosen != nullptr ? chosen : first);
}

bool SelectControl::set_value(std::string_view value) {
    Node* select = select_;
    if (!is_select(select)) {
        return false;
    }
    Node* match = nullptr;
    Node* label_match = nullptr;
    for (auto& ch : select->children) {
        if (!is_option(ch.get())) {
            continue;
        }
        if (option_value(ch.get()) == value) {
            match = ch.get();
            break;
        }
        if (label_match == nullptr && option_label(ch.get()) == value) {
            label_match = ch.get();
        }
    }
    if (match == nullptr) {
        match = label_match;
    }
    if (match == nullptr) {
        return false;
    }
    select_commit(select, match);
    return true;
}

std::string option_label(const Node* option) { return SelectControl::option_label(option); }

std::string option_value(const Node* option) { return SelectControl::option_value(option); }

void select_commit(Node* select, Node* option) { SelectControl(select).commit(option); }

void select_init(Node* select) { SelectControl(select).init(); }

bool select_set_value(Node* select, std::string_view value) {
    return SelectControl(select).set_value(value);
}

}  // namespace html
}  // namespace zui
