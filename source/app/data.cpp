#include <zui/app.h>

#include "perf/perf_budget.h"
#include "backends/host_media.h"
#include "backends/platform.h"
#include "backends/renderer.h"
#include "css/engine/engine.h"
#include "css/transition/transition.h"
#include "html/document.h"
#include "html/selection.h"
#include "input/interaction.h"
#include "layout/caret/caret_service.h"
#include "layout/engine.h"
#include "layout/viewport.h"
#include "layout/box.h"
#include "paint/display_list.h"
#include "trace/trace.h"

#include "app/impl.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>


namespace zui {

bool Document::fragment_into(std::string_view path, std::string_view into_id,
                             bool replace_children) {
    Document::Impl& impl = *impl_;
    if (impl.page != nullptr) {
        return false;
    }
    perf::begin_write();
    if (!impl.session->loaded || impl.session->document.root == nullptr) {
        ZUI_ERROR("zui::App::load_fragment: no document loaded");
        return false;
    }
    namespace fs = std::filesystem;
    fs::path file(std::string(path.begin(), path.end()));
    if (!file.is_absolute()) {
        file = fs::path(impl.session->document.source_path).parent_path() / file;
    }
    html::Node* into = nullptr;
    if (!into_id.empty()) {
        into = html::find(impl.session->document, into_id);
    } else {
        into = html::find_tag(impl.session->document.root.get(), "body");
        if (into == nullptr) {
            into = impl.session->document.root.get();
        }
    }
    if (into == nullptr) {
        ZUI_ERROR("zui::App::load_fragment: parent not found");
        return false;
    }
    const size_t sheet_begin = impl.session->document.stylesheets.size();
    std::string error;
    auto before_remove = [&impl](html::Node* old) {
        if (old == nullptr) {
            return;
        }
        impl.session->drop_style_roots_under(old);
        impl.session->forget_subtree(old);
        impl.session->layouter.detach_node(old);
    };
    if (!html::attach_fragment(impl.session->document, into, file.string(), error, before_remove,
                               replace_children)) {
        ZUI_ERROR("zui::App::load_fragment: {}", error);
        return false;
    }
    for (size_t i = sheet_begin; i < impl.session->document.stylesheets.size(); ++i) {
        std::string css_error;
        const html::Document::StylesheetSource& sheet = impl.session->document.stylesheets[i];
        const bool ok = sheet.href.empty() ? impl.session->styles.add_sheet_text(sheet.text, css_error)
                                           : impl.session->styles.add_sheet_file(sheet.href, css_error);
        if (!ok) {
            ZUI_ERROR("zui::App::load_fragment: {}", css_error);
        }
    }
    impl.session->pending_attach.insert(into);
    impl.session->restyle(App::Impl::kReasonFragment);
    ZUI_DEBUG("[fragment] loaded {} into #{}", file.filename().string(), into->id);
    return true;
}

bool Document::load_fragment(std::string_view path, std::string_view into_id) {
    return fragment_into(path, into_id, false);
}

bool Document::replace_fragment(std::string_view path, std::string_view into_id) {
    return fragment_into(path, into_id, true);
}

ModelBind Document::set_model(std::string_view id, const ListModel& model) {
    if (impl_->page != nullptr) {
        return {false, "child document"};
    }
    perf::begin_write();
    if (!impl_->session->loaded) {
        ZUI_ERROR("zui::App::set_model: no document loaded");
        return {false, "no document loaded"};
    }
    html::Node* host = html::find(impl_->session->document, id);
    auto retiring = [this](html::Node* node) {
        impl_->session->drop_style_roots_under(node);
        impl_->session->forget_subtree(node);
        impl_->session->layouter.detach_node(node);
    };
    bool unchanged = false;
    std::string error;
    // The signature walk notifies `Document::watch`. With no watch on this page it only builds
    // strings. Calendar's month rebuild calls `set_model` once per list.
    const bool watching = impl_->session->has_watch(impl_->page);
    std::unordered_map<std::string, std::string> before_sig;
    if (host != nullptr && watching) {
        std::function<void(html::Node*)> snap = [&](html::Node* node) {
            if (node == nullptr) {
                return;
            }
            if (!node->id.empty()) {
                std::string sig = node->text;
                sig.push_back('\n');
                sig += node->value;
                sig.push_back(node->checked ? '1' : '0');
                std::vector<std::pair<std::string, std::string>> attrs(node->attrs.begin(),
                                                                       node->attrs.end());
                std::sort(attrs.begin(), attrs.end());
                for (const auto& item : attrs) {
                    sig.push_back('\n');
                    sig += item.first;
                    sig.push_back('=');
                    sig += item.second;
                }
                before_sig.emplace(node->id, std::move(sig));
            }
            for (const auto& child : node->children) {
                snap(child.get());
            }
        };
        snap(host);
    }
    // Rows, each row's nested `with_children` host and, for a `TableModel`, the column stamp all
    // live in `html::model` (RFC_MODEL.md section 6): the engine owns the reconcile, and the app
    // only hands over the model.
    std::vector<html::Node*> sync_hosts;
    if (!html::apply_list_model(impl_->session->document, id, model, error, retiring, &unchanged,
                                &sync_hosts)) {
        ZUI_ERROR("zui::App::set_model: {}", error);
        return {false, error};
    }
    if (unchanged) {
        ZUI_TRACE("[model] #{} unchanged", std::string(id));
        return {true, {}};
    }
    if (host != nullptr) {
        // Child boxes that still belong to a kept node stay; sync_children builds only the new
        // ones. The host's parent is not queued (PERF.md I4).
        impl_->session->pending_attach.insert(host);
        // A row host that kept its own box still had its children replaced by the column stamp or
        // by `with_children`, and the top host's child list is unchanged, so `sync_children` would
        // stop above them (RFC_MODEL.md KD3/KD6). Each one is queued on its own.
        for (html::Node* touched : sync_hosts) {
            if (touched != nullptr && touched != host) {
                impl_->session->pending_attach.insert(touched);
            }
        }
        // The kept rows keep their boxes, and the placement walk clears their `geo.runs`. The
        // frame writes the single line back after it places and before it paints (RFC_MODEL.md
        // wave 1); enqueueing the host only is the same I4 rule as above.
        impl_->session->pending_run_refill.insert(host);
        html::note_paint_change(host);
    }
    impl_->session->restyle_at(App::Impl::kReasonModel, host);
    if (host != nullptr && watching) {
        std::unordered_map<std::string, std::string> after_sig;
        std::function<void(html::Node*)> snap = [&](html::Node* node) {
            if (node == nullptr) {
                return;
            }
            if (!node->id.empty()) {
                std::string sig = node->text;
                sig.push_back('\n');
                sig += node->value;
                sig.push_back(node->checked ? '1' : '0');
                std::vector<std::pair<std::string, std::string>> attrs(node->attrs.begin(),
                                                                       node->attrs.end());
                std::sort(attrs.begin(), attrs.end());
                for (const auto& item : attrs) {
                    sig.push_back('\n');
                    sig += item.first;
                    sig.push_back('=');
                    sig += item.second;
                }
                after_sig.emplace(node->id, std::move(sig));
            }
            for (const auto& child : node->children) {
                snap(child.get());
            }
        };
        snap(host);
        for (const auto& item : before_sig) {
            const auto it = after_sig.find(item.first);
            if (it == after_sig.end() || it->second != item.second) {
                impl_->session->notify_watch(item.first, impl_->page);
            }
        }
        for (const auto& item : after_sig) {
            if (before_sig.find(item.first) == before_sig.end()) {
                impl_->session->notify_watch(item.first, impl_->page);
            }
        }
    }
    ZUI_TRACE("[model] #{} synced", std::string(id));
    return {true, {}};
}

bool Document::set_theme(std::string_view name) {
    if (impl_->page != nullptr) {
        return false;
    }
    perf::begin_write();
    if (!impl_->session->loaded || impl_->session->document.root == nullptr) {
        ZUI_ERROR("zui::App::set_theme: no document loaded");
        return false;
    }
    std::string stem(name.begin(), name.end());
    for (char c : stem) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')) {
            ZUI_ERROR("zui::App::set_theme: invalid name");
            return false;
        }
    }
    html::Node* html_el = html::find_tag(impl_->session->document.root.get(), "html");
    if (html_el == nullptr) {
        html_el = impl_->session->document.root.get();
    }
    if (impl_->session->theme_applied && impl_->session->theme_stem == stem) {
        return true;
    }
    std::string next;
    size_t i = 0;
    const std::string& cur = html_el->class_name;
    while (i < cur.size()) {
        while (i < cur.size() && cur[i] == ' ') {
            ++i;
        }
        const size_t j = cur.find(' ', i);
        const size_t n = (j == std::string::npos ? cur.size() : j) - i;
        if (n > 0 && cur.compare(i, 6, "theme-") != 0) {
            if (!next.empty()) {
                next.push_back(' ');
            }
            next.append(cur, i, n);
        }
        if (j == std::string::npos) {
            break;
        }
        i = j + 1;
    }
    if (!stem.empty()) {
        if (!next.empty()) {
            next.push_back(' ');
        }
        next += "theme-";
        next += stem;
    }
    html_el->class_name = next;
    html_el->attrs["class"] = next;
    namespace fs = std::filesystem;
    const fs::path pack = fs::path(impl_->session->document.source_path).parent_path() / (stem + ".css");
    std::string css_error;
    if (!stem.empty() && fs::exists(pack)) {
        if (!impl_->session->styles.set_theme_file(pack.string(), css_error)) {
            ZUI_WARN("zui::App::set_theme: {}", css_error);
        }
    } else {
        impl_->session->styles.set_theme_file({}, css_error);
    }
    impl_->session->theme_applied = true;
    impl_->session->theme_stem = std::move(stem);
    impl_->session->restyle();
    ZUI_DEBUG("[theme] {} applied", impl_->session->theme_stem);
    return true;
}

