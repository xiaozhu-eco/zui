#include "ascii.h"
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
bool is_checkable(const html::Node* node) {
    return html::is_checkbox(node) || html::is_radio(node);
}

html::Node* find_checkable_desc(html::Node* node) {
    if (node == nullptr) {
        return nullptr;
    }
    if (is_checkable(node)) {
        return node;
    }
    for (auto& child : node->children) {
        if (html::Node* found = find_checkable_desc(child.get())) {
            return found;
        }
    }
    return nullptr;
}

html::Node* checkable_from(html::Document& doc, html::Node* node) {
    if (is_checkable(node)) {
        return node;
    }
    for (html::Node* cursor = node; cursor != nullptr; cursor = cursor->parent) {
        if (cursor->tag != "label") {
            continue;
        }
        auto it = cursor->attrs.find("for");
        if (it != cursor->attrs.end() && !it->second.empty()) {
            html::Node* target = html::find(doc, it->second);
            if (is_checkable(target)) {
                return target;
            }
        }
        if (html::Node* inner = find_checkable_desc(cursor)) {
            return inner;
        }
    }
    return nullptr;
}

void uncheck_radio_group(html::Node* node, const std::string& name, html::Node* keep,
                         std::vector<html::Node*>* unchecked) {
    if (node == nullptr) {
        return;
    }
    if (html::is_radio(node) && node != keep) {
        auto it = node->attrs.find("name");
        if (it != node->attrs.end() && it->second == name) {
            node->checked = false;
            if (unchecked != nullptr) {
                unchecked->push_back(node);
            }
        }
    }
    for (auto& child : node->children) {
        uncheck_radio_group(child.get(), name, keep, unchecked);
    }
}

bool is_file_input(const html::Node* node) {
    return node != nullptr && !node->disabled && html::is_file(node);
}

std::string path_basename(std::string_view path) {
    const std::size_t slash = path.find_last_of("/\\");
    if (slash == std::string_view::npos) {
        return std::string(path);
    }
    return std::string(path.substr(slash + 1));
}

void trim_inplace(std::string& s) { zui::trim_inplace(s); }

/// Map a simple HTML `accept` list onto FileFilter patterns (extensions, no `*`).
FileDialogOptions file_dialog_from_input(const html::Node& node) {
    FileDialogOptions opt;
    opt.multi = node.attrs.find("multiple") != node.attrs.end();
    const auto accept = node.attrs.find("accept");
    if (accept == node.attrs.end() || accept->second.empty()) {
        return opt;
    }
    std::string patterns;
    std::string token;
    const std::string& raw = accept->second;
    auto flush_token = [&] {
        trim_inplace(token);
        if (token.empty()) {
            return;
        }
        std::string ext;
        if (token[0] == '.') {
            ext = token.substr(1);
        } else if (token == "image/*") {
            ext = "png;jpg;jpeg;gif;webp;bmp;svg";
        } else if (token == "text/plain") {
            ext = "txt;text";
        } else if (token == "text/markdown") {
            ext = "md;markdown";
        } else if (token.find('/') != std::string::npos) {
            token.clear();
            return;
        } else {
            ext = token;
        }
        for (char& c : ext) {
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        if (!ext.empty()) {
            if (!patterns.empty()) {
                patterns.push_back(';');
            }
            patterns += ext;
        }
        token.clear();
    };
    for (char c : raw) {
        if (c == ',') {
            flush_token();
        } else {
            token.push_back(c);
        }
    }
    flush_token();
    if (!patterns.empty()) {
        opt.filters.push_back({"Files", patterns});
    }
    return opt;
}

}  // namespace input_detail

