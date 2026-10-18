#include "trace/trace.h"

#include "log/log.h"

#if defined(ZUI_TRACE_PERFETTO)

#include "trace/trace_categories.h"

#include <perfetto.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// The category registry's storage. Exactly one translation unit may reserve it, and every binary
// that wants tracing links this one.
PERFETTO_TRACK_EVENT_STATIC_STORAGE();

namespace zui {
namespace trace {
namespace {

/// The session as a whole. Reached through `state()`, so nothing here runs before the first zone:
/// a program that never asks for a trace never starts the SDK's service thread.
struct State {
    bool env_checked = false;
    bool initialized = false;
    bool initialized_ok = false;
    bool requested = false;
    bool running = false;
    std::string path;
    std::unique_ptr<perfetto::TracingSession> session;
};

State& state_impl() {
    static State s;
    return s;
}

void stop_and_write();

State& state() {
    static State& s = state_impl();
    return s;
}

std::uint32_t buffer_kb_from_env() {
    if (const char* v = std::getenv("ZUI_TRACE_BUFFER_KB")) {
        const long n = std::strtol(v, nullptr, 10);
        if (n > 0) {
            return static_cast<std::uint32_t>(n);
        }
    }
    // 64 MB: a studio session with app+engine categories fills ~4 MB in about a minute.
    // When the ring wraps without refreshing interned strings, ui.perfetto.dev reports
    // "Data losses" and the flamechart is empty (incremental_sequences_dropped).
    return 65536;
}

bool initialize_sdk() {
    State& s = state();
    if (s.initialized) {
        return s.initialized_ok;
    }
    s.initialized = true;
    perfetto::TracingInitArgs args;
    // In-process only: nothing outside this process joins the timeline.
    args.backends = perfetto::kInProcessBackend;
    perfetto::Tracing::Initialize(args);
    perfetto::TrackEvent::Register();
    s.initialized_ok = true;
    return true;
}

void start_session(std::string path) {
    State& s = state();
    if (s.requested || path.empty()) {
        return;
    }
    s.requested = true;
    s.path = std::move(path);
    if (!initialize_sdk()) {
        return;
    }

    perfetto::TraceConfig cfg;
    auto* buffer = cfg.add_buffers();
    buffer->set_size_kb(buffer_kb_from_env());
    // Keep the newest window of a long click session. clear_period_ms below re-emits interned
    // event names so a wrap does not make every later TrackEvent undecodable.
    buffer->set_fill_policy(perfetto::protos::gen::TraceConfig::BufferConfig::RING_BUFFER);
    cfg.mutable_incremental_state_config()->set_clear_period_ms(1000);
    auto* data_source = cfg.add_data_sources()->mutable_config();
    data_source->set_name("track_event");
    perfetto::protos::gen::TrackEventConfig track_event_cfg;
    // `ZUI_TRACE_CATEGORIES=app,engine` narrows the recording to the categories named. This is the
    // SDK's own mechanism, and it replaces the duration threshold the hand-written writer needed
    // to keep the per-box categories from flooding a trace.
    //
    // The `disabled: "*"` entry is required, not decoration: a category that matches neither list
    // falls back to "enabled" in the C++ API, so an enabled list alone would change nothing. An
    // exact entry in `enabled` is matched before `disabled` (TrackEventConfig's documented order),
    // which is why the named categories survive it.
    track_event_cfg.add_disabled_categories("*");
    if (const char* only = std::getenv("ZUI_TRACE_CATEGORIES")) {
        if (*only != '\0') {
            std::string list(only);
            for (std::size_t begin = 0; begin <= list.size();) {
                const std::size_t comma = list.find(',', begin);
                const std::size_t end = comma == std::string::npos ? list.size() : comma;
                if (end > begin) {
                    track_event_cfg.add_enabled_categories(list.substr(begin, end - begin));
                }
                if (comma == std::string::npos) {
                    break;
                }
                begin = comma + 1;
            }
        }
    } else {
        track_event_cfg.add_enabled_categories("*");
    }
    data_source->set_track_event_config_raw(track_event_cfg.SerializeAsString());

    s.session = perfetto::Tracing::NewTrace();
    if (s.session == nullptr) {
        ZUI_ERROR("[trace] could not create a tracing session");
        return;
    }
    s.session->Setup(cfg);
    s.session->StartBlocking();
    s.running = true;

    perfetto::ProcessTrack process_track = perfetto::ProcessTrack::Current();
    perfetto::protos::gen::TrackDescriptor desc = process_track.Serialize();
    desc.mutable_process()->set_process_name("zui");
    perfetto::TrackEvent::SetTrackDescriptor(process_track, desc);
}

/// Reads the environment once. `App::run` never calls `begin`, so this is what makes
/// `ZUI_TRACE=<path> zui_studio.exe` work without the driver knowing the writer exists.
void maybe_start_from_env() {
    State& s = state();
    if (s.env_checked) {
        return;
    }
    s.env_checked = true;
    if (const char* path = std::getenv("ZUI_TRACE")) {
        if (*path != '\0') {
            start_session(path);
        }
    }
}

void stop_and_write() {
    State& s = state();
    if (!s.running || s.session == nullptr) {
        return;
    }
    s.running = false;
    // Flush closes the open slices, then the service thread hands back what it buffered.
    // This has to run while the DLL is still fully alive. `atexit` and the function-local
    // destructor both run from DLL detach on Windows, and `StopBlocking` there faults.
    perfetto::TrackEvent::Flush();
    s.session->StopBlocking();
    std::vector<char> data = s.session->ReadTraceBlocking();
    s.session.reset();
    if (data.empty()) {
        ZUI_ERROR("[trace] {}: nothing was recorded", s.path);
        return;
    }
    std::FILE* file = std::fopen(s.path.c_str(), "wb");
    if (file == nullptr) {
        ZUI_ERROR("[trace] cannot write {}", s.path);
        return;
    }
    const std::size_t wrote = std::fwrite(data.data(), 1, data.size(), file);
    std::fclose(file);
    if (wrote != data.size()) {
        ZUI_ERROR("[trace] {}: short write", s.path);
        return;
    }
    ZUI_INFO("[trace] {}: {:.2f} MB", s.path,
             static_cast<double>(data.size()) / (1024.0 * 1024.0));
}

}  // namespace

bool enabled() {
    maybe_start_from_env();
    return state().requested;
}

bool active() {
    maybe_start_from_env();
    return state().running;
}

void begin(std::string_view path) {
    State& s = state();
    s.env_checked = true;
    start_session(std::string(path));
}

void flush() { stop_and_write(); }

void counter(const char* name, double value) {
    maybe_start_from_env();
    if (!state().running) {
        return;
    }
    TRACE_COUNTER("engine", perfetto::StaticString{name}, value);
}

Zone::Zone(const char* name, Category category, const BoxRef* box)
    : name_(name),
      category_(category),
      box_(box) {
    maybe_start_from_env();
    if (!state().running) {
        return;
    }
    open_ = true;
    // The SDK resolves a category at compile time, so the four cannot fold into one runtime call.
    // This switch and the macro below are what keep `TRACE_EVENT_BEGIN`'s category a constant.
    // Box strings are `DynamicString` because they belong to the node, not to the binary.
#define ZUI_TRACE_BEGIN(category_literal)                                                        \
    do {                                                                                         \
        if (box_ == nullptr) {                                                                   \
            TRACE_EVENT_BEGIN(category_literal, perfetto::StaticString{name_});                  \
        } else {                                                                                 \
            TRACE_EVENT_BEGIN(category_literal, perfetto::StaticString{name_}, "tag",            \
                              perfetto::DynamicString{box_->tag.data(), box_->tag.size()}, "id", \
                              perfetto::DynamicString{box_->id.data(), box_->id.size()},         \
                              "class",                                                           \
                              perfetto::DynamicString{box_->class_name.data(),                   \
                                                      box_->class_name.size()},                  \
                              "kind",                                                            \
                              perfetto::DynamicString{box_->kind.data(), box_->kind.size()});    \
        }                                                                                        \
    } while (false)
    switch (category_) {
        case Category::App:
            ZUI_TRACE_BEGIN("app");
            break;
        case Category::Engine:
            ZUI_TRACE_BEGIN("engine");
            break;
        case Category::LayoutPhase:
            ZUI_TRACE_BEGIN("layout.phase");
            break;
        case Category::LayoutAlgo:
            ZUI_TRACE_BEGIN("layout.algo");
            break;
        case Category::LayoutInner:
            ZUI_TRACE_BEGIN("layout.inner");
            break;
        case Category::Parse:
            ZUI_TRACE_BEGIN("parse");
            break;
        case Category::Text:
            ZUI_TRACE_BEGIN("text");
            break;
        case Category::Image:
            ZUI_TRACE_BEGIN("image");
            break;
        case Category::Input:
            ZUI_TRACE_BEGIN("input");
            break;
        case Category::Gpu:
            ZUI_TRACE_BEGIN("gpu");
            break;
    }
#undef ZUI_TRACE_BEGIN
}

Zone::~Zone() { close(); }

void Zone::close() {
    if (!open_) {
        return;
    }
    open_ = false;
    switch (category_) {
        case Category::App:
            TRACE_EVENT_END("app");
            break;
        case Category::Engine:
            TRACE_EVENT_END("engine");
            break;
        case Category::LayoutPhase:
            TRACE_EVENT_END("layout.phase");
            break;
        case Category::LayoutAlgo:
            TRACE_EVENT_END("layout.algo");
            break;
        case Category::LayoutInner:
            TRACE_EVENT_END("layout.inner");
            break;
        case Category::Parse:
            TRACE_EVENT_END("parse");
            break;
        case Category::Text:
            TRACE_EVENT_END("text");
            break;
        case Category::Image:
            TRACE_EVENT_END("image");
            break;
        case Category::Input:
            TRACE_EVENT_END("input");
            break;
        case Category::Gpu:
            TRACE_EVENT_END("gpu");
            break;
    }
}

}  // namespace trace
}  // namespace zui

// Called while the process is still in `main` (gtest TearDown, `App::run`, `zui_dump`).
// DLL detach is too late: the SDK's service thread is already gone.
// Hidden visibility on the shared library would drop this symbol. Windows uses
// dllexport; ELF uses default visibility so GetProcAddress / dlsym can see it.
#if defined(_WIN32)
extern "C" __declspec(dllexport) void zui_trace_shutdown() {
#else
extern "C" __attribute__((visibility("default"))) void zui_trace_shutdown() {
#endif
    zui::trace::flush();
}

#endif  // ZUI_TRACE_PERFETTO
