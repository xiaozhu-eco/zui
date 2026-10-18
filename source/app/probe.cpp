#include <zui/app.h>

#include "app/frame_session.h"
#include "probe/probe.h"
#include "perf/perf_budget.h"
#include "css/engine/engine.h"
#include "css/transition/transition.h"
#include "html/document.h"
#include "input/interaction.h"
#include "layout/box.h"
#include "layout/engine.h"
#include "paint/display_list.h"
#include "trace/trace.h"

#include "app/impl.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void set_env_value(const char* key, const char* value) {
#if defined(_WIN32)
    _putenv_s(key, value == nullptr ? "" : value);
#else
    if (value == nullptr || value[0] == '\0') {
        unsetenv(key);
    } else {
        setenv(key, value, 1);
    }
#endif
}

}  // namespace

namespace zui::paint {
void take_item_build_diag(double& build_ms, std::uint64_t& build_n, double& copy_ms,
                          std::uint64_t& copy_n);
}

namespace zui::frame {

/// Paint record + GPU upload for an armed probe, inside the open accounting window and before
/// `end_frame`. `present()` is not called: the swapchain wait is not engine time. A carried
/// `frame_visual` is a click or hover that already flushed in this window; clearing the flag
/// before `flush_style` would otherwise skip that paint. Idle and a second identical write
/// carry nothing and flush nothing, so they do not walk or upload.
void Access::submit_recorded(App::Impl& impl, bool carried_visual) {
    backends::IRenderer* renderer =
        impl.borrowed_renderer != nullptr ? impl.borrowed_renderer : impl.loop_renderer.get();
    if (!impl.record_submit || renderer == nullptr || !impl.viewport.has_value()) {
        return;
    }
    const bool paint_now = carried_visual || impl.frame_visual || impl.motion.needs_visual() ||
                           impl.motion.repaint_pending() || impl.animation.needs_visual() ||
                           impl.animation.repaint_pending();
    if (!paint_now || impl.layouter.root() == nullptr) {
        return;
    }
    const float vw = impl.viewport->width;
    const float vh = impl.viewport->height;
    double discard_build_ms = 0;
    std::uint64_t discard_build_n = 0;
    double discard_copy_ms = 0;
    std::uint64_t discard_copy_n = 0;
    paint::take_item_build_diag(discard_build_ms, discard_build_n, discard_copy_ms, discard_copy_n);
    (void)discard_build_ms;
    (void)discard_build_n;
    (void)discard_copy_ms;
    (void)discard_copy_n;
    const auto paint_started = std::chrono::steady_clock::now();
    {
        zui::trace::Zone zone("paint", zui::trace::Category::App);
        impl.layouter.resolve_pending_paint_damage();
        const html::SelectionRange& sel = impl.document.selection;
        impl.loop_painter.set_resource_base(impl.document.source_path);
        impl.loop_painter.set_selection(sel);
        impl.loop_painter.paint(*impl.layouter.root(), impl.loop_list, vw, vh);
    }
    const double paint_ms = std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - paint_started)
                                .count();
    double item_build_ms = 0;
    std::uint64_t item_build_n = 0;
    double item_copy_ms = 0;
    std::uint64_t item_copy_n = 0;
    paint::take_item_build_diag(item_build_ms, item_build_n, item_copy_ms, item_copy_n);
    const auto begin_started = std::chrono::steady_clock::now();
    renderer->begin_frame();
    const double begin_ms = std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - begin_started)
                                .count();
    const auto submit_started = std::chrono::steady_clock::now();
    renderer->submit(impl.loop_list);
    const double submit_ms = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - submit_started)
                                 .count();
    ZUI_TRACE("[frame-gap] paint={:.1f} begin={:.1f} submit={:.1f} items={} ibuild={:.1f} n={}"
              " icopy={:.1f} cn={}",
              paint_ms, begin_ms, submit_ms, impl.loop_list.items().size(), item_build_ms,
              item_build_n, item_copy_ms, item_copy_n);
    impl.frame_visual = false;
}