bool Document::unload_fragment(std::string_view id) {
    if (impl_->page != nullptr) {
        return false;
    }
    perf::begin_write();
    // The id map can lose entries while the node remains in the tree. Walk the
    // document by node->id so every namesake leaves, not only the mapped one.
    std::vector<html::Node*> matches;
    html::collect_nodes_with_id(impl_->session->document.root.get(), id, matches);
    if (matches.empty()) {
        ZUI_DEBUG("[fragment] #{} not found for unload", std::string(id));
        return false;
    }
    std::unordered_set<html::Node*> parents;
    // Deepest first: removing a parent invalidates descendant pointers in matches.
    std::sort(matches.begin(), matches.end(), [](html::Node* a, html::Node* b) {
        auto depth = [](html::Node* n) {
            int d = 0;
            for (; n != nullptr; n = n->parent) {
                ++d;
            }
            return d;
        };
        return depth(a) > depth(b);
    });
    std::unordered_set<html::Node*> removed;
    for (html::Node* node : matches) {
        if (node == nullptr || !removed.insert(node).second) {
            continue;
        }
        bool still_in_tree = false;
        for (html::Node* walk = node; walk != nullptr; walk = walk->parent) {
            if (walk == impl_->session->document.root.get()) {
                still_in_tree = true;
                break;
            }
        }
        if (!still_in_tree) {
            continue;
        }
        html::Node* parent = node->parent;
        impl_->session->drop_style_roots_under(node);
        impl_->session->forget_subtree(node);
        impl_->session->layouter.detach_node(node);
        if (!html::remove_node(impl_->session->document, node)) {
            continue;
        }
        if (parent != nullptr) {
            parents.insert(parent);
        }
    }
    if (parents.empty()) {
        return false;
    }
    for (html::Node* parent : parents) {
        impl_->session->layouter.mark_needs_layout(parent);
    }
    impl_->session->geometry_dirty = true;
    // Removing a node only changes its parent's child list, so only the parent's subtree can
    // change computed style (sibling positions, `:empty`, list ordinals). A `:has()` above the
    // parent adds the matching anchors' subtrees, and an author counter - or a rule mixing `:has()`
    // with `+` / `~` - still makes the closure the whole document (`closure_is_global()`); the
    // closure walk itself is `Engine::closure_nodes` (docs/PERF.md section 2.4).
    for (html::Node* parent : parents) {
        impl_->session->restyle_at(App::Impl::kReasonFragment, parent);
    }
    return true;
}

