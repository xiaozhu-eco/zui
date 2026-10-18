// HTML/Selectors pseudo-classes, :lang(), and :dir().

#include "ascii.h"
#include "css/engine/cascade_internal.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace zui {
namespace css {
namespace detail {

using html::iequals;
using html::has_class_token;

std::string ascii_lower(std::string_view text) { return ::zui::ascii_lower(text); }

const std::string* attr_of(const html::Node* node, const char* name) {
    if (node == nullptr) {
        return nullptr;
    }
    const auto it = node->attrs.find(name);
    return it == node->attrs.end() ? nullptr : &it->second;
}

bool input_type_is_one_of(const html::Node* node, std::initializer_list<const char*> types) {
    const std::string* type = attr_of(node, "type");
    if (type == nullptr || type->empty()) {
        return false;
    }
    for (const char* name : types) {
        if (iequals(*type, name)) {
            return true;
        }
    }
    return false;
}

/// HTML: `required` does not apply to hidden, range, color, or button-like inputs. A missing
/// type is `text` and does apply. `select` and `textarea` apply. Nothing else does.
bool required_applies(const html::Node* node) {
    if (node->tag == "select" || node->tag == "textarea") {
        return true;
    }
    if (node->tag != "input") {
        return false;
    }
    const std::string* type = attr_of(node, "type");
    if (type == nullptr || type->empty()) {
        return true;
    }
    return !input_type_is_one_of(node, {"hidden", "range", "color", "button", "submit", "reset",
                                        "image"});
}

/// HTML: `readonly` applies to text-like inputs and `textarea`, not to checkbox, radio, file,
/// hidden, range, color, or the button types. A missing input type is `text`.
bool readonly_applies(const html::Node* node) {
    if (node->tag == "textarea") {
        return true;
    }
    if (node->tag != "input") {
        return false;
    }
    const std::string* type = attr_of(node, "type");
    if (type == nullptr || type->empty()) {
        return true;
    }
    return input_type_is_one_of(node, {"text", "search", "url", "tel", "email", "password", "date",
                                       "month", "week", "time", "datetime-local", "number"});
}

enum class EditDecl { Inherit, On, Off };

EditDecl content_editable_decl(const html::Node* node) {
    const std::string* raw = attr_of(node, "contenteditable");
    if (raw == nullptr) {
        return EditDecl::Inherit;
    }
    if (raw->empty() || iequals(*raw, "true") || iequals(*raw, "plaintext-only")) {
        return EditDecl::On;
    }
    if (iequals(*raw, "false")) {
        return EditDecl::Off;
    }
    // The invalid-value default is inherit. The missing-value default (empty) is true.
    return EditDecl::Inherit;
}

/// An editing host or a descendant that has not opted out. Inputs are not asked this: HTML uses
/// the input's own mutability even when it sits inside an editing host.
bool editing_host_editable(const html::Node* node) {
    for (const html::Node* n = node; n != nullptr; n = n->parent) {
        const EditDecl decl = content_editable_decl(n);
        if (decl == EditDecl::On) {
            return true;
        }
        if (decl == EditDecl::Off) {
            return false;
        }
    }
    return false;
}

bool is_read_write(const html::Node* node) {
    if (node->tag == "input" || node->tag == "textarea") {
        if (!readonly_applies(node) || node->disabled || attr_of(node, "readonly") != nullptr) {
            return false;
        }
        return true;
    }
    return editing_host_editable(node);
}

bool placeholder_shown(const html::Node* node) {
    if (node->tag != "input" && node->tag != "textarea") {
        return false;
    }
    if (attr_of(node, "placeholder") == nullptr) {
        return false;
    }
    return node->value.empty();
}

/// `form="id"` wins over an ancestor form. A missing id, or a null document when the attribute
/// is set, means the control has no form owner.
html::Node* form_owner(const html::Node* node, const html::Document* doc) {
    const std::string* form_id = attr_of(node, "form");
    if (form_id != nullptr) {
        if (doc == nullptr) {
            return nullptr;
        }
        const auto found = doc->ids.find(*form_id);
        if (found == doc->ids.end() || found->second == nullptr || found->second->tag != "form") {
            return nullptr;
        }
        return found->second;
    }
    for (html::Node* p = node->parent; p != nullptr; p = p->parent) {
        if (p->tag == "form") {
            return p;
        }
    }
    return nullptr;
}

bool is_submit_button(const html::Node* node) {
    if (node->tag == "button") {
        const std::string* type = attr_of(node, "type");
        return type == nullptr || type->empty() || iequals(*type, "submit");
    }
    if (node->tag != "input") {
        return false;
    }
    return input_type_is_one_of(node, {"submit", "image"});
}

html::Node* document_root(const html::Node* node, const html::Document* doc) {
    if (doc != nullptr && doc->root != nullptr) {
        return doc->root.get();
    }
    const html::Node* top = node;
    while (top != nullptr && top->parent != nullptr) {
        top = top->parent;
    }
    return const_cast<html::Node*>(top);
}

bool is_default_match(const html::Node* node, const html::Document* doc) {
    if (html::is_checkbox(node) || html::is_radio(node) || node->tag == "option") {
        return node->default_selected;
    }
    if (!is_submit_button(node)) {
        return false;
    }
    html::Node* owner = form_owner(node, doc);
    html::Node* root = document_root(node, doc);
    if (owner == nullptr || root == nullptr) {
        return false;
    }
    std::vector<html::Node*> stack{root};
    while (!stack.empty()) {
        html::Node* cur = stack.back();
        stack.pop_back();
        if (is_submit_button(cur) && form_owner(cur, doc) == owner) {
            return cur == node;
        }
        for (auto it = cur->children.rbegin(); it != cur->children.rend(); ++it) {
            if (*it != nullptr) {
                stack.push_back(it->get());
            }
        }
    }
    return false;
}

bool radio_group_unchecked(const html::Node* node, const html::Document* doc) {
    if (!html::is_radio(node)) {
        return false;
    }
    // HTML's radio button group requires a name that is not the empty string. A missing or
    // empty name is not shared, so only this radio's checkedness counts.
    const std::string* name_attr = attr_of(node, "name");
    if (name_attr == nullptr || name_attr->empty()) {
        return !node->checked;
    }
    html::Node* root = document_root(node, doc);
    if (root == nullptr) {
        return !node->checked;
    }
    const std::string& name = *name_attr;
    html::Node* owner = form_owner(node, doc);
    bool any = false;
    std::vector<html::Node*> stack{root};
    while (!stack.empty()) {
        html::Node* cur = stack.back();
        stack.pop_back();
        if (html::is_radio(cur)) {
            const std::string* other_attr = attr_of(cur, "name");
            const std::string other = other_attr != nullptr ? *other_attr : std::string{};
            if (other == name && form_owner(cur, doc) == owner) {
                any = true;
                if (cur->checked) {
                    return false;
                }
            }
        }
        for (const auto& child : cur->children) {
            if (child != nullptr) {
                stack.push_back(child.get());
            }
        }
    }
    return any;
}

bool is_indeterminate(const html::Node* node, const html::Document* doc) {
    if (node->tag == "progress") {
        return attr_of(node, "value") == nullptr;
    }
    return radio_group_unchecked(node, doc);
}

bool is_link_match(const html::Node* node) {
    if (node->tag != "a" && node->tag != "area") {
        return false;
    }
    return attr_of(node, "href") != nullptr;
}

/// UAX #9 strong class for HTML directionality. Layout's `bidi_class_of` folds U+200E / U+200F
/// into BN; LRM and RLM are strong (L and R), and U+061C ALM is AL. Other ranges match that table.
enum class StrongDir : std::uint8_t { None, L, R, AL };

StrongDir strong_dir_of(std::uint32_t cp) {
    if (cp == 0x200E) {
        return StrongDir::L;
    }
    if (cp == 0x200F) {
        return StrongDir::R;
    }
    if (cp == 0x061C) {
        return StrongDir::AL;
    }
    if ((cp >= 0x05D0 && cp <= 0x05EA) || (cp >= 0x05F0 && cp <= 0x05F4) ||
        (cp >= 0xFB1D && cp <= 0xFB28) || (cp >= 0xFB2A && cp <= 0xFB4F)) {
        return StrongDir::R;
    }
    if (cp == 0x0608 || (cp >= 0x0620 && cp <= 0x064A) || (cp >= 0x066E && cp <= 0x066F) ||
        (cp >= 0x0671 && cp <= 0x06D3) || cp == 0x06D5 || (cp >= 0x06EE && cp <= 0x06EF) ||
        (cp >= 0x06FA && cp <= 0x06FC) || cp == 0x06FF || (cp >= 0x0750 && cp <= 0x077F) ||
        (cp >= 0x08A0 && cp <= 0x08BD) || (cp >= 0xFB50 && cp <= 0xFD3D) ||
        (cp >= 0xFD50 && cp <= 0xFD8F) || (cp >= 0xFD92 && cp <= 0xFDC7) ||
        (cp >= 0xFDF0 && cp <= 0xFDFB) || (cp >= 0xFE70 && cp <= 0xFEFC)) {
        return StrongDir::AL;
    }
    if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || (cp >= 0x00C0 && cp <= 0x02AF) ||
        (cp >= 0x0400 && cp <= 0x04FF) || (cp >= 0x1E00 && cp <= 0x1EFF) ||
        (cp >= 0x3040 && cp <= 0x30FF) || (cp >= 0x3400 && cp <= 0x9FFF) ||
        (cp >= 0xAC00 && cp <= 0xD7AF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
        (cp >= 0xFF21 && cp <= 0xFF3A) || (cp >= 0xFF41 && cp <= 0xFF5A) ||
        (cp >= 0x20000 && cp <= 0x2A6DF)) {
        return StrongDir::L;
    }
    return StrongDir::None;
}

bool next_code_point(std::string_view text, std::size_t& i, std::uint32_t& cp) {
    if (i >= text.size()) {
        return false;
    }
    const auto b0 = static_cast<unsigned char>(text[i]);
    if (b0 < 0x80) {
        cp = b0;
        ++i;
        return true;
    }
    if ((b0 & 0xE0) == 0xC0 && i + 1 < text.size()) {
        cp = (static_cast<std::uint32_t>(b0 & 0x1F) << 6) |
             static_cast<unsigned char>(text[i + 1] & 0x3F);
        i += 2;
        return true;
    }
    if ((b0 & 0xF0) == 0xE0 && i + 2 < text.size()) {
        cp = (static_cast<std::uint32_t>(b0 & 0x0F) << 12) |
             (static_cast<std::uint32_t>(static_cast<unsigned char>(text[i + 1] & 0x3F)) << 6) |
             static_cast<unsigned char>(text[i + 2] & 0x3F);
        i += 3;
        return true;
    }
    if ((b0 & 0xF8) == 0xF0 && i + 3 < text.size()) {
        cp = (static_cast<std::uint32_t>(b0 & 0x07) << 18) |
             (static_cast<std::uint32_t>(static_cast<unsigned char>(text[i + 1] & 0x3F)) << 12) |
             (static_cast<std::uint32_t>(static_cast<unsigned char>(text[i + 2] & 0x3F)) << 6) |
             static_cast<unsigned char>(text[i + 3] & 0x3F);
        i += 4;
        return true;
    }
    cp = b0;
    ++i;
    return true;
}

/// HTML `dir` attribute state. Missing and invalid values are both Undefined.
enum class DirState : std::uint8_t { Ltr, Rtl, Auto, Undefined };

DirState dir_state_of(const html::Node& node) {
    const std::string* dir = attr_of(&node, "dir");
    if (dir == nullptr) {
        return DirState::Undefined;
    }
    if (iequals(*dir, "ltr")) {
        return DirState::Ltr;
    }
    if (iequals(*dir, "rtl")) {
        return DirState::Rtl;
    }
    if (iequals(*dir, "auto")) {
        return DirState::Auto;
    }
    return DirState::Undefined;
}

/// -1 null, 0 ltr, 1 rtl. First strong L wins; else first AL/R; else null.
int text_node_direction(std::string_view data) {
    std::size_t i = 0;
    std::uint32_t cp = 0;
    while (next_code_point(data, i, cp)) {
        const StrongDir strong = strong_dir_of(cp);
        if (strong == StrongDir::L) {
            return 0;
        }
        if (strong == StrongDir::R || strong == StrongDir::AL) {
            return 1;
        }
    }
    return -1;
}

/// Form value: AL/R with no L before it is rtl. Any other non-empty value is ltr. Empty is null.
int value_auto_direction(std::string_view value) {
    const int strong = text_node_direction(value);
    if (strong >= 0) {
        return strong;
    }
    return value.empty() ? -1 : 0;
}

bool is_tel_input(const html::Node& node) {
    if (node.tag != "input") {
        return false;
    }
    const std::string* type = attr_of(&node, "type");
    return type != nullptr && iequals(*type, "tel");
}

/// Input states whose `dir=auto` does not read the value. Missing and invalid types are Text.
bool input_skips_value_for_auto(std::string_view type) {
    return iequals(type, "checkbox") || iequals(type, "radio") || iequals(type, "file") ||
           iequals(type, "image") || iequals(type, "color") || iequals(type, "date") ||
           iequals(type, "month") || iequals(type, "week") || iequals(type, "time") ||
           iequals(type, "datetime-local") || iequals(type, "number") || iequals(type, "range");
}

bool auto_dir_form(const html::Node& node) {
    if (node.tag == "textarea") {
        return true;
    }
    if (node.tag != "input") {
        return false;
    }
    const std::string* type = attr_of(&node, "type");
    return type == nullptr || !input_skips_value_for_auto(*type);
}

/// Descendants of these elements do not contribute to an ancestor's auto direction.
bool excludes_auto_text(const html::Node& node) {
    if (node.tag == "bdi" || node.tag == "script" || node.tag == "style" ||
        node.tag == "textarea") {
        return true;
    }
    return dir_state_of(node) != DirState::Undefined;
}

int contained_from_children(const html::Node& node, bool skip) {
    if (node.children.empty()) {
        // `<input>` is void. Its value is not a text node; the paint mirror in `Node::text`
        // must not leak into a parent's auto direction.
        if (skip || node.tag == "input") {
            return -1;
        }
        return text_node_direction(node.text);
    }
    for (const auto& child : node.children) {
        if (child == nullptr) {
            continue;
        }
        if (child->tag == "#text") {
            if (skip) {
                continue;
            }
            const int found = text_node_direction(child->text);
            if (found >= 0) {
                return found;
            }
            continue;
        }
        if (!html::is_element(child.get())) {
            continue;
        }
        const int found = contained_from_children(*child, skip || excludes_auto_text(*child));
        if (found >= 0) {
            return found;
        }
    }
    return -1;
}

int auto_direction(const html::Node& node) {
    if (auto_dir_form(node)) {
        return value_auto_direction(node.value);
    }
    // No shadow tree: the slot / assigned-nodes step does not apply.
    return contained_from_children(node, false);
}

bool element_is_rtl(const html::Node& node) {
    switch (dir_state_of(node)) {
        case DirState::Ltr:
            return false;
        case DirState::Rtl:
            return true;
        case DirState::Auto:
            return auto_direction(node) == 1;
        case DirState::Undefined:
            if (node.tag == "bdi") {
                return auto_direction(node) == 1;
            }
            if (is_tel_input(node)) {
                return false;
            }
            if (node.parent != nullptr && html::is_element(node.parent)) {
                return element_is_rtl(*node.parent);
            }
            return false;
    }
    return false;
}

bool direction_from_contents(const html::Node& node) {
    const DirState state = dir_state_of(node);
    return state == DirState::Auto || (state == DirState::Undefined && node.tag == "bdi");
}

std::string resolved_language(const html::Node& node) {
    for (const html::Node* n = &node; n != nullptr; n = n->parent) {
        if (const std::string* lang = attr_of(n, "lang")) {
            return ascii_lower(*lang);
        }
        if (const std::string* xml = attr_of(n, "xml:lang")) {
            return ascii_lower(*xml);
        }
    }
    return {};
}

/// `:lang(zh, en)` — each range is a BCP 47 prefix on a hyphen boundary. `*` ranges are a
/// non-goal and do not match.
bool language_matches_list(const html::Node& node, std::string_view list) {
    const std::string language = resolved_language(node);
    if (language.empty() || list.empty()) {
        return false;
    }
    std::size_t i = 0;
    while (i < list.size()) {
        while (i < list.size() && (list[i] == ' ' || list[i] == '\t')) {
            ++i;
        }
        const std::size_t start = i;
        while (i < list.size() && list[i] != ',') {
            ++i;
        }
        std::size_t end = i;
        while (end > start && (list[end - 1] == ' ' || list[end - 1] == '\t')) {
            --end;
        }
        if (end > start) {
            const std::string range = ascii_lower(list.substr(start, end - start));
            if (range.find('*') == std::string::npos && !range.empty()) {
                if (language.size() >= range.size() &&
                    language.compare(0, range.size(), range) == 0 &&
                    (language.size() == range.size() || language[range.size()] == '-')) {
                    return true;
                }
            }
        }
        if (i < list.size() && list[i] == ',') {
            ++i;
        }
    }
    return false;
}

uint32_t html_pseudo_bits(const html::Node& node, const html::Document* doc, uint32_t mask) {
    uint32_t bits = 0;
    if ((mask & kBitPlaceholderShown) != 0 && placeholder_shown(&node)) {
        bits |= kBitPlaceholderShown;
    }
    if ((mask & kBitFocusWithin) != 0 && (node.focused || node.focus_within)) {
        bits |= kBitFocusWithin;
    }
    if ((mask & (kBitRequired | kBitOptional)) != 0 && required_applies(&node)) {
        bits |= attr_of(&node, "required") != nullptr ? kBitRequired : kBitOptional;
    }
    // `:read-write` walks ancestors for `contenteditable`. Sheets that never test it used to pay
    // that walk once per element, twice when a sibling combinator also fingerprints state.
    if ((mask & kBitReadWrite) != 0 && is_read_write(&node)) {
        bits |= kBitReadWrite;
    }
    if ((mask & kBitDefault) != 0 && is_default_match(&node, doc)) {
        bits |= kBitDefault;
    }
    if ((mask & kBitIndeterminate) != 0 && is_indeterminate(&node, doc)) {
        bits |= kBitIndeterminate;
    }
    if ((mask & kBitLink) != 0 && is_link_match(&node)) {
        bits |= kBitLink;
    }
    return bits;
}

/// The pseudo-classes that only read the node (and its siblings, form, or radio group). `:hover`
/// and the structural position pseudos are here so `hover_may_change_style` can evaluate the rest
/// of a hover compound with the same code the cascade uses instead of treating every such compound
/// as a possible match for every element.
bool match_plain_pseudo_class(const html::Node* node, uint32_t type, const html::Node* root,
                              const html::Document* doc) {
    switch (type) {
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_HOVER:
            return node->hovered;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_ACTIVE:
            return node->active;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_FOCUS:
            return node->focused;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_FOCUS_VISIBLE:
            // Selectors 4: a subset of `:focus`. The interaction layer decides when the
            // UA would draw a focus indicator (keyboard focus, or a text-entry control).
            return node->focused && node->focus_visible;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_FOCUS_WITHIN:
            return node->focused || node->focus_within;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_MODAL:
            // HTML / CSS UI: top-layer modal (dialog showModal). Node::modal is set for
            // declarative `<dialog open>` until a show()/showModal() host API lands.
            return node->modal;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_CHECKED:
            return node->checked;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_DISABLED:
            return node->disabled;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_ENABLED:
            return !node->disabled;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_EMPTY:
            // Selectors 3 / Chrome: whitespace-only text children make an element non-empty.
            // convert_children drops those nodes but sets Node::whitespace_child.
            return node->children.empty() && node->text.empty() && !node->whitespace_child;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_ROOT:
            // The document element is the only element without a parent (`doc.root`).
            return root != nullptr ? node == root : node->parent == nullptr;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_FIRST_CHILD:
            return is_first_element(node);
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_LAST_CHILD:
            return is_last_element(node);
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_FIRST_OF_TYPE:
            return is_first_of_type(node);
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_LAST_OF_TYPE:
            return is_last_of_type(node);
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_ONLY_CHILD:
            return is_first_element(node) && is_last_element(node);
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_ONLY_OF_TYPE:
            return is_first_of_type(node) && is_last_of_type(node);
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_PLACEHOLDER_SHOWN:
            return placeholder_shown(node);
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_REQUIRED:
            return required_applies(node) && attr_of(node, "required") != nullptr;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_OPTIONAL:
            return required_applies(node) && attr_of(node, "required") == nullptr;
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_READ_WRITE:
            return is_read_write(node);
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_READ_ONLY:
            return !is_read_write(node);
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_DEFAULT:
            return is_default_match(node, doc);
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_INDETERMINATE:
            return is_indeterminate(node, doc);
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_LINK:
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_ANY_LINK:
            return is_link_match(node);
        case LXB_CSS_SELECTOR_PSEUDO_CLASS_VISITED:
            // No session history. The selector still parses, so `a:link, a:visited` keeps `a:link`.
            return false;
        default:
            return false;
    }
}


}  // namespace detail
}  // namespace css
}  // namespace zui