std::uint64_t Access::document_paint_stamp(const App& app) {
    if (app.m_impl == nullptr) {
        return 0;
    }
    return html::damage_of(app.m_impl->document).paint_stamp();
}

std::uint64_t Access::child_paint_stamp(const App& app) {
    if (app.m_impl == nullptr || app.m_impl->children.empty() ||
        app.m_impl->children.back() == nullptr) {
        return 0;
    }
    const html::DamageTracker* tracker = app.m_impl->children.back()->document.damage.get();
    if (tracker == nullptr) {
        return 0;
    }
    return tracker->paint_stamp();
}

bool Access::prepare_interaction(App& app, int width, int height) {
    App::Impl& impl = *app.m_impl;
    if (!impl.loaded || impl.document.root == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    // The seam does real work (a media restyle and the first cascade + layout). `pump_frame` wraps
    // its frame in an accounting window; without one here the initial layout of a headless test
    // vanished from `perf::FrameWork`, and only the layout were counted (docs/PERF.md section 6.1).
    perf::begin_frame();
    struct PublishFrame {
        ~PublishFrame() { perf::end_frame(); }
    } publish_frame;
    auto run = [&]() -> bool {
        const bool carried_visual = impl.frame_visual;
        // Prefer the live window Interaction when run() already owns one.
        if (impl.loop_interaction) {
            impl.live_interaction = impl.loop_interaction.get();
            impl.viewport = layout::Viewport{static_cast<float>(width), static_cast<float>(height)};
            impl.restyle(App::Impl::kReasonMedia);
            impl.geometry_dirty = true;
            impl.flush_style();
            submit_recorded(impl, carried_visual);
            return true;
        }
        impl.viewport = layout::Viewport{static_cast<float>(width), static_cast<float>(height)};
        impl.restyle(App::Impl::kReasonMedia);
        impl.geometry_dirty = true;
        impl.flush_style();

    input::InteractionHost host;
    host.document = &impl.document;
    host.layout = &impl.layouter;
    host.metrics = &impl.metrics;
    host.scrollbar_thickness = impl.layouter.scrollbar_thickness();
    host.hovered = &impl.hovered;
    host.focused = &impl.focused;
    host.scroll_drag = &impl.scroll_drag;
    host.restyle = [&app] { app.m_impl->restyle(App::Impl::kReasonHover); };
    host.restyle_roots = [&app](const std::vector<html::Node*>& roots) {
        app.m_impl->restyle_many(App::Impl::kReasonHover, roots);
    };
    host.hover_styles = [&app](const html::Node& node) {
        return app.m_impl->styles.hover_may_change_style(node);
    };
    host.active_styles = [&app](const html::Node& node) {
        return app.m_impl->styles.active_may_change_style(node);
    };
    host.dispatch = [&app](Event& e) { app.m_impl->dispatch_to(nullptr, e); };
    host.mark_geometry = [&app] { app.m_impl->geometry_dirty = true; };
    host.pick_open = [&app](const FileDialogOptions& opt) { return app.host().pick_open(opt); };
    host.open_uri = [&app](std::string_view uri) { return app.host().open_uri(uri); };
    impl.test_interaction = std::make_unique<input::Interaction>(std::move(host));
    impl.live_interaction = impl.test_interaction.get();
    submit_recorded(impl, carried_visual);
    return true;
    };
    if (impl.record_submit) {
        // Parent of cascade, layout, paint, and gpu.submit. Present is not in this window.
        zui::trace::Zone frame_zone("frame", zui::trace::Category::App);
        return run();
    }
    return run();
}

void Access::sync_observation(App& app) {
    App::Impl& impl = *app.m_impl;
    perf::begin_frame();
    struct PublishFrame {
        ~PublishFrame() { perf::end_frame(); }
    } publish_frame;
    auto body = [&] {
        // A click/hover in this window already flushed and set `frame_visual`. Zeroing first
        // keeps a clean observation from painting, and the carried bit still records that edit.
        const bool carried_visual = impl.frame_visual;
        impl.frame_visual = false;
        impl.flush_style();
        if (impl.sync_list_windows()) {
            impl.flush_style();
        }
        impl.flush_scroll(impl.live_interaction);
        // A clean observation does not walk the world (PERF.md I1). A style or layout flush still
        // publishes placement, including a paint-only restyle whose boxes did not move.
        if (!impl.layouter.world_is_current() || impl.layouter.world_is_clipped()) {
            // `world_is_clipped` means a culled descendant was parked with no rectangle. A scroll
            // that only translated boxes an earlier walk already published leaves the flag clear,
            // so observation does not map the tree again. Paint and hit testing still honor
            // `Geometry::culled`. The census and `box_rect` read the published border boxes.
            impl.layouter.sync_world_all();
        }
        // Same seam as the paint frame: write back the single line of the model rows this frame kept
        // (docs/RFC_MODEL.md wave 1), after the placement above cleared their `geo.runs`.
        impl.refill_kept_rows();
        submit_recorded(impl, carried_visual);
    };
    if (impl.record_submit) {
        zui::trace::Zone frame_zone("frame", zui::trace::Category::App);
        body();
    } else {
        body();
    }
}

void Access::flush_deferred(App& app) {
    App::Impl& impl = *app.m_impl;
    // KD13: skip while a host modal pick is nested (same as drain_deferred).
    if (impl.picks_.modal()) {
        return;
    }
    if (impl.deferred.empty()) {
        return;
    }
    std::vector<App::Impl::DeferredCall> batch;
    batch.swap(impl.deferred);
    for (auto& call : batch) {
        if (call.fn) {
            call.fn();
        }
    }
}

bool Access::has_class(const App& app, std::string_view id, std::string_view token) {
    html::Node* node = html::find(app.m_impl->document, id);
    if (node == nullptr || token.empty()) {
        return false;
    }
    return html::has_class_token(node->class_name, token);
}

std::string Access::hit_chain(const App& app, float x, float y) {
    std::string chain;
    for (const layout::Box* box = app.m_impl->layouter.hit_test(x, y); box != nullptr;
         box = box->parent) {
        if (box->node == nullptr) {
            continue;
        }
        if (!chain.empty()) {
            chain += ' ';
        }
        chain += box->node->tag;
        if (!box->node->id.empty()) {
            chain += '#';
            chain += box->node->id;
        }
    }
    return chain;
}

std::string Access::measure(const App& app, std::string_view id, std::string_view field) {
    html::Node* node = html::find(app.m_impl->document, id);
    if (node == nullptr) {
        return {};
    }
    const bool geometry_field = field == "content_width" || field == "content_height" ||
                                field == "client_width" || field == "client_height" ||
                                field == "scroll_width" || field == "scroll_height";
    if (!geometry_field) {
        return {};
    }
    // I8: a read does not flush. Call sync_observation() when the geometry must be current.
    layout::Box* box = app.m_impl->layouter.principal_box(node);
    if (box == nullptr) {
        return {};
    }
    const layout::Geometry& geo = box->geo;
    if (field == "content_width") {
        return std::to_string(geo.content_width);
    }
    if (field == "content_height") {
        return std::to_string(geo.content_height);
    }
    if (field == "client_width") {
        return std::to_string(geo.content_width + geo.padding.horizontal());
    }
    if (field == "client_height") {
        return std::to_string(geo.content_height + geo.padding.vertical());
    }
    if (field == "scroll_width") {
        return std::to_string(geo.scroll_width);
    }
    return std::to_string(geo.scroll_height);
}

bool Access::box_center(const App& app, std::string_view id, float& out_x, float& out_y) {
    float w = 0.f;
    float h = 0.f;
    if (!box_rect(app, id, out_x, out_y, w, h)) {
        return false;
    }
    out_x += w * 0.5f;
    out_y += h * 0.5f;
    return true;
}

bool Access::box_rect(const App& app, std::string_view id, float& out_x, float& out_y,
                      float& out_w, float& out_h) {
    html::Node* node = html::find(app.m_impl->document, id);
    if (node == nullptr) {
        return false;
    }
    layout::Box* box = app.m_impl->layouter.principal_box(node);
    if (box == nullptr || box->geo.width <= 0.f || box->geo.height <= 0.f) {
        return false;
    }
    box->world_origin(out_x, out_y);
    out_w = box->geo.width;
    out_h = box->geo.height;
    return true;
}

bool Access::click_xy(App& app, float x, float y, int count) {
    App::Impl& impl = *app.m_impl;
    perf::begin_write();
    input::Interaction* interaction = impl.live_interaction;
    if (App::Impl::OwnedWindow* modal = impl.modal_child()) {
        interaction = modal->interaction.get();
    }
    if (interaction == nullptr) {
        return false;
    }
    if (count < 1) {
        count = 1;
    }
    impl.flush_style();
    // Hit testing reads paint_*/geo.world from the last world place; keep it current even when
    // flush_style skipped layout (style-only dirty).
    if (!impl.layouter.world_is_current()) {
        impl.layouter.sync_world();
    }
    Pointer mods;
    mods.x = x;
    mods.y = y;
    mods.button = "left";
    mods.buttons = 1;
    mods.count = count;
    interaction->pointer_button("mousePressed", x, y, "left", count, mods);
    mods.buttons = 0;
    interaction->pointer_button("mouseReleased", x, y, "left", 1, mods);
    // Settle only what the handler dirtied. Forcing a restyle here made every click look like a
    // whole-document change (PERF.md P6).
    impl.flush_style();
    return true;
}

bool Access::hover_xy(App& app, float x, float y) {
    App::Impl& impl = *app.m_impl;
    perf::begin_write();
    input::Interaction* interaction = impl.live_interaction;
    if (App::Impl::OwnedWindow* modal = impl.modal_child()) {
        interaction = modal->interaction.get();
    }
    if (interaction == nullptr) {
        return false;
    }
    impl.flush_style();
    if (!impl.layouter.world_is_current()) {
        impl.layouter.sync_world();
    }
    Pointer mods;
    mods.x = x;
    mods.y = y;
    interaction->pointer_move(x, y, mods);
    impl.flush_style();
    return true;
}

bool Access::activate_id(App& app, std::string_view id) {
    App::Impl& impl = *app.m_impl;
    if (impl.live_interaction == nullptr || id.empty()) {
        return false;
    }
    html::Node* node = html::find(impl.document, id);
    if (node == nullptr || node->disabled) {
        return false;
    }
    impl.live_interaction->click_target(node);
    impl.flush_style();
    return true;
}

bool Access::click_id(App& app, std::string_view id, int count) {
    if (App::Impl::OwnedWindow* modal = app.m_impl->modal_child()) {
        html::Node* node = html::find(modal->document, id);
        if (node == nullptr || !node->visible || node->disabled || modal->layouter == nullptr) {
            return false;
        }
        layout::Box* box = modal->layouter->principal_box(node);
        if (box == nullptr || box->geo.width <= 0.f || box->geo.height <= 0.f) {
            return false;
        }
        const float x = box->geo.world_x + box->geo.width * 0.5f;
        const float y = box->geo.world_y + box->geo.height * 0.5f;
        return click_xy(app, x, y, count);
    }
    html::Node* node = html::find(app.m_impl->document, id);
    if (node == nullptr || !node->visible || node->disabled) {
        return false;
    }
    sync_observation(app);
    float x = 0.f;
    float y = 0.f;
    if (!box_center(app, id, x, y)) {
        return false;
    }
    return click_xy(app, x, y, count);
}

bool Access::wheel_xy(App& app, float x, float y, float delta_y, float delta_x) {
    App::Impl& impl = *app.m_impl;
    perf::begin_write();
    if (impl.live_interaction == nullptr) {
        return false;
    }
    Wheel mods;
    mods.delta_x = delta_x;
    mods.delta_y = delta_y;
    (void)impl.live_interaction->wheel(x, y, delta_x, delta_y, /*delta_mode=*/0, mods);
    impl.flush_style();
    impl.flush_scroll(impl.live_interaction);
    return true;
}

bool Access::wheel_id(App& app, std::string_view id, float delta_y, float delta_x) {
    App::Impl& impl = *app.m_impl;
    perf::begin_write();
    if (impl.live_interaction == nullptr) {
        return false;
    }
    html::Node* node = html::find(impl.document, id);
    if (node == nullptr || !node->visible || node->disabled) {
        return false;
    }
    sync_observation(app);
    float x = 0.f;
    float y = 0.f;
    if (!box_center(app, id, x, y)) {
        return false;
    }
    return wheel_xy(app, x, y, delta_y, delta_x);
}

bool Access::key(App& app, std::string_view key_name, bool ctrl, bool shift, bool alt, bool meta,
                 bool up) {
    App::Impl& impl = *app.m_impl;
    perf::begin_write();
    input::Interaction* interaction = impl.live_interaction;
    if (App::Impl::OwnedWindow* modal = impl.modal_child()) {
        interaction = modal->interaction.get();
    }
    if (interaction == nullptr || key_name.empty()) {
        return false;
    }
    Key mods;
    mods.key = key_name;
    mods.ctrl = ctrl;
    mods.shift = shift;
    mods.alt = alt;
    mods.meta = meta;
    const char* type = up ? "keyUp" : "keyDown";
    (void)interaction->key(type, key_name, mods);
    impl.flush_style();
    return true;
}

void Access::advance_time(App& app, double seconds) {
    if (app.m_impl == nullptr || seconds == 0.0) {
        return;
    }
    // Pull clock0 backward so the next frame_t (= now - clock0) jumps forward by @p seconds.
    const auto delta = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(seconds));
    app.m_impl->clock0 -= delta;
}