namespace {

bool falsey_bool(std::string_view value) {
    return value == "false" || value == "0" || value.empty();
}

float parsed_scroll(std::string_view value, bool& ok) {
    char* end = nullptr;
    const std::string text(value);
    const float px = std::strtof(text.c_str(), &end);
    ok = end != text.c_str();
    return px < 0.f ? 0.f : px;
}

/// True when `set` would leave the node unchanged (PERF.md I2).
bool same_field(const html::Node& node, std::string_view field, std::string_view value) {
    if (field == "text") {
        return node.text == value;
    }
    if (field == "value") {
        return node.value == value;
    }
    if (field == "checked" || field == "disabled") {
        const bool next = !falsey_bool(value);
        return field == "checked" ? node.checked == next : node.disabled == next;
    }
    if (field == "visible") {
        const bool next = !(value == "false" || value == "0");
        return node.visible == next;
    }
    if (field == "src" || field == "href") {
        const auto it = node.attrs.find(std::string(field));
        const std::string current = it == node.attrs.end() ? std::string{} : it->second;
        return current == value;
    }
    if (field == "scroll_top" || field == "scroll_y" || field == "scroll_left" ||
        field == "scroll_x") {
        bool ok = false;
        const float px = parsed_scroll(value, ok);
        if (!ok) {
            return false;
        }
        const float current =
            (field == "scroll_left" || field == "scroll_x") ? node.scroll_x : node.scroll_y;
        return current == px;
    }
    const bool data_attr = field.size() > 5 && field.compare(0, 5, "data-") == 0;
    const bool aria_attr = field.size() > 5 && field.compare(0, 5, "aria-") == 0;
    if (data_attr || aria_attr) {
        const auto it = node.attrs.find(std::string(field));
        const std::string current = it == node.attrs.end() ? std::string{} : it->second;
        return current == value;
    }
    return false;
}

}  // namespace

