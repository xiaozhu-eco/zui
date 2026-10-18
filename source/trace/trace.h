#ifndef ZUI_TRACE_TRACE_H
#define ZUI_TRACE_TRACE_H

// Instrumentation for the Perfetto Tracing SDK (third_party/perfetto, `ZUI_WITH_PERFETTO`).
// C-tier observation only (docs/LAYOUT.md section 16): nothing in the pipeline reads what this
// records, and a run that never asks for a trace never touches the SDK - `begin` is the only
// place that initializes it, and it is reached from the environment or a command line.
//
// Two builds of this header exist:
//
//   ZUI_TRACE_PERFETTO defined   passed to the SDK by `source/trace/trace.cpp`
//   not defined                  every call below is an inline no-op
//
// The second build is what the standalone layout test binaries use, which is why linking the SDK
// is a per-target decision (CMakeLists.txt wires it to `zui` and `zui_dump`) instead of something
// every executable in the tree pays for.
//
// Reading a trace: `tools/trace/README.md` and `docs/PERF.md` §5.

#include <string>
#include <string_view>

namespace zui {
namespace trace {

/// The categories a trace can enable. The strings live in `trace_categories.h`; the call sites
/// name this enum, so a typo is a compile error instead of a silently empty category.
enum class Category {
    App,
    Engine,
    LayoutPhase,
    LayoutAlgo,
    LayoutInner,
    Parse,
    Text,
    Image,
    Input,
    Gpu
};

/// Names a box for the trace. The views point into strings the node already holds, so building one
/// costs nothing and nothing is copied unless the zone is actually recorded.
struct BoxRef {
    std::string_view tag;
    std::string_view id;
    std::string_view class_name;
    std::string_view kind;
};

/// True when a trace was requested (`ZUI_TRACE`, or `zui_dump --trace`). The first call starts
/// the session from the environment if `ZUI_TRACE` is set, so an accounting window that opens
/// before the first `Zone` still sees a live session.
bool enabled();
/// True while a session is recording. Same first-call start as `enabled()`.
bool active();
/// Opens a session writing to `path`. Initializes the SDK on first use.
void begin(std::string_view path);
/// Stops the session, reads the buffer and writes the file. Also runs at exit.
void flush();
/// A point-in-time value on the same timeline as the zones, written as a counter track.
void counter(const char* name, double value);

/// A duration zone. Names must be static strings - every call site passes a literal.
class Zone {
public:
    Zone(const char* name, Category category, const BoxRef* box = nullptr);
    ~Zone();
    Zone(const Zone&) = delete;
    Zone& operator=(const Zone&) = delete;
    /// Closes the zone early, for call sites that keep the object alive past the work it measures.
    /// A closed zone's destructor does nothing.
    void close();

private:
    const char* name_ = "";
    Category category_ = Category::App;
    const BoxRef* box_ = nullptr;
    bool open_ = false;
};

}  // namespace trace
}  // namespace zui

#if !defined(ZUI_TRACE_PERFETTO)

namespace zui {
namespace trace {
namespace detail {

#if defined(ZUI_WITH_SPDLOG)
/// One line, once, when `ZUI_TRACE` names a file and this binary has no SDK.
/// Defined in `log.cpp`: targets that include this header do not compile `trace.cpp`.
void warn_without_sdk();
#endif

}  // namespace detail

inline bool enabled() { return false; }
inline bool active() { return false; }
inline void flush() {}
inline void counter(const char*, double) {}

inline void begin(std::string_view path) {
#if defined(ZUI_WITH_SPDLOG)
    if (!path.empty()) {
        detail::warn_without_sdk();
    }
#else
    (void)path;
#endif
}

inline Zone::Zone(const char*, Category, const BoxRef*) {}
inline Zone::~Zone() = default;
inline void Zone::close() {}

}  // namespace trace
}  // namespace zui

#endif  // !ZUI_TRACE_PERFETTO

#endif  // ZUI_TRACE_TRACE_H