bool Access::drop_paths(App& app, const std::vector<std::string>& paths, float x, float y) {
    App::Impl& impl = *app.m_impl;
    perf::begin_write();
    input::Interaction* interaction = impl.live_interaction;
    if (App::Impl::OwnedWindow* modal = impl.modal_child()) {
        interaction = modal->interaction.get();
    }
    if (interaction == nullptr || paths.empty()) {
        return false;
    }
    std::vector<std::string_view> views;
    views.reserve(paths.size());
    for (const std::string& path : paths) {
        views.push_back(path);
    }
    Drop payload;
    bind_drop_paths(payload, views.data(), views.size());
    const bool fired = interaction->file_drop(x, y, payload);
    impl.flush_style();
    return fired;
}

std::vector<std::string> Access::interactive_ids(const App& app) {
    static const char* kKinds[] = {"click",
                                   "dblclick",
                                   "input",
                                   "change",
                                   "keydown",
                                   "keyup",
                                   "beforeunload",
                                   "scroll",
                                   "wheel",
                                   "drop",
                                   "focus",
                                   "blur",
                                   "contextmenu",
                                   "auxclick",
                                   "pointermove",
                                   "mousemove",
                                   "pointerdown",
                                   "pointerup",
                                   "mouseenter",
                                   "mouseleave",
                                   "mouseover",
                                   "mouseout",
                                   "pointerenter",
                                   "pointerleave",
                                   "dragstart",
                                   "drag",
                                   "dragenter",
                                   "dragover",
                                   "dragleave",
                                   "dragend",
                                   "visibilitychange",
                                   "resize",
                                   "orientationchange"};
    std::vector<std::string> out;
    std::function<void(html::Node*)> walk = [&](html::Node* node) {
        if (node == nullptr) {
            return;
        }
        if (!node->id.empty()) {
            for (const char* kind : kKinds) {
                if (html::handler_attribute(node, kind) != nullptr) {
                    out.push_back(node->id);
                    break;
                }
            }
        }
        for (const auto& ch : node->children) {
            walk(ch.get());
        }
    };
    walk(app.m_impl->document.root.get());
    return out;
}