bool Document::set(std::string_view id, std::string_view field, std::string_view value) {
    perf::begin_write();
    App::Impl* session = impl_->session;
    App::Impl::OwnedWindow* page = impl_->page;
    html::Document& tree = page != nullptr ? page->document : session->document;
    html::Node* node = html::find(tree, id);
    if (node != nullptr && same_field(*node, field, value)) {
        return true;
    }
    const bool empty_before = node == nullptr || node->text.empty();
    if (!html::set_field(tree, id, field, value)) {
        ZUI_DEBUG("[set] #{} field '{}' was not applied", std::string(id), std::string(field));
        return false;
    }
    if (node == nullptr) {
        node = html::find(tree, id);
    }
    if (page != nullptr) {
        session->notify_watch(id, page);
        // pump_frame lays out the root and paints this page's boxes as they stand. A write has to
        // update this page's own layouter, the same way add_class / remove_class do.
        if (page->layouter != nullptr) {
            if (field == "text" || field == "value" || field == "insert") {
                page->layouter->sync_text(node);
                if (field == "text" && node != nullptr) {
                    for (auto& ch : node->children) {
                        if (ch != nullptr && ch->tag == "#text") {
                            page->layouter->sync_text(ch.get());
                            break;
                        }
                    }
                }
                page->layouter->mark_needs_layout(node);
            }
            if (field == "src" && node != nullptr) {
                page->layouter->sync_replaced_src(node);
                page->layouter->mark_needs_layout(node);
            }
            const css::Engine::CascadeResult cr =
                page->styles.cascade(page->document, 800.f, 600.f);
            std::unordered_set<html::Node*> reattach(cr.reattach.begin(), cr.reattach.end());
            std::unordered_set<html::Node*> attach;
            for (html::Node* changed : cr.reattach) {
                if (changed != nullptr) {
                    attach.insert(changed->parent != nullptr ? changed->parent : changed);
                }
            }
            // `visible` flips the `hidden` attribute. The box has to leave or rejoin its parent
            // even when the cascade reports the change only as a computed display.
            if (field == "visible" && node != nullptr) {
                attach.insert(node->parent != nullptr ? node->parent : node);
            }
            // Same rule as `flush_style`: an unboxed host whose ancestor is already queued is
            // built by that ancestor. Syncing the host itself relowers the ancestor.
            std::unordered_set<html::Node*> unboxed_attach;
            for (html::Node* host : attach) {
                if (host != nullptr && page->layouter->principal_box(host) == nullptr) {
                    unboxed_attach.insert(host);
                }
            }
            for (html::Node* host : attach) {
                bool covered = false;
                if (unboxed_attach.count(host) != 0) {
                    for (html::Node* p = host->parent; p != nullptr; p = p->parent) {
                        if (attach.count(p) != 0) {
                            covered = true;
                            break;
                        }
                    }
                }
                if (!covered) {
                    page->layouter->sync_children(host, reattach);
                }
            }
            for (html::Node* changed : cr.layout) {
                page->layouter->mark_needs_layout(changed);
            }
            page->layouter->layout(page->document, layout::Viewport{800.f, 600.f});
            if ((field == "scroll_top" || field == "scroll_y" || field == "scroll_left" ||
                 field == "scroll_x") &&
                node != nullptr) {
                if (layout::Box* box = page->layouter->principal_box(node)) {
                    // set_field already wrote the node, so set_scroll's own moved-test is false.
                    // Compare with the offset the last placement published.
                    const float prev_x = box->geo.scroll_x;
                    const float prev_y = box->geo.scroll_y;
                    float view_w = -1.f;
                    float view_h = -1.f;
                    input::document_scroll_view(*page->layouter, *box,
                                                page->layouter->scrollbar_thickness(), view_w,
                                                view_h);
                    input::set_scroll(*box, node->scroll_x, node->scroll_y, view_w, view_h);
                    if (std::fabs(node->scroll_x - prev_x) > 0.01f ||
                        std::fabs(node->scroll_y - prev_y) > 0.01f) {
                        layout::mark_place_dirty(*box);
                    }
                }
            }
            page->layouter->sync_world();
            input::settle_content_visibility(*page->layouter, page->document,
                                             page->layouter->scrollbar_thickness());
            page->needs_paint = true;
        }
        return true;
    }
    // A selection range is paint-only. set_field already bumped the paint stamp.
    if (field == "select") {
        impl_->session->notify_watch(id, nullptr);
        return true;
    }
    bool text_keeps_style = false;
    if (field == "text" || field == "value" || field == "insert") {
        impl_->session->layouter.sync_text(node);
        // Mixed-content labels: set_field also wrote the first `#text` child; sync that box too.
        if (field == "text" && node != nullptr) {
            for (auto& ch : node->children) {
                if (ch != nullptr && ch->tag == "#text") {
                    impl_->session->layouter.sync_text(ch.get());
                    break;
                }
            }
        }
        impl_->session->layouter.mark_needs_layout(node);
        impl_->session->geometry_dirty = true;
        // Emptiness is what `:empty` sees. A text edit that does not cross that line does not
        // change any selector, so the cascade can stay put (PERF.md set(text)).
        const bool empty_now = node == nullptr || node->text.empty();
        text_keeps_style = empty_before == empty_now;
    }
    if (field == "src" && node != nullptr) {
        impl_->session->layouter.sync_replaced_src(node);
        impl_->session->layouter.mark_needs_layout(node);
        impl_->session->geometry_dirty = true;
    }
    if (field == "visible" && node != nullptr) {
        impl_->session->pending_attach.insert(node->parent != nullptr ? node->parent : node);
    }
    if (field == "scroll_top" || field == "scroll_y" || field == "scroll_left" ||
        field == "scroll_x") {
        // Scroll lives on the node and is consumed by world placement, so there is no restyle. The
        // frame runs the value past the box's scroll range and remaps the world, which is exactly
        // what the interaction layer does for a wheel - marking geometry dirty was not enough,
        // because the world is only replaced after a frame that computed boxes, and a frame that
        // merely scrolled computes none. `set(..., "scroll_top", ...)` on its own did nothing.
        impl_->session->pending_scroll.insert(std::string(id));
        impl_->session->notify_watch(id, nullptr);
        return true;
    }
    if (text_keeps_style) {
        impl_->session->notify_watch(id, nullptr);
        return true;
    }
    std::uint32_t reason = App::Impl::kReasonOther;
    if (field == "checked") {
        reason = App::Impl::kReasonChecked;
    } else if (field == "text" || field == "value" || field == "insert") {
        reason = App::Impl::kReasonText;
    } else if (field == "disabled") {
        reason = App::Impl::kReasonOther;
    } else if (field == "visible") {
        reason = App::Impl::kReasonOther;
    }
    // Only this element's own subtree can change computed style. `visible` keeps the node in the
    // tree (it flips the `hidden` attribute), so its parent's other children keep their positions
    // and matches; the box-tree diff on `Children(parent)` is a separate unit (docs/PERF.md
    // section 2.1).
    impl_->session->restyle_at(reason, node);
    impl_->session->notify_watch(id, nullptr);
    return true;
}

