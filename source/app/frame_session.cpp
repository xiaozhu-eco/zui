#include "app/frame_session.h"

#include "app/impl.h"
#include "layout/box.h"

#include <chrono>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zui::frame {

bool Access::attach(App& app, const Session& session) {
    App::Impl& impl = *app.m_impl;
    if (!impl.loaded || impl.document.root == nullptr || session.window == nullptr ||
        session.renderer == nullptr || impl.loop_window != nullptr || impl.test_interaction) {
        return false;
    }

    backends::IWindow* window = session.window;
    impl.borrowed_window = window;
    impl.borrowed_renderer = session.renderer;
    impl.active_window = window;

    impl.loop_painter = paint::Painter{};
    if (const char* on = std::getenv("ZUI_RETAINED_PAINT")) {
        const bool enabled = !(on[0] == '0' && on[1] == '\0');
        impl.loop_painter.set_retained(enabled);
        ZUI_INFO("[paint] retained listings {} (ZUI_RETAINED_PAINT)", enabled ? "on" : "off");
    }
    impl.loop_list = paint::DisplayList{};
    window->set_ime(false);
    if (impl.pending_icon.has_value()) {
        window->set_icon(*impl.pending_icon);
    }
    impl.running = true;
    impl.clock_on = false;

    input::InteractionHost input_host;
    input_host.document = &impl.document;
    input_host.layout = &impl.layouter;
    input_host.metrics = &impl.metrics;
    input_host.scrollbar_thickness = impl.layouter.scrollbar_thickness();
    input_host.hovered = &impl.hovered;
    input_host.focused = &impl.focused;
    input_host.scroll_drag = &impl.scroll_drag;
    input_host.restyle = [&app] { app.m_impl->restyle(App::Impl::kReasonHover); };
    input_host.restyle_roots = [&app](const std::vector<html::Node*>& roots) {
        app.m_impl->restyle_many(App::Impl::kReasonHover, roots);
    };
    input_host.hover_styles = [&app](const html::Node& node) {
        return app.m_impl->styles.hover_may_change_style(node);
    };
    input_host.active_styles = [&app](const html::Node& node) {
        return app.m_impl->styles.active_may_change_style(node);
    };
    input_host.dispatch = [&app](Event& e) { app.m_impl->dispatch_to(nullptr, e); };
    input_host.set_ime = [window](bool enabled) { window->set_ime(enabled); };
    input_host.set_ime_rect = [window](float x, float y, float w, float h) {
        window->set_ime_rect(x, y, w, h);
    };
    input_host.clipboard_text = [window] { return window->clipboard_text(); };
    input_host.set_clipboard_text = [window](std::string_view text) {
        return window->set_clipboard_text(text);
    };
    input_host.mark_geometry = [&app] { app.m_impl->geometry_dirty = true; };
    input_host.pick_open = [&app](const FileDialogOptions& opt) { return app.host().pick_open(opt); };
    input_host.open_uri = [&app](std::string_view uri) { return app.host().open_uri(uri); };
    impl.loop_interaction = std::make_unique<input::Interaction>(std::move(input_host));
    impl.live_interaction = impl.loop_interaction.get();
    return true;
}

void Access::detach(App& app) {
    App::Impl& impl = *app.m_impl;
    if (impl.borrowed_window == nullptr) {
        return;
    }
    impl.live_interaction = nullptr;
    impl.loop_interaction.reset();
    impl.borrowed_window = nullptr;
    impl.borrowed_renderer = nullptr;
    impl.active_window = nullptr;
    impl.running = false;
    impl.record_submit = false;
}

namespace {

bool box_in_subtree(const layout::Box* box, const layout::Box* root) {
    for (const layout::Box* p = box; p != nullptr; p = p->parent) {
        if (p == root) {
            return true;
        }
    }
    return false;
}

void append_runs(const layout::Box* cursor, std::string& out, float& first_ascent) {
    if (cursor == nullptr) {
        return;
    }
    // A zero-chrome inline that joined the ancestor IFC does not keep geo.runs. The glyphs
    // live on that ancestor, stamped with this box (or a descendant) as style_box.
    if (cursor->geo.runs.empty() && cursor->inline_text_owned_by_parent) {
        const layout::Box* paint = cursor;
        while (paint->parent != nullptr && paint->inline_text_owned_by_parent) {
            paint = paint->parent;
        }
        for (const layout::TextRun& run : paint->geo.runs) {
            if (run.style_box == nullptr || !box_in_subtree(run.style_box, cursor)) {
                continue;
            }
            if (out.empty() && !run.text.empty()) {
                first_ascent = run.ascent;
            }
            out += run.text;
        }
        return;
    }
    for (const layout::TextRun& run : cursor->geo.runs) {
        if (out.empty() && !run.text.empty()) {
            first_ascent = run.ascent;
        }
        out += run.text;
    }
    for (const layout::Box* child : cursor->children) {
        if (child == nullptr || child->inline_text_owned_by_parent) {
            continue;
        }
        append_runs(child, out, first_ascent);
    }
}

void census_walk(html::Node* node, int parent, const layout::LayoutEngine& layouter,
                 std::vector<NodeCensus>& out) {
    if (node == nullptr) {
        return;
    }
    NodeCensus row;
    row.parent = parent;
    row.id = node->id;
    row.tag = node->tag;
    row.class_attr = node->class_name;
    const auto date = node->attrs.find("data-date");
    if (date != node->attrs.end()) {
        row.data_date = date->second;
    }
    row.text = node->text;
    row.value = node->value;
    if (layout::Box* box = layouter.principal_box(node)) {
        box->world_origin(row.x, row.y);
        row.w = box->geo.width;
        row.h = box->geo.height;
        row.laid = row.w > 0.f && row.h > 0.f;
        // A grid item's glyphs can sit on a descendant line box of its principal box.
        append_runs(box, row.run_text, row.run_ascent);
    }
    const int index = static_cast<int>(out.size());
    out.push_back(std::move(row));
    for (const auto& child : node->children) {
        census_walk(child.get(), index, layouter, out);
    }
}

}  // namespace

std::string Access::computed(const App& app, std::string_view id, std::string_view key) {
    if (id.empty() || key.empty()) {
        return {};
    }
    const html::Node* node = html::find(app.m_impl->document, id);
    if (node == nullptr) {
        return {};
    }
    // `Node::style(const char*)` caches the literal's address (html/literal_atom.cpp), and `key`
    // here is a caller string whose storage does not outlive the call: two keys that reuse the
    // address would read each other's value (`--w-accent` answered `--w-radius`). Intern instead.
    const std::string* value = node->style(Atom::intern(key));
    return value != nullptr ? *value : std::string{};
}

std::vector<NodeCensus> Access::census(const App& app) {
    std::vector<NodeCensus> out;
    html::Node* root = app.m_impl->document.root.get();
    if (root != nullptr) {
        census_walk(root, -1, app.m_impl->layouter, out);
    }
    return out;
}

void Access::advance_clock(App& app, double seconds) {
    App::Impl& impl = *app.m_impl;
    if (impl.borrowed_window == nullptr) {
        return;
    }
    const auto delta = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(seconds));
    if (!impl.clock_on) {
        impl.clock_on = true;
        impl.clock0 = std::chrono::steady_clock::now() - delta;
    } else {
        impl.clock0 -= delta;
    }
}

}  // namespace zui::frame
