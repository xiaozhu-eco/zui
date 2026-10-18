#include "input/detail.h"
#include "input/interaction.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace zui {
namespace input {

using namespace input_detail;

namespace input_detail {

namespace {

bool attr_present(const html::Node* node, const char* key) {
    if (node == nullptr || key == nullptr) {
        return false;
    }
    const auto it = node->attrs.find(key);
    return it != node->attrs.end();
}

bool is_labelable(const html::Node* node) {
    if (node == nullptr || node->disabled) {
        return false;
    }
    return node->tag == "input" || node->tag == "button" || node->tag == "select" ||
           node->tag == "textarea";
}

html::Node* first_labelable(html::Node* node) {
    if (node == nullptr) {
        return nullptr;
    }
    if (node->tag != "label" && is_labelable(node)) {
        return node;
    }
    for (auto& child : node->children) {
        if (html::Node* found = first_labelable(child.get())) {
            return found;
        }
    }
    return nullptr;
}

bool blank_text(std::string_view text) {
    for (char c : text) {
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            return false;
        }
    }
    return true;
}

std::string invalid_reason(const html::Node* node) {
    if (node == nullptr || node->disabled) {
        return {};
    }
    const bool required = attr_present(node, "required");
    if (html::is_checkbox(node) || html::is_radio(node)) {
        return required && !node->checked ? "required" : std::string{};
    }
    if (node->tag != "input" && node->tag != "textarea" && node->tag != "select") {
        return {};
    }
    const std::string& value = node->value;
    if (required && blank_text(value)) {
        return "required";
    }
    if (node->tag == "input" && !blank_text(value)) {
        if (html::input_type_is(node, "email")) {
            const std::size_t at = value.find('@');
            if (at == std::string::npos || at == 0 || at + 1 >= value.size()) {
                return "type";
            }
        } else if (html::input_type_is(node, "url")) {
            if (value.find(':') == std::string::npos) {
                return "type";
            }
        } else if (html::input_type_is(node, "number")) {
            char* end = nullptr;
            std::strtof(value.c_str(), &end);
            if (end == value.c_str() || (end != nullptr && *end != '\0')) {
                return "type";
            }
        }
    }
    return {};
}

void mark_validity(html::Node* node, const std::string& reason) {
    if (node == nullptr) {
        return;
    }
    if (reason.empty()) {
        node->attrs.erase("aria-invalid");
        return;
    }
    node->attrs["aria-invalid"] = "true";
}

void collect_controls(html::Node* node, std::vector<html::Node*>& out) {
    if (node == nullptr) {
        return;
    }
    if (node->tag == "input" || node->tag == "textarea" || node->tag == "select") {
        out.push_back(node);
    }
    for (auto& child : node->children) {
        collect_controls(child.get(), out);
    }
}

html::Node* first_submit_button(html::Node* node) {
    if (node == nullptr) {
        return nullptr;
    }
    if (is_submit_button(node)) {
        return node;
    }
    for (auto& child : node->children) {
        if (html::Node* found = first_submit_button(child.get())) {
            return found;
        }
    }
    return nullptr;
}

}  // namespace

html::Node* label_control(html::Document& doc, html::Node* node) {
    for (html::Node* cursor = node; cursor != nullptr; cursor = cursor->parent) {
        if (cursor->tag != "label") {
            continue;
        }
        const auto it = cursor->attrs.find("for");
        if (it != cursor->attrs.end() && !it->second.empty()) {
            html::Node* target = html::find(doc, it->second);
            if (is_labelable(target)) {
                return target;
            }
        }
        if (html::Node* inner = first_labelable(cursor)) {
            return inner;
        }
    }
    return nullptr;
}

html::Node* form_ancestor(html::Node* node) {
    for (html::Node* cursor = node; cursor != nullptr; cursor = cursor->parent) {
        if (cursor->tag == "form") {
            return cursor;
        }
    }
    return nullptr;
}

bool is_submit_button(const html::Node* node) {
    if (node == nullptr || node->disabled) {
        return false;
    }
    if (node->tag == "input") {
        return html::input_type_is(node, "submit");
    }
    if (node->tag != "button") {
        return false;
    }
    const auto it = node->attrs.find("type");
    return it == node->attrs.end() || it->second.empty() || it->second == "submit";
}

bool is_reset_button(const html::Node* node) {
    if (node == nullptr || node->disabled) {
        return false;
    }
    if (node->tag == "input") {
        return html::input_type_is(node, "reset");
    }
    if (node->tag != "button") {
        return false;
    }
    const auto it = node->attrs.find("type");
    return it != node->attrs.end() && it->second == "reset";
}

bool is_activation_target(const html::Node* node) {
    if (node == nullptr || node->disabled) {
        return false;
    }
    if (node->tag == "button") {
        return true;
    }
    return html::input_type_is(node, "button") || html::input_type_is(node, "submit") ||
           html::input_type_is(node, "reset");
}

bool submits_on_enter(const html::Node* node) {
    if (node == nullptr || node->disabled || node->tag != "input") {
        return false;
    }
    if (html::is_checkbox(node) || html::is_radio(node) || is_file_input(node) ||
        is_activation_target(node) || html::input_type_is(node, "hidden")) {
        return false;
    }
    return true;
}

}  // namespace input_detail

void Interaction::submit_form(html::Node* form, html::Node* submitter) {
    if (form == nullptr || form->tag != "form" || focus_.submitting_) {
        return;
    }
    focus_.submitting_ = true;
    struct Guard {
        bool& flag;
        ~Guard() { flag = false; }
    } guard{focus_.submitting_};
    std::vector<html::Node*> controls;
    collect_controls(form, controls);
    // HTML constraint validation visits every control. The first invalid field used to return
    // before later type mismatches were marked, so a following email stayed aria-invalid="".
    bool blocked = false;
    for (html::Node* control : controls) {
        const std::string reason = invalid_reason(control);
        mark_validity(control, reason);
        if (!reason.empty()) {
            fire(control, "invalid");
            blocked = true;
        }
    }
    if (blocked) {
        return;
    }
    if (submitter == nullptr) {
        submitter = first_submit_button(form);
        if (submitter != nullptr && fire(submitter, "click")) {
            return;
        }
    }
    fire(form, "submit");
}

void Interaction::reset_form(html::Node* form) {
    if (form == nullptr) {
        return;
    }
    std::vector<html::Node*> controls;
    collect_controls(form, controls);
    for (html::Node* control : controls) {
        if (html::is_checkbox(control) || html::is_radio(control)) {
            control->checked = control->default_selected;
            if (!html::is_radio(control)) {
                control->value = control->checked ? "true" : "false";
            }
            continue;
        }
        const auto it = control->attrs.find("value");
        control->value = it == control->attrs.end() ? std::string{} : it->second;
        control->text = control->value;
        control->caret = static_cast<int>(control->value.size());
        if (host_.layout != nullptr) {
            host_.layout->sync_text(control);
            host_.layout->mark_needs_layout(control);
        }
    }
    if (host_.mark_geometry) {
        host_.mark_geometry();
    }
}

}  // namespace input
}  // namespace zui