namespace {

bool node_holds(const html::Node* host, const html::Node* node) {
    for (const html::Node* cursor = node; cursor != nullptr; cursor = cursor->parent) {
        if (cursor == host) {
            return true;
        }
    }
    return false;
}

/// `[out_a, out_b)` of @p sel inside @p host's innerText. False when the range misses @p host.
bool selection_in_host(const html::Node* host, const html::SelectionRange& sel, std::size_t& out_a,
                       std::size_t& out_b) {
    out_a = 0;
    out_b = 0;
    const html::BoundaryPoint start = html::selection_start_point(sel);
    const html::BoundaryPoint end = html::selection_end_point(sel);
    if (host == nullptr || start.node == nullptr || end.node == nullptr) {
        return false;
    }
    if (start.node == end.node && start.offset == end.offset) {
        return false;
    }
    const bool start_in = node_holds(host, start.node);
    const bool end_in = node_holds(host, end.node);
    if (!start_in && !end_in) {
        return false;
    }
    const std::size_t n = html::range_edit_text(host).size();
    out_a = start_in ? html::point_to_flatten(host, start) : 0;
    out_b = end_in ? html::point_to_flatten(host, end) : n;
    if (out_a > out_b) {
        std::swap(out_a, out_b);
    }
    if (out_b > n) {
        out_b = n;
    }
    return out_a < out_b;
}

}  // namespace