namespace {

struct BudgetDevice {
    std::unique_ptr<backends::IWindow> window;
    std::unique_ptr<backends::IRenderer> renderer;
    bool ok = false;
};

BudgetDevice& budget_device() {
    // One device for every armed app in the process. Creating it on the first call keeps that
    // cost out of the per-operation clock. A one-quad submit here also pays the first command
    // buffer and vertex-buffer map, which are not an operation.
    static BudgetDevice device = [] {
        BudgetDevice created;
        auto open = [&](backends::GfxApi api, const char* kind) {
            backends::WindowDesc desc;
            desc.title = "zui-budget-probe";
            desc.width = 1100;
            desc.height = 720;
            desc.hidden = true;
            desc.api = api;
            created.window = backends::create_window("sdl3");
            created.renderer = backends::create_renderer(kind);
            return created.window && created.window->create(desc) && created.renderer &&
                   created.renderer->create(*created.window);
        };
        if (open(backends::GfxApi::Vulkan, "") || open(backends::GfxApi::OpenGL, "gl")) {
            created.ok = true;
            paint::DisplayList warm;
            warm.fill_rect(paint::Rect{0.f, 0.f, 4.f, 4.f}, paint::Color{0, 0, 0, 255});
            created.renderer->begin_frame();
            created.renderer->submit(warm);
        }
        return created;
    }();
    return device;
}

}  // namespace