void Interaction::note_select_geometry(html::Node* select) {
    if (select == nullptr) {
        return;
    }
    if (host_.layout != nullptr) {
        host_.layout->sync_text(select);
        host_.layout->mark_needs_layout(select);
    }
    if (host_.mark_geometry) {
        host_.mark_geometry();
    }
    note_restyle_roots({select});
}
void Interaction::close_select_popup() {
    if (pointer_.open_select_ == nullptr) {
        return;
    }
    html::Node* select = pointer_.open_select_;
    pointer_.open_select_ = nullptr;
    select->attrs.erase("open");
    for (auto& ch : select->children) {
        if (html::is_option(ch.get())) {
            ch->attrs.erase("style");
        }
    }
    note_select_geometry(select);
}
void Interaction::open_select_popup(html::Node* select) {
    if (!html::is_select(select) || select->disabled) {
        return;
    }
    if (pointer_.open_select_ == select) {
        return;
    }
    if (pointer_.open_select_ != nullptr) {
        close_select_popup();
    }
    pointer_.open_select_ = select;
    select->attrs["open"] = "";
    float left = 0.f;
    float top = 0.f;
    float width = 80.f;
    // Fall back to a 22px row when the control has not been laid out yet.
    float row = 22.f;
    if (host_.layout != nullptr) {
        if (layout::Box* box = host_.layout->principal_box(select)) {
            left = box->geo.world_x;
            top = box->geo.world_y + box->geo.height;
            width = std::max(box->geo.width, 40.f);
            if (box->geo.height > 1.f) {
                row = std::max(18.f, box->geo.height);
            }
        }
    }
    float y = top;
    for (auto& ch : select->children) {
        if (!html::is_option(ch.get())) {
            continue;
        }
        // One row per option, as tall as the control and stacked without a gap: the UA rule for
        // `select[open] > option` is border-box, so an explicit height plus a matching line-height
        // gives a contiguous menu instead of the list's own (smaller) box height leaving gaps
        // between bordered rows.
        ch->attrs["style"] = "left:" + std::to_string(left) + "px;top:" + std::to_string(y) +
                             "px;min-width:" + std::to_string(width) +
                             "px;height:" + std::to_string(row) +
                             "px;line-height:" + std::to_string(row) + "px;";
        y += row;
    }
    note_select_geometry(select);
}
void Interaction::forget_subtree(html::Node* dead) {
    if (dead == nullptr) {
        return;
    }
    auto under = [](html::Node* n, html::Node* root) {
        for (; n != nullptr; n = n->parent) {
            if (n == root) {
                return true;
            }
        }
        return false;
    };
    for (auto it = pointer_.pointer_capture_.begin(); it != pointer_.pointer_capture_.end();) {
        if (under(it->second, dead)) {
            it = pointer_.pointer_capture_.erase(it);
        } else {
            ++it;
        }
    }
    if (under(text_.select_node_, dead)) {
        text_.selecting_text_ = false;
        text_.select_node_ = nullptr;
    }
    // `Document::selection` names a node the paint walk reads, so a selection inside the subtree
    // has to go with it - the same dangling-pointer rule the pointers above follow. Anchor and
    // focus can name different nodes than `selection.node` (a cross-element Range).
    if (host_.document != nullptr) {
        const html::SelectionRange& sel = host_.document->selection;
        if (under(sel.node, dead) || under(sel.anchor.node, dead) || under(sel.focus.node, dead)) {
            html::clear_selection(*host_.document);
        }
    }
    if (under(pointer_.open_select_, dead)) {
        pointer_.open_select_ = nullptr;
    }
    // A queued caret reveal names the control the caret moved in; the subtree that held it is
    // being freed here.
    if (under(text_.reveal_caret_, dead)) {
        text_.reveal_caret_ = nullptr;
    }
    if (under(drag_.html_drag_.source, dead)) {
        drag_.html_drag_.source = nullptr;
    }
    if (under(drag_.html_drag_.zone, dead)) {
        drag_.html_drag_.zone = nullptr;
    }
    if (under(drag_.html_drag_.hit, dead)) {
        drag_.html_drag_.hit = nullptr;
    }
    // A drop handler may rebuild the pressed row before release finishes the click. Clear
    // `:active` while the nodes are still linked, then drop the pointers so finish_click does
    // not walk freed memory.
    for (auto& entry : pointer_.press_state_) {
        PressState& state = entry.second;
        if (under(state.active_node, dead)) {
            set_active(state.active_node, false);
            state.active_node = nullptr;
        }
        if (under(state.target, dead)) {
            state.target = nullptr;
        }
        if (under(state.scroller, dead)) {
            state.scroller = nullptr;
        }
    }
    if (under(focused(), dead)) {
        text_.edit_dirty_ = false;
    }
}
void Interaction::activate_file_input(html::Node* node) {
    if (node == nullptr || !is_file_input(node) || !host_.pick_open) {
        return;
    }
    const PickPaths picked = host_.pick_open(file_dialog_from_input(*node));
    if (picked.status != PickStatus::Ok || picked.paths.empty()) {
        return;
    }
    std::string display;
    const std::size_t limit =
        node->attrs.find("multiple") != node->attrs.end() ? picked.paths.size() : std::size_t{1};
    for (std::size_t i = 0; i < limit && i < picked.paths.size(); ++i) {
        const std::string name = path_basename(picked.paths[i]);
        if (name.empty()) {
            continue;
        }
        if (!display.empty()) {
            display += "; ";
        }
        display += name;
    }
    if (display.empty()) {
        display = path_basename(picked.paths.front());
    }
    node->value = display;
    // Painter draws the filename; keep text empty so layout does not emit a glyph run.
    node->text.clear();
    if (host_.mark_geometry) {
        host_.mark_geometry();
    }
    note_restyle_roots({node});
    fire(node, "input");
    fire(node, "change");
}

}  // namespace input
}  // namespace zui