std::string Document::get(std::string_view id, std::string_view field) {
    html::Document& tree = impl_->page != nullptr ? impl_->page->document : impl_->session->document;
    html::Node* node = html::find(tree, id);
    html::Document* doc = &tree;
    if (node == nullptr) {
        return {};
    }
    if (field == "text") {
        if (!node->text.empty() || node->tag == "input" || node->tag == "textarea") {
            return node->text;
        }
        for (const auto& ch : node->children) {
            if (ch != nullptr && ch->tag == "#text") {
                return ch->text;
            }
        }
        return node->text;
    }
    if (field == "value") {
        return node->value;
    }
    if (field == "checked") {
        return node->checked ? "true" : "false";
    }
    if (field == "visible") {
        return node->visible ? "true" : "false";
    }
    if (field == "disabled") {
        return node->disabled ? "true" : "false";
    }
    if (field == "class") {
        return node->class_name;
    }
    if (field == "src") {
        auto it = node->attrs.find("src");
        return it == node->attrs.end() ? std::string{} : it->second;
    }
    if (field == "href") {
        auto it = node->attrs.find("href");
        return it == node->attrs.end() ? std::string{} : it->second;
    }
    if (field == "scroll_top" || field == "scroll_y") {
        return std::to_string(node->scroll_y);
    }
    if (field == "scroll_left" || field == "scroll_x") {
        return std::to_string(node->scroll_x);
    }
    if (field == "caret") {
        if (!html::is_editing_host(node)) {
            return {};
        }
        std::size_t caret = static_cast<std::size_t>(std::max(node->caret, 0));
        if (caret > node->value.size()) {
            caret = node->value.size();
        }
        return std::to_string(caret);
    }
    if (field == "selection" || field == "selection_start" || field == "selection_end") {
        const html::SelectionRange& sel = doc->selection;
        if (html::is_editing_host(node)) {
            std::size_t a = node->select_anchor;
            std::size_t b = static_cast<std::size_t>(std::max(node->caret, 0));
            const std::string& payload = !node->value.empty() ? node->value : node->text;
            if (sel.node == node && sel.end >= sel.start && sel.end <= payload.size()) {
                a = sel.start;
                b = sel.end;
            }
            if (a > b) {
                std::swap(a, b);
            }
            if (a == b || b > payload.size()) {
                return {};
            }
            if (field == "selection_start") {
                return std::to_string(a);
            }
            if (field == "selection_end") {
                return std::to_string(b);
            }
            return payload.substr(a, b - a);
        }
        // A paper range lives on the text nodes under this element, not on the element itself.
        std::size_t a = 0;
        std::size_t b = 0;
        if (!selection_in_host(node, sel, a, b)) {
            return {};
        }
        if (field == "selection_start") {
            return std::to_string(a);
        }
        if (field == "selection_end") {
            return std::to_string(b);
        }
        const std::string payload = html::range_edit_text(node);
        if (b > payload.size()) {
            b = payload.size();
        }
        if (a >= b) {
            return {};
        }
        return payload.substr(a, b - a);
    }
    if (field == "validity") {
        const auto it = node->attrs.find("aria-invalid");
        return it != node->attrs.end() && it->second == "true" ? "invalid" : "valid";
    }
    const bool data_attr = field.size() > 5 && field.compare(0, 5, "data-") == 0;
    const bool aria_attr = field.size() > 5 && field.compare(0, 5, "aria-") == 0;
    if (data_attr || aria_attr) {
        const auto it = node->attrs.find(std::string(field));
        return it == node->attrs.end() ? std::string{} : it->second;
    }
    return {};
}

namespace {

html::Node* selection_caret_node(html::Document& document) {
    const html::SelectionRange& sel = document.selection;
    if (sel.focus.node != nullptr) {
        return sel.focus.node;
    }
    return sel.node;
}

}  // namespace

std::optional<std::size_t> Document::text_offset_at(std::string_view id, float x, float y) {
    auto resolve = [&](html::Document& document, layout::LayoutEngine& layouter) -> std::optional<std::size_t> {
        html::Node* node = html::find(document, id);
        if (node == nullptr) {
            return std::nullopt;
        }
        layout::Box* box = layouter.principal_box(node);
        if (box == nullptr) {
            return std::nullopt;
        }
        const std::string edit = layout::caret_edit_text(node);
        if (edit.empty()) {
            return std::nullopt;
        }
        const layout::EditMapping mapping = layout::make_edit_mapping(*box, edit);
        if (mapping.spans.empty()) {
            return std::nullopt;
        }
        const layout::CaretHit hit = layout::hit_test(*box, mapping, x, y);
        if (!hit.ok) {
            return std::nullopt;
        }
        return hit.offset;
    };
    if (impl_->page != nullptr) {
        App::Impl::OwnedWindow* page = impl_->page;
        if (page->layouter == nullptr) {
            return std::nullopt;
        }
        if (!page->layouter->world_is_current()) {
            page->layouter->sync_world();
        }
        return resolve(page->document, *page->layouter);
    }
    impl_->session->flush_style();
    if (impl_->session->sync_list_windows()) {
        impl_->session->flush_style();
    }
    impl_->session->flush_scroll(nullptr);
    if (!impl_->session->layouter.world_is_current()) {
        impl_->session->layouter.sync_world();
    }
    return resolve(impl_->session->document, impl_->session->layouter);
}

