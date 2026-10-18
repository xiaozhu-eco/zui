#include "ascii.h"
#include "input/detail.h"
#include "input/interaction.h"

#include "layout/paint_transform.h"
#include "layout/point.h"
#include "layout/scrollbar_h.h"
#include "layout/scrollbar_metrics.h"
#include "layout/scrollbar_v.h"
#include "layout/viewport.h"
#include "trace/trace.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zui {
namespace input {

using namespace input_detail;

namespace {

bool starts_with_ci(std::string_view text, std::string_view lower) {
    return zui::starts_with_ci(text, lower);
}

/// HTML hyperlink default: the nearest `<a href>` opens unless the href is empty,
/// a fragment, or a script URL. The platform whitelist still rejects other schemes.
void follow_hyperlink(InteractionHost& host, html::Node* target) {
    if (!host.open_uri) {
        return;
    }
    for (html::Node* node = target; node != nullptr; node = node->parent) {
        if (node->tag != "a") {
            continue;
        }
        const auto href_it = node->attrs.find("href");
        if (href_it == node->attrs.end()) {
            continue;
        }
        std::string_view href = href_it->second;
        while (!href.empty() && (href.front() == ' ' || href.front() == '\t')) {
            href.remove_prefix(1);
        }
        if (href.empty() || href.front() == '#' || starts_with_ci(href, "javascript:") ||
            starts_with_ci(href, "vbscript:")) {
            return;
        }
        host.open_uri(href);
        return;
    }
}

bool owns_click_activation(const html::Node* node) {
    if (node == nullptr) {
        return false;
    }
    const std::string& tag = node->tag;
    return tag == "a" || tag == "button" || tag == "input" || tag == "select" ||
           tag == "textarea" || tag == "label";
}

/// The `<details>` a summary click should toggle, or null. Only the first `<summary>` child is
/// the disclosure control. An interactive descendant (link, button, field) keeps its own action.
html::Node* details_of_summary_click(html::Node* target) {
    for (html::Node* node = target; node != nullptr; node = node->parent) {
        if (owns_click_activation(node)) {
            return nullptr;
        }
        if (node->tag != "summary") {
            continue;
        }
        html::Node* parent = node->parent;
        if (parent == nullptr || parent->tag != "details") {
            return nullptr;
        }
        for (const auto& child : parent->children) {
            if (child != nullptr && child->tag == "summary") {
                return child.get() == node ? parent : nullptr;
            }
        }
        return nullptr;
    }
    return nullptr;
}

void toggle_details_open(html::Node* details) {
    const auto it = details->attrs.find("open");
    if (it == details->attrs.end()) {
        details->attrs.emplace("open", "");
    } else {
        details->attrs.erase(it);
    }
}

/// First `autofocus` descendant that can take Tab, else the first in tree order.
html::Node* dialog_focus_target(html::Node* dialog) {
    html::Node* autofocus = nullptr;
    html::Node* first = nullptr;
    const auto walk = [&](auto&& self, html::Node* node) -> void {
        if (node == nullptr) {
            return;
        }
        for (auto& child : node->children) {
            html::Node* current = child.get();
            if (current == nullptr) {
                continue;
            }
            if (is_sequentially_focusable(current)) {
                if (first == nullptr) {
                    first = current;
                }
                if (autofocus == nullptr &&
                    current->attrs.find("autofocus") != current->attrs.end()) {
                    autofocus = current;
                }
            }
            self(self, current);
        }
    };
    walk(walk, dialog);
    return autofocus != nullptr ? autofocus : first;
}

}  // namespace

void Interaction::apply_dialog_command(html::Node* target) {
    if (host_.document == nullptr) {
        return;
    }
    html::Node* button = nullptr;
    for (html::Node* node = target; node != nullptr; node = node->parent) {
        const bool buttonish =
            node->tag == "button" ||
            (node->tag == "input" &&
             (html::input_type_is(node, "button") || html::input_type_is(node, "submit") ||
              html::input_type_is(node, "reset")));
        if (!buttonish) {
            continue;
        }
        if (node->disabled || node->attrs.find("command") == node->attrs.end()) {
            return;
        }
        button = node;
        break;
    }
    if (button == nullptr) {
        return;
    }
    const auto verb = button->attrs.find("command");
    const bool show = html::iequals(verb->second, "show-modal");
    const bool close = html::iequals(verb->second, "close");
    if (!show && !close) {
        return;
    }
    const auto ref = button->attrs.find("commandfor");
    if (ref == button->attrs.end() || ref->second.empty()) {
        return;
    }
    html::Node* dialog = html::find(*host_.document, ref->second);
    if (dialog == nullptr || dialog->tag != "dialog") {
        return;
    }
    const bool open = dialog->attrs.find("open") != dialog->attrs.end();
    if (show) {
        // HTML showModal() throws when the dialog is already open. Leave it alone.
        if (open) {
            return;
        }
        focus_.modal_return_id_.clear();
        if (html::Node* current = focused()) {
            focus_.modal_return_id_ = current->id;
        }
        dialog->attrs["open"] = "";
        dialog->modal = true;
        note_restyle_roots({dialog});
        if (html::Node* pick = dialog_focus_target(dialog)) {
            focus(pick, /*keyboard_modality=*/true);
        }
        return;
    }
    if (!open && !dialog->modal) {
        return;
    }
    dialog->attrs.erase("open");
    dialog->modal = false;
    note_restyle_roots({dialog});
    html::Node* back =
        focus_.modal_return_id_.empty() ? nullptr : html::find(*host_.document, focus_.modal_return_id_);
    focus_.modal_return_id_.clear();
    if (back != nullptr && is_focusable(back)) {
        focus(back, /*keyboard_modality=*/true);
    } else if (focused() != nullptr && html::node_contains(dialog, focused())) {
        blur();
    }
}

void Interaction::finish_click(float x, float y, const Pointer& mods) {
    if (host_.layout == nullptr || host_.document == nullptr) {
        return;
    }
    const auto it = pointer_.press_state_.find(mods.pointer_id);
    if (it == pointer_.press_state_.end()) {
        // A release with no press (a stray OS up): nothing to activate.
        return;
    }
    const PressState state = it->second;
    pointer_.press_state_.erase(it);
    set_active(state.active_node, false);
    if (drag_.html_drag_suppress_click_) {
        drag_.html_drag_suppress_click_ = false;
        return;
    }
    if (state.target == nullptr || state.panning) {
        return;
    }
    // UI Events: click goes to the nearest common inclusive ancestor of the press and release
    // targets, so dragging off a button cancels its click instead of firing it.
    html::Node* target = state.target;
    html::Node* release_node = node_of(host_.layout->hit_test(x, y));
    while (target != nullptr && !node_contains(target, release_node)) {
        target = target->parent;
    }
    if (target == nullptr) {
        return;
    }
    // A click on a `<label>` focuses the labeled control and, for a checkable or a button,
    // delivers the click to that control. The label's own click handler runs first: retargeting
    // before the lookup dropped `onclick` on a `<label for="file">`, so the album import never
    // ran and only the file input's empty picker did. `prevent_default` on that handler cancels
    // the retarget, the same way a cancelled label click skips the control's activation.
    html::Node* already_fired = nullptr;
    if (host_.document != nullptr) {
        if (html::Node* labeled = label_control(*host_.document, target)) {
            if (labeled != target && !html::node_contains(labeled, target)) {
                html::Node* label_handler = handler_owner(target, "click");
                const bool label_owns_click =
                    label_handler != nullptr && label_handler != labeled &&
                    !html::node_contains(labeled, label_handler);
                if (label_owns_click) {
                    Pointer payload = state.mods;
                    payload.x = x;
                    payload.y = y;
                    payload.button = "left";
                    payload.count = state.click_count > 0 ? state.click_count : 1;
                    const std::uint64_t dom_before = html::dom_stamp(host_.document);
                    already_fired = label_handler;
                    const bool prevented =
                        fire(label_handler, "click", nullptr, nullptr, nullptr, &payload);
                    if (html::dom_stamp(host_.document) != dom_before || prevented) {
                        return;
                    }
                }
                focus(labeled, /*keyboard_modality=*/false);
                target = labeled;
            }
        }
    }
    // Checkbox / radio toggle. It runs before the click so both click and change read the new
    // checked/value (HOST_ARCHITECTURE PR-0c / EVENT.md section 7).
    html::Node* toggled = nullptr;
    if (html::Node* box_cb = checkable_from(*host_.document, target)) {
        // The toggled control and every radio the group unchecks are the only nodes whose computed
        // style can move (docs/PERF.md I3), so they are this restyle's roots.
        std::vector<html::Node*> toggle_roots;
        if (html::is_radio(box_cb)) {
            auto name_it = box_cb->attrs.find("name");
            const std::string name =
                name_it != box_cb->attrs.end() ? name_it->second : std::string{};
            if (!name.empty() && host_.document->root) {
                uncheck_radio_group(host_.document->root.get(), name, box_cb, &toggle_roots);
            }
            box_cb->checked = true;
        } else {
            box_cb->checked = !box_cb->checked;
            box_cb->value = box_cb->checked ? "true" : "false";
        }
        toggle_roots.push_back(box_cb);
        note_restyle_roots(toggle_roots);
        toggled = box_cb;
    }
    Pointer payload = state.mods;
    payload.x = x;
    payload.y = y;
    payload.button = "left";
    payload.count = state.click_count > 0 ? state.click_count : 1;
    // A handler may rebuild the subtree the release came from; the tail reads nodes resolved
    // before the dispatch, so give up on it once the structural clock moves.
    const std::uint64_t dom_before = html::dom_stamp(host_.document);
    bool click_prevented = false;
    if (html::Node* click_handler = handler_owner(target, "click")) {
        // A wrapping label was already fired above. Walking up from the control would find it
        // again.
        if (click_handler != already_fired) {
            click_prevented = fire(click_handler, "click", nullptr, nullptr, nullptr, &payload);
        }
        if (html::dom_stamp(host_.document) != dom_before) {
            return;
        }
    }
    if (payload.count >= 2) {
        if (html::Node* dbl = handler_owner(target, "dblclick")) {
            fire(dbl, "dblclick", nullptr, nullptr, nullptr, &payload);
        }
    }
    if (toggled != nullptr) {
        fire(toggled, "change");
    }
    // `<input type=file>` sugar: the picker opens on the click.
    for (html::Node* cursor = target; cursor != nullptr; cursor = cursor->parent) {
        if (is_file_input(cursor)) {
            activate_file_input(cursor);
            break;
        }
    }
    // Click defaults run after the handler. prevent_default cancels every one of them.
    if (!click_prevented) {
        // `<a href>` navigation. An app that already called open_uri cancels this.
        follow_hyperlink(host_, target);
        // `<summary>` toggles its parent `<details>` (EVENT.md section 7).
        if (html::Node* details = details_of_summary_click(target)) {
            toggle_details_open(details);
            note_restyle_roots({details});
        }
        // `<button command="show-modal|close" commandfor="id">` (EVENT.md section 7).
        apply_dialog_command(target);
        if (html::Node* form = form_ancestor(target)) {
            if (is_reset_button(target)) {
                reset_form(form);
            } else if (is_submit_button(target)) {
                submit_form(form, target);
            }
        }
    }
}

void Interaction::click_target(html::Node* node) { activate_control(node); }

void Interaction::activate_control(html::Node* node) {
    if (node == nullptr || node->disabled) {
        return;
    }
    if (fire(node, "click")) {
        return;
    }
    follow_hyperlink(host_, node);
    if (html::Node* details = details_of_summary_click(node)) {
        toggle_details_open(details);
        note_restyle_roots({details});
    }
    apply_dialog_command(node);
    if (html::Node* form = form_ancestor(node)) {
        if (is_reset_button(node)) {
            reset_form(form);
        } else if (is_submit_button(node)) {
            submit_form(form, node);
        }
    }
}

}  // namespace input
}  // namespace zui
