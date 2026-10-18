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
#include "app/impl.h"

namespace zui {

App::App(int argc, char** argv) : m_impl(std::make_unique<Impl>()) {
    {
        auto state = std::unique_ptr<Document::Impl>(new Document::Impl);
        state->session = m_impl.get();
        state->page = nullptr;
        m_impl->root_document.reset(new Document(std::move(state)));
    }
    {
        auto state = std::unique_ptr<Host::Impl>(new Host::Impl);
        state->session = m_impl.get();
        m_impl->host_facade.reset(new Host(std::move(state)));
    }
    {
        auto state = std::unique_ptr<Window::Impl>(new Window::Impl);
        state->session = m_impl.get();
        m_impl->window_facade.reset(new Window(std::move(state)));
    }
    if (argc > 0 && argv != nullptr && argv[0] != nullptr) {
        m_impl->argv0 = argv[0];
    }
    m_impl->register_mobile_pick_sink();
    m_impl->register_mobile_permission_sink();
}

Document::Document(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Document::~Document() = default;
Host::Host(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Host::~Host() = default;
Window::Window(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Window::~Window() = default;
App::Impl::~Impl() = default;

Document& App::document() noexcept { return *m_impl->root_document; }
const Document& App::document() const noexcept { return *m_impl->root_document; }
Host& App::host() noexcept { return *m_impl->host_facade; }
const Host& App::host() const noexcept { return *m_impl->host_facade; }
Window& App::window() noexcept { return *m_impl->window_facade; }
const Window& App::window() const noexcept { return *m_impl->window_facade; }

App::~App() {
    // Join background tasks first so a worker cannot outlive the document. Completions are dropped.
    m_impl->tasks_.shutdown();
    // Cancel pending timers/deferrals so captured app state cannot outlive App.
    m_impl->timer_q_.clear();
    m_impl->deferred.clear();
    m_impl->picks_.clear();
    m_impl->permissions.clear();
    m_impl->unregister_mobile_pick_sink();
    m_impl->unregister_mobile_permission_sink();
}

App::Impl& App::impl() const noexcept {
    return *m_impl;
}

bool Document::load(std::string_view path) {
    if (impl_->page != nullptr) {
        return false;
    }
    // A load is one accounting window: the parse work units (docs/PERF.md section 6.2
    // `html_nodes` / `css_sheets`) are counted inside it and published when it closes, so a load
    // can be read without a trace. `begin_frame` / `end_frame` is the recorder's only window; a
    // load is one of them (the headless `prepare_interaction` seam does the same for its flush).
    perf::begin_frame();
    struct PublishLoad {
        ~PublishLoad() { perf::end_frame(); }
    } publish_load;
    std::string error;
    html::Document doc;
    {
        zui::trace::Zone zone("load.html", zui::trace::Category::App);
        if (!html::load_file(path, doc, error)) {
            ZUI_ERROR("zui::App::load: {}", error);
            return false;
        }
    }
    std::string css_error;
    {
        zui::trace::Zone zone("load.css", zui::trace::Category::App);
        if (!impl_->session->styles.load_sheets(doc, css_error)) {
            ZUI_ERROR("zui::App::load: {}", css_error);
            return false;
        }
    }
    // Drop transition records while the outgoing nodes are still alive. `clear` walks those
    // nodes to stamp paint damage; doing it after the move reads freed memory.
    impl_->session->motion.clear();
    impl_->session->animation.clear();
    impl_->session->document = std::move(doc);
    impl_->session->loaded = true;
    impl_->session->layouter.reset();
    if (!impl_->session->app_id_explicit) {
        impl_->session->app_id.clear();
        impl_->session->apply_meta_app_id(impl_->session->document.root.get());
    }
    if (!impl_->session->window_size_explicit) {
        impl_->session->pending_window_w = 1100;
        impl_->session->pending_window_h = 720;
        impl_->session->apply_meta_window(impl_->session->document.root.get());
    }
    impl_->session->restyle();
    ZUI_INFO("[load] {} title=\"{}\" sheets={}", std::string(path), impl_->session->document.title,
             impl_->session->document.stylesheets.size());
    return true;
}

bool Document::load_fragment(std::string_view path) { return load_fragment(path, {}); }

void Document::bind(std::string_view handler_name, Handler handler) {
    if (handler_name.empty() || !handler) {
        return;
    }
    auto& table = impl_->page != nullptr ? impl_->page->handlers : impl_->session->handlers;
    table[std::string(handler_name)] = std::move(handler);
}

void Document::unbind(std::string_view handler_name) {
    auto& table = impl_->page != nullptr ? impl_->page->handlers : impl_->session->handlers;
    table.erase(std::string(handler_name));
}

int App::run() {
    if (!m_impl->loaded || m_impl->document.root == nullptr) {
        ZUI_ERROR("zui::App::run: no document loaded");
        return 1;
    }
    if (m_impl->borrowed_window != nullptr) {
        ZUI_ERROR("zui::App::run: a borrowed frame session is attached");
        return 1;
    }
    const char* wenv = std::getenv("ZUI_WINDOW");
    const char* renv = std::getenv("ZUI_RENDERER");
    const char* penv = std::getenv("ZUI_PLATFORM");
    const std::string wkind = wenv ? wenv : "sdl3";
    const std::string rkind = renv ? renv : "";
    // Empty kind lets create_platform pick the OS default (win32/linux/macos).
    const std::string pkind = (penv && penv[0] != '\0') ? penv : "";
    m_impl->ensure_platform();
    m_impl->maybe_log_host(pkind.empty() ? std::string("native") : pkind);
    m_impl->loop_window = backends::create_window(wkind);
    m_impl->loop_renderer = backends::create_renderer(rkind);
    if (!m_impl->loop_window || !m_impl->loop_renderer) {
        ZUI_ERROR("zui::App::run: failed to create backends");
        return 1;
    }
    backends::WindowDesc desc;
    desc.title = m_impl->document.title.empty() ? "zui" : m_impl->document.title;
    desc.width = m_impl->pending_window_w > 0 ? m_impl->pending_window_w : 1100;
    desc.height = m_impl->pending_window_h > 0 ? m_impl->pending_window_h : 720;
    desc.api = backends::gfx_api_from_renderer(rkind);
    if (m_impl->loop_renderer->name() && std::string(m_impl->loop_renderer->name()) == "gl") {
        desc.api = backends::GfxApi::OpenGL;
    } else if (m_impl->loop_renderer->name() &&
               std::string(m_impl->loop_renderer->name()) == "gles") {
        desc.api = backends::GfxApi::OpenGLES;
    } else if (m_impl->loop_renderer->name() &&
               std::string(m_impl->loop_renderer->name()) == "vk") {
        desc.api = backends::GfxApi::Vulkan;
    }
    if (!m_impl->loop_window->create(desc)) {
        ZUI_ERROR("zui::App::run: window create failed");
        return 1;
    }
    if (!m_impl->loop_renderer->create(*m_impl->loop_window)) {
        ZUI_WARN("zui::App::run: {} renderer failed, trying OpenGL",
                 m_impl->loop_renderer->name());
        m_impl->loop_renderer = backends::create_renderer("gl");
        if (!m_impl->loop_renderer) {
            ZUI_ERROR("zui::App::run: OpenGL fallback missing");
            return 1;
        }
        m_impl->loop_window->destroy();
        desc.api = backends::GfxApi::OpenGL;
        if (!m_impl->loop_window->create(desc) ||
            !m_impl->loop_renderer->create(*m_impl->loop_window)) {
            ZUI_ERROR("zui::App::run: OpenGL fallback failed");
            return 1;
        }
    }
    ZUI_INFO("zui: window={} renderer={}", m_impl->loop_window->name(),
             m_impl->loop_renderer->name());

    m_impl->active_window = m_impl->loop_window.get();

    m_impl->loop_painter = paint::Painter{};
    // Retained scroll listings (LAYOUT.md section 9 step D) are on by default. A listing is only
    // replayed while the content under it is unchanged - `note_paint_change` retires it otherwise -
    // so ZUI_RETAINED_PAINT exists to A/B a suspected replay bug, not to turn the fast path off.
    if (const char* on = std::getenv("ZUI_RETAINED_PAINT")) {
        const bool enabled = !(on[0] == '0' && on[1] == '\0');
        m_impl->loop_painter.set_retained(enabled);
        ZUI_INFO("[paint] retained listings {} (ZUI_RETAINED_PAINT)", enabled ? "on" : "off");
    }
    m_impl->loop_list = paint::DisplayList{};
    m_impl->loop_window->set_ime(false);
    if (m_impl->pending_icon.has_value()) {
        m_impl->loop_window->set_icon(*m_impl->pending_icon);
    }
    m_impl->running = true;
    m_impl->clock_on = false;

    input::InteractionHost input_host;
    input_host.document = &m_impl->document;
    input_host.layout = &m_impl->layouter;
    input_host.metrics = &m_impl->metrics;
    input_host.scrollbar_thickness = m_impl->layouter.scrollbar_thickness();
    input_host.hovered = &m_impl->hovered;
    input_host.focused = &m_impl->focused;
    input_host.scroll_drag = &m_impl->scroll_drag;
    input_host.restyle = [this] { m_impl->restyle(Impl::kReasonHover); };
    input_host.restyle_roots = [this](const std::vector<html::Node*>& roots) {
        m_impl->restyle_many(Impl::kReasonHover, roots);
    };
    input_host.hover_styles = [this](const html::Node& node) {
        return m_impl->styles.hover_may_change_style(node);
    };
    input_host.active_styles = [this](const html::Node& node) {
        return m_impl->styles.active_may_change_style(node);
    };
    input_host.dispatch = [this](Event& e) { m_impl->dispatch_to(nullptr, e); };
    input_host.set_ime = [this](bool enabled) {
        if (m_impl->loop_window) {
            m_impl->loop_window->set_ime(enabled);
        }
    };
    input_host.set_ime_rect = [this](float x, float y, float w, float h) {
        if (m_impl->loop_window) {
            m_impl->loop_window->set_ime_rect(x, y, w, h);
        }
    };
    input_host.clipboard_text = [this] {
        return m_impl->loop_window ? m_impl->loop_window->clipboard_text() : ClipboardText{};
    };
    input_host.set_clipboard_text = [this](std::string_view text) {
        return m_impl->loop_window && m_impl->loop_window->set_clipboard_text(text);
    };
    input_host.mark_geometry = [this] { m_impl->geometry_dirty = true; };
    input_host.pick_open = [this](const FileDialogOptions& opt) { return host().pick_open(opt); };
    input_host.open_uri = [this](std::string_view uri) { return host().open_uri(uri); };
    m_impl->loop_interaction = std::make_unique<input::Interaction>(std::move(input_host));
    m_impl->live_interaction = m_impl->loop_interaction.get();

    while (m_impl->running && pump_frame()) {
    }

    m_impl->live_interaction = nullptr;
    m_impl->loop_interaction.reset();
    m_impl->loop_renderer->destroy();
    m_impl->loop_window->destroy();
    m_impl->active_window = nullptr;
    m_impl->loop_renderer.reset();
    m_impl->loop_window.reset();
    // Flush before static destructors: writing from FlushAtExit alone can race the SDK's
    // service thread during process teardown, and a hung destroy would skip it entirely.
    zui::trace::flush();
    return 0;
}

void Document::defer(std::function<void()> fn) {
    if (!fn) {
        return;
    }
    impl_->session->deferred.push_back(App::Impl::DeferredCall{impl_->page, std::move(fn)});
}

TimerId Document::set_timeout(std::function<void()> fn, int delay_ms) {
    return impl_->session->arm_timer(std::move(fn), delay_ms, false, impl_->page);
}

TimerId Document::set_interval(std::function<void()> fn, int interval_ms) {
    return impl_->session->arm_timer(std::move(fn), interval_ms, true, impl_->page);
}

void Document::clear_timer(TimerId id) {
    impl_->session->timer_q_.cancel(id, impl_->page);
}

void App::quit() { m_impl->running = false; }

}  // namespace zui