void Document::set_text_caret(std::string_view id, std::optional<std::size_t> offset) {
    html::Document* doc = nullptr;
    if (impl_->page != nullptr) {
        doc = &impl_->page->document;
    } else if (impl_->session != nullptr) {
        doc = &impl_->session->document;
    }
    if (doc == nullptr || doc->root == nullptr) {
        return;
    }
    if (!offset.has_value() || id.empty()) {
        // Hide a paper / non-editable caret. A form-control Selection (the parked textarea a
        // writing view maps onto) stays: clearing it dropped zmd's source highlight whenever the
        // paper asked to hide its bar.
        html::Node* host = selection_caret_node(*doc);
        if (host != nullptr && host->tag == "#text" && host->parent != nullptr) {
            host = host->parent;
        }
        if (host == nullptr || !html::is_editing_host(host)) {
            html::clear_selection(*doc);
        }
        return;
    }
    html::Node* node = html::find(*doc, id);
    if (node == nullptr) {
        html::clear_selection(*doc);
        return;
    }
    html::BoundaryPoint p;
    if (!html::flatten_to_point(node, *offset, p) || p.node == nullptr) {
        p.node = node;
        p.offset = *offset;
    }
    html::set_selection(*doc, p, p);
}

std::optional<TextCaret> Document::text_caret_rect() {
    auto resolve = [&](html::Document& document, layout::LayoutEngine& layouter) -> std::optional<TextCaret> {
        html::Node* node = selection_caret_node(document);
        if (node == nullptr) {
            return std::nullopt;
        }
        // A highlight has no caret bar (CSS UI / Chromium). text_caret_rect is the bar the paper
        // hides while a source range is selected.
        if (!html::selection_collapsed(document.selection)) {
            return std::nullopt;
        }
        html::Node* host = node;
        if (node->tag == "#text" && node->parent != nullptr) {
            host = node->parent;
        }
        const html::BoundaryPoint at =
            document.selection.focus.node != nullptr
                ? document.selection.focus
                : html::BoundaryPoint{node, document.selection.end};
        layout::Box* box = layouter.principal_box(host);
        if (box == nullptr) {
            box = layouter.principal_box(node);
        }
        if (box == nullptr) {
            return std::nullopt;
        }
        const layout::CaretRect bar = layout::caret_rect_at(*box, at);
        if (!bar.ok || bar.height <= 0.f) {
            return std::nullopt;
        }
        TextCaret out;
        out.x = bar.x;
        out.y = bar.y;
        out.width = 2.f;
        out.height = bar.height;
        return out;
    };
    if (impl_->page != nullptr) {
        App::Impl::OwnedWindow* page = impl_->page;
        if (page->layouter == nullptr) {
            return std::nullopt;
        }
        if (!page->layouter->world_is_current()) {
            page->layouter->sync_world();
        }
        return resolve(page->document, *page->layouter);
    }
    impl_->session->flush_style();
    if (impl_->session->sync_list_windows()) {
        impl_->session->flush_style();
    }
    impl_->session->flush_scroll(nullptr);
    if (!impl_->session->layouter.world_is_current()) {
        impl_->session->layouter.sync_world();
    }
    return resolve(impl_->session->document, impl_->session->layouter);
}

std::optional<ElementBox> Document::measure(std::string_view id) {
    ElementBox out;
    if (impl_->page != nullptr) {
        App::Impl::OwnedWindow* page = impl_->page;
        html::Node* node = html::find(page->document, id);
        if (node == nullptr || page->layouter == nullptr) {
            return std::nullopt;
        }
        if (!page->layouter->world_is_current()) {
            page->layouter->sync_world();
        }
        layout::Box* box = page->layouter->principal_box(node);
        if (box == nullptr) {
            return std::nullopt;
        }
        const layout::Geometry& geo = box->geo;
        out.border.x = geo.world_x;
        out.border.y = geo.world_y;
        out.border.width = geo.width;
        out.border.height = geo.height;
        out.content_width = geo.content_width;
        out.content_height = geo.content_height;
        out.client_width = geo.content_width + geo.padding.horizontal();
        out.client_height = geo.content_height + geo.padding.vertical();
        out.scroll_width = geo.scroll_width;
        out.scroll_height = geo.scroll_height;
        out.laid = geo.width > 0.0f && geo.height > 0.0f;
        return out;
    }
    html::Node* node = html::find(impl_->session->document, id);
    if (node == nullptr) {
        return std::nullopt;
    }
    // getBoundingClientRect() forces pending style/layout before it reads. This does not dispatch
    // input events and does not reset the frame's visual flag, so a handler may measure mid-frame.
    // It opens no perf window of its own: inside a frame the flush is already counted, and a
    // measurement loop outside a frame should not publish one window per call.
    impl_->session->flush_style();
    if (impl_->session->sync_list_windows()) {
        impl_->session->flush_style();
    }
    impl_->session->flush_scroll(nullptr);
    if (!impl_->session->layouter.world_is_current()) {
        impl_->session->layouter.sync_world();
    }
    layout::Box* box = impl_->session->layouter.principal_box(node);
    if (box == nullptr) {
        return std::nullopt;
    }
    const layout::Geometry& geo = box->geo;
    out.border.x = geo.world_x;
    out.border.y = geo.world_y;
    out.border.width = geo.width;
    out.border.height = geo.height;
    out.content_width = geo.content_width;
    out.content_height = geo.content_height;
    out.client_width = geo.content_width + geo.padding.horizontal();
    out.client_height = geo.content_height + geo.padding.vertical();
    out.scroll_width = geo.scroll_width;
    out.scroll_height = geo.scroll_height;
    out.laid = geo.width > 0.0f && geo.height > 0.0f;
    return out;
}