void Access::release_gpu_record() {
    BudgetDevice& device = budget_device();
    // Renderer first: its destructor waits on the device and unbinds the SVG runtime.
    device.renderer.reset();
    device.window.reset();
    device.ok = false;
}

bool Access::arm_gpu_record(App& app) {
    BudgetDevice& device = budget_device();
    if (!device.ok || !app.m_impl->loaded || app.m_impl->document.root == nullptr) {
        return false;
    }
    if (app.m_impl->borrowed_renderer == nullptr) {
        Session session;
        session.window = device.window.get();
        session.renderer = device.renderer.get();
        if (!attach(app, session)) {
            return false;
        }
    }
    app.m_impl->record_submit = true;
    return true;
}

Access::GpuRetainReport Access::gpu_retain_probe() {
    GpuRetainReport report;
    backends::WindowDesc desc;
    desc.title = "zui-gpu-retain";
    desc.width = 320;
    desc.height = 240;
    desc.hidden = true;
    desc.api = backends::GfxApi::Vulkan;
    std::unique_ptr<backends::IWindow> window = backends::create_window("sdl3");
    std::unique_ptr<backends::IRenderer> renderer = backends::create_renderer("");
    if (window == nullptr || !window->create(desc) || renderer == nullptr ||
        !renderer->create(*window)) {
        desc.api = backends::GfxApi::OpenGL;
        window = backends::create_window("sdl3");
        renderer = backends::create_renderer("gl");
        if (window == nullptr || !window->create(desc) || renderer == nullptr ||
            !renderer->create(*window)) {
            return report;
        }
    }
    report.available = true;
    const char* backend_name = renderer->name() != nullptr ? renderer->name() : "";
    std::snprintf(report.backend, sizeof(report.backend), "%s", backend_name);

    auto submit = [&](paint::DisplayList& list) {
        perf::begin_frame();
        renderer->begin_frame();
        renderer->submit(list);
        perf::end_frame();
        return perf::last_frame_work();
    };

    const paint::Color chrome{20, 30, 40, 255};
    const paint::Color ink{200, 10, 10, 255};
    paint::DisplayList list;
    list.fill_rect(paint::Rect{10.f, 10.f, 40.f, 20.f}, chrome);
    list.push_clip(paint::Rect{0.f, 80.f, 200.f, 100.f});
    list.fill_rect(paint::Rect{12.f, 90.f, 30.f, 16.f}, ink);
    list.pop_clip();
    report.upload_first = submit(list).gpu_uploads;
    report.upload_same = submit(list).gpu_uploads;

    paint::DisplayList scrolled;
    scrolled.fill_rect(paint::Rect{10.f, 10.f, 40.f, 20.f}, chrome);
    scrolled.push_clip(paint::Rect{0.f, 80.f, 200.f, 100.f});
    scrolled.fill_rect(paint::Rect{12.f, 70.f, 30.f, 16.f}, ink);
    scrolled.pop_clip();
    report.upload_scroll = submit(scrolled).gpu_uploads;

    paint::DisplayList effects;
    paint::FilterEffects multiply;
    multiply.blend = paint::MixBlendMode::Multiply;
    effects.begin_filter(paint::Rect{4.f, 4.f, 70.f, 30.f}, multiply);
    effects.fill_rect(paint::Rect{8.f, 8.f, 24.f, 16.f}, ink);
    effects.end_filter();
    effects.shadow_rect(paint::Rect{20.f, 40.f, 36.f, 18.f}, paint::Color{0, 0, 0, 180},
                        paint::CornerRadii{}, 8.f);
    report.filter_effects = submit(effects).gpu_effects;

    const std::filesystem::path page =
        std::filesystem::temp_directory_path() / "zui-gpu-retain-page.html";
    {
        std::ofstream out(page);
        out << "<!DOCTYPE html><html><head><style>"
               "html,body{margin:0}"
               "#bar{position:fixed;left:0;top:0;width:80px;height:16px;background:#123}"
               "#sc{position:absolute;left:0;top:24px;width:200px;height:100px;overflow:hidden}"
               "#in{width:40px;height:400px;background:#c11}"
               "</style></head><body><div id=\"bar\"></div>"
               "<div id=\"sc\"><div id=\"in\"></div></div></body></html>\n";
    }
    std::string saved_timeout;
    const bool had_timeout = std::getenv("ZUI_PRESENT_TIMEOUT_MS") != nullptr;
    if (had_timeout) {
        saved_timeout = std::getenv("ZUI_PRESENT_TIMEOUT_MS");
    }
    set_env_value("ZUI_PRESENT_TIMEOUT_MS", "250");
    {
        App app{0, nullptr};
        if (app.document().load(page.string())) {
            Session session;
            session.window = window.get();
            session.renderer = renderer.get();
            if (attach(app, session)) {
                const bool first = app.pump_frame();
                const perf::FrameWork cold = perf::last_frame_work();
                const bool second = app.pump_frame();
                report.page_same_uploads = perf::last_frame_work().gpu_uploads;
                const bool wrote = app.document().set("sc", "scroll_y", "30");
                const bool third = app.pump_frame();
                const perf::FrameWork moved = perf::last_frame_work();
                report.page_scroll_uploads = moved.gpu_uploads;
                report.page_boxes = moved.boxes_created;
                report.page_laid = moved.laid_out;
                report.page_visited = moved.cascade_visited;
                report.scroll_y = 0.f;
                const std::string raw = app.document().get("sc", "scroll_y");
                if (!raw.empty()) {
                    report.scroll_y = std::strtof(raw.c_str(), nullptr);
                }
                report.page_ok = first && second && wrote && third && cold.gpu_uploads > 0 &&
                                 report.scroll_y > 1.f;
                detach(app);
            }
        }
    }
    if (had_timeout) {
        set_env_value("ZUI_PRESENT_TIMEOUT_MS", saved_timeout.c_str());
    } else {
        set_env_value("ZUI_PRESENT_TIMEOUT_MS", "");
    }
    renderer->destroy();
    window->destroy();
    return report;
}

}  // namespace zui::frame
