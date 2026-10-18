#include <zui/app.h>

#include "perf/perf_budget.h"
#include "backends/host_media.h"
#include "backends/platform.h"
#include "backends/renderer.h"
#include "css/engine/engine.h"
#include "css/transition/transition.h"
#include "html/document.h"
#include "input/interaction.h"
#include "layout/engine.h"
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
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>


namespace zui {

void Host::set_app_id(std::string_view id) {
    impl_->session->app_id = std::string(id);
    impl_->session->app_id_explicit = true;
}

std::string Host::user_data_dir() const {
    impl_->session->ensure_platform();
    return impl_->session->platform->user_data_dir(impl_->session->resolved_app_id());
}

std::string Host::cache_dir() const {
    impl_->session->ensure_platform();
    return impl_->session->platform->cache_dir(impl_->session->resolved_app_id());
}

PickPaths Host::pick_open(const FileDialogOptions& opt) {
    impl_->session->ensure_platform();
    return impl_->session->with_host_modal([&] {
        const NativeWindowId owner =
            impl_->session->active_window != nullptr ? impl_->session->active_window->native_handle() : NativeWindowId{};
        return impl_->session->platform->pick_open(owner, opt);
    });
}

PickPath Host::pick_save(const FileDialogOptions& opt) {
    impl_->session->ensure_platform();
    return impl_->session->with_host_modal([&] {
        const NativeWindowId owner =
            impl_->session->active_window != nullptr ? impl_->session->active_window->native_handle() : NativeWindowId{};
        return impl_->session->platform->pick_save(owner, opt);
    });
}

bool Document::scroll_into_view(std::string_view id, ScrollAlign block) {
    perf::begin_write();
    App::Impl* session = impl_->session;
    App::Impl::OwnedWindow* page = impl_->page;
    html::Document& tree = page != nullptr ? page->document : session->document;
    layout::LayoutEngine* layouter = page != nullptr ? page->layouter.get() : &session->layouter;
    if (layouter == nullptr) {
        return false;
    }
    html::Node* target = html::find(tree, id);
    if (target == nullptr) {
        return false;
    }
    html::Node* scrolled = nullptr;
    if (!input::scroll_into_view(*layouter, *target, block, layouter->scrollbar_thickness(),
                                 &scrolled)) {
        return false;
    }
    layouter->invalidate_world();
    layouter->sync_world();
    input::Interaction* live = page != nullptr ? page->interaction.get() : session->live_interaction;
    if (scrolled != nullptr && live != nullptr) {
        live->note_scroll(scrolled);
    }
    return true;
}

bool Document::focus(std::string_view id) {
    perf::begin_write();
    App::Impl* session = impl_->session;
    App::Impl::OwnedWindow* page = impl_->page;
    html::Document& tree = page != nullptr ? page->document : session->document;
    html::Node* target = html::find(tree, id);
    if (target == nullptr || !input::is_focusable(target)) {
        return false;
    }
    input::Interaction* live = page != nullptr ? page->interaction.get() : session->live_interaction;
    if (live != nullptr) {
        return live->focus(target, /*keyboard_modality=*/true);
    }
    // A child page has no interaction until open_document finishes building one.
    if (page != nullptr) {
        return false;
    }
    // Outside run(): still move focus flags and fire handlers; IME waits for an active window.
    input::InteractionHost host;
    host.document = &session->document;
    host.layout = &session->layouter;
    host.metrics = &session->metrics;
    host.focused = &session->focused;
    host.restyle = [session] { session->restyle(App::Impl::kReasonHover); };
    host.restyle_roots = [session](const std::vector<html::Node*>& roots) {
        session->restyle_many(App::Impl::kReasonHover, roots);
    };
    host.hover_styles = [session](const html::Node& node) {
        return session->styles.hover_may_change_style(node);
    };
    host.active_styles = [session](const html::Node& node) {
        return session->styles.active_may_change_style(node);
    };
    host.dispatch = [session](Event& event) { session->dispatch_to(nullptr, event); };
    if (session->active_window != nullptr) {
        host.set_ime = [session](bool enabled) { session->active_window->set_ime(enabled); };
        host.set_ime_rect = [session](float x, float y, float w, float h) {
            session->active_window->set_ime_rect(x, y, w, h);
        };
    }
    input::Interaction interaction(std::move(host));
    return interaction.focus(target, /*keyboard_modality=*/true);
}

bool Window::set_title(std::string_view title) {
    if (title.empty()) {
        return false;
    }
    impl_->session->document.title.assign(title.begin(), title.end());
    if (impl_->session->loop_window) {
        impl_->session->loop_window->set_title(impl_->session->document.title);
    }
    return true;
}

bool Window::set_window_size(int w, int h) {
    if (w <= 0 || h <= 0) {
        return false;
    }
    impl_->session->pending_window_w = w;
    impl_->session->pending_window_h = h;
    impl_->session->window_size_explicit = true;
    if (impl_->session->loop_window) {
        impl_->session->loop_window->set_window_size(w, h);
    }
    return true;
}

bool Window::set_icon(const WindowIcon& icon) {
    if (!icon.valid()) {
        return false;
    }
    impl_->session->pending_icon = icon;
    if (backends::IWindow* window = impl_->session->live_window()) {
        return window->set_icon(*impl_->session->pending_icon);
    }
    return true;
}

bool Window::minimize() {
    backends::IWindow* window = impl_->session->live_window();
    return window != nullptr && window->minimize();
}

bool Window::set_maximized(bool on) {
    backends::IWindow* window = impl_->session->live_window();
    return window != nullptr && window->set_maximized(on);
}

bool Window::set_fullscreen(bool on) {
    backends::IWindow* window = impl_->session->live_window();
    return window != nullptr && window->set_fullscreen(on);
}

bool Window::set_topmost(bool on) {
    backends::IWindow* window = impl_->session->live_window();
    return window != nullptr && window->set_topmost(on);
}

bool Window::minimized() const {
    const backends::IWindow* window = impl_->session->live_window();
    return window != nullptr && window->minimized();
}

bool Window::visible() const {
    const backends::IWindow* window = impl_->session->live_window();
    return window != nullptr && window->visibility() == backends::HostVisibility::Visible;
}

bool Window::maximized() const {
    const backends::IWindow* window = impl_->session->live_window();
    return window != nullptr && window->maximized();
}

bool Window::fullscreen() const {
    const backends::IWindow* window = impl_->session->live_window();
    return window != nullptr && window->fullscreen();
}

bool Window::topmost() const {
    const backends::IWindow* window = impl_->session->live_window();
    return window != nullptr && window->topmost();
}

DisplayIdentity Window::display() const {
    const backends::IWindow* window = impl_->session->live_window();
    return window != nullptr ? window->display() : DisplayIdentity{};
}

bool Document::blur() {
    perf::begin_write();
    App::Impl* session = impl_->session;
    App::Impl::OwnedWindow* page = impl_->page;
    html::Node* focused = page != nullptr ? page->focused : session->focused;
    if (focused == nullptr) {
        return false;
    }
    input::Interaction* live = page != nullptr ? page->interaction.get() : session->live_interaction;
    if (live != nullptr) {
        return live->blur();
    }
    if (page != nullptr) {
        return false;
    }
    input::InteractionHost host;
    host.document = &session->document;
    host.layout = &session->layouter;
    host.metrics = &session->metrics;
    host.focused = &session->focused;
    host.restyle = [session] { session->restyle(App::Impl::kReasonHover); };
    host.restyle_roots = [session](const std::vector<html::Node*>& roots) {
        session->restyle_many(App::Impl::kReasonHover, roots);
    };
    host.hover_styles = [session](const html::Node& node) {
        return session->styles.hover_may_change_style(node);
    };
    host.active_styles = [session](const html::Node& node) {
        return session->styles.active_may_change_style(node);
    };
    host.dispatch = [session](Event& event) { session->dispatch_to(nullptr, event); };
    if (session->active_window != nullptr) {
        host.set_ime = [session](bool enabled) { session->active_window->set_ime(enabled); };
        host.set_ime_rect = [session](float x, float y, float w, float h) {
            session->active_window->set_ime_rect(x, y, w, h);
        };
    }
    input::Interaction interaction(std::move(host));
    return interaction.blur();
}

PickPath Host::pick_folder(const FileDialogOptions& opt) {
    impl_->session->ensure_platform();
    return impl_->session->with_host_modal([&] {
        const NativeWindowId owner =
            impl_->session->active_window != nullptr ? impl_->session->active_window->native_handle() : NativeWindowId{};
        return impl_->session->platform->pick_folder(owner, opt);
    });
}

void Host::pick_open_async(const FileDialogOptions& opt, PickOpenHandler handler) {
    if (!handler) {
        return;
    }
    impl_->session->ensure_platform();
    if (impl_->session->platform->picks_are_async()) {
        impl_->session->picks_.open = std::move(handler);
        impl_->session->picks_.open_opt = opt;
        const NativeWindowId owner =
            impl_->session->active_window != nullptr ? impl_->session->active_window->native_handle() : NativeWindowId{};
        impl_->session->platform->begin_pick_open(owner, opt);
        return;
    }
    handler(pick_open(opt));
}

void Host::pick_save_async(const FileDialogOptions& opt, PickPathHandler handler) {
    if (!handler) {
        return;
    }
    impl_->session->ensure_platform();
    if (impl_->session->platform->picks_are_async()) {
        impl_->session->picks_.save = std::move(handler);
        impl_->session->picks_.save_opt = opt;
        const NativeWindowId owner =
            impl_->session->active_window != nullptr ? impl_->session->active_window->native_handle() : NativeWindowId{};
        impl_->session->platform->begin_pick_save(owner, opt);
        return;
    }
    handler(pick_save(opt));
}

void Host::pick_folder_async(const FileDialogOptions& opt, PickPathHandler handler) {
    if (!handler) {
        return;
    }
    impl_->session->ensure_platform();
    if (impl_->session->platform->picks_are_async()) {
        impl_->session->picks_.folder = std::move(handler);
        impl_->session->picks_.folder_opt = opt;
        const NativeWindowId owner =
            impl_->session->active_window != nullptr ? impl_->session->active_window->native_handle() : NativeWindowId{};
        impl_->session->platform->begin_pick_folder(owner, opt);
        return;
    }
    handler(pick_folder(opt));
}

PermissionState Host::permission_state(Permission permission) const {
    impl_->session->ensure_platform();
    return impl_->session->permissions.state(impl_->session->platform.get(), permission_name(permission));
}

void Host::request_permission(Permission permission, PermissionHandler handler) {
    if (!handler) {
        return;
    }
    impl_->session->ensure_platform();
    impl_->session->permissions.request(impl_->session->platform.get(), permission_name(permission),
                                        std::move(handler));
}

HostServices Host::services() const { return HostServices(HostServices::LiveTag{}); }

ClipboardText Host::clipboard_text() {
    if (impl_->session->active_window == nullptr) {
        return {};
    }
    return impl_->session->active_window->clipboard_text();
}

bool Host::set_clipboard_text(std::string_view text) {
    if (impl_->session->active_window == nullptr) {
        return false;
    }
    return impl_->session->active_window->set_clipboard_text(text);
}

ClipboardImage Host::clipboard_image() {
    if (impl_->session->active_window == nullptr) {
        return {};
    }
    return impl_->session->active_window->clipboard_image();
}

bool Host::set_clipboard_image(const ClipboardImage& image) {
    if (impl_->session->active_window == nullptr) {
        return false;
    }
    if (image.width <= 0 || image.height <= 0) {
        return false;
    }
    const std::size_t expect = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4;
    if (image.rgba.size() != expect) {
        return false;
    }
    return impl_->session->active_window->set_clipboard_image(image);
}

bool Host::open_uri(std::string_view uri) {
    impl_->session->ensure_platform();
    // ShellExecuteW returns immediately; no nested OS pump → no KD13 wrap.
    return impl_->session->platform->open_uri(uri);
}

}  // namespace zui