GamepadState Host::gamepad(std::size_t index) const {
    const backends::GamepadFrame& frame = impl_->session->gamepads;
    if (index < frame.pads.size()) {
        return frame.pads[index];
    }
    return GamepadState{};
}

std::size_t Host::gamepad_count() const { return impl_->session->gamepads.pads.size(); }

bool Document::add_class(std::string_view id, std::string_view class_name) {
    perf::begin_write();
    if (impl_->page != nullptr) {
        App::Impl::OwnedWindow* page = impl_->page;
        html::Node* node = html::find(page->document, id);
        if (node == nullptr) {
            return false;
        }
        if (html::has_class_token(node->class_name, class_name)) {
            return true;
        }
        if (!html::add_class(page->document, id, class_name)) {
            return false;
        }
        if (page->layouter != nullptr) {
            page->styles.cascade(page->document, 800.f, 600.f);
            page->layouter->layout(page->document, layout::Viewport{800.f, 600.f});
            page->layouter->sync_world();
        }
        return true;
    }
    html::Node* node = html::find(impl_->session->document, id);
    if (node == nullptr) {
        return false;
    }
    if (html::has_class_token(node->class_name, class_name)) {
        return true;
    }
    if (!html::add_class(impl_->session->document, id, class_name)) {
        return false;
    }
    impl_->session->restyle_at(App::Impl::kReasonClass, node);
    return true;
}

bool Document::remove_class(std::string_view id, std::string_view class_name) {
    perf::begin_write();
    if (impl_->page != nullptr) {
        App::Impl::OwnedWindow* page = impl_->page;
        html::Node* node = html::find(page->document, id);
        if (node == nullptr) {
            return false;
        }
        if (!html::has_class_token(node->class_name, class_name)) {
            return true;
        }
        if (!html::remove_class(page->document, id, class_name)) {
            return false;
        }
        if (page->layouter != nullptr) {
            page->styles.cascade(page->document, 800.f, 600.f);
            page->layouter->layout(page->document, layout::Viewport{800.f, 600.f});
            page->layouter->sync_world();
        }
        return true;
    }
    html::Node* node = html::find(impl_->session->document, id);
    if (node == nullptr) {
        return false;
    }
    if (!html::has_class_token(node->class_name, class_name)) {
        return true;
    }
    if (!html::remove_class(impl_->session->document, id, class_name)) {
        return false;
    }
    impl_->session->restyle_at(App::Impl::kReasonClass, node);
    return true;
}

Document::WatchId Document::watch(std::string_view id, std::function<void(std::string_view id)> fn) {
    if (id.empty() || !fn) {
        return 0;
    }
    App::Impl::Watch watch;
    watch.id = ++impl_->session->next_watch;
    watch.page = impl_->page;
    watch.node.assign(id.begin(), id.end());
    watch.fn = std::move(fn);
    const Document::WatchId issued = watch.id;
    impl_->session->watches.push_back(std::move(watch));
    return issued;
}

void Document::unwatch(WatchId id) {
    if (id == 0) {
        return;
    }
    auto& watches = impl_->session->watches;
    App::Impl::OwnedWindow* page = impl_->page;
    watches.erase(std::remove_if(watches.begin(), watches.end(),
                                 [id, page](const App::Impl::Watch& watch) {
                                     return watch.id == id && watch.page == page;
                                 }),
                  watches.end());
}

bool App::Impl::has_watch(void* page) const {
    for (const Watch& watch : watches) {
        if (watch.page == page && watch.fn) {
            return true;
        }
    }
    return false;
}

void App::Impl::notify_watch(std::string_view id, void* page) {
    std::vector<std::function<void(std::string_view)>> callbacks;
    for (const Watch& watch : watches) {
        if (watch.page == page && watch.node == id && watch.fn) {
            callbacks.push_back(watch.fn);
        }
    }
    for (const auto& fn : callbacks) {
        fn(id);
    }
}

}  // namespace zui
