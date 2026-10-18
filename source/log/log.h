// Internal debug log. Not an application API: this header lives under source/
// and is not installed. Application code includes include/zui/app.h only.
//
// Debug and RelWithDebInfo set ZUI_WITH_SPDLOG. Release and MinSizeRel do not
// compile spdlog, and the macros below become empty statements.
//
// Isolation from an application that also uses spdlog or fmt:
//   * The vendored sources are compiled into namespaces zui_spdlog and zui_fmt,
//     so the symbols are not spdlog::* or fmt::*.
//   * The logger is a private instance. It is not registered, and it is not
//     spdlog's default logger, so the two sides do not share a registry.
//   * Nothing here is exported from the zui shared library.
//
// ZUI_LOG selects the level (trace, debug, info, warn, error, critical, off).
// The default is debug.
//
// ZUI_TRACE/DEBUG/INFO/WARN/ERROR/CRITICAL go to spdlog's stderr color console
// sink (stderr_color_sink_mt; wincolor_stderr_sink_mt on Windows).
// ZUI_FILE_* go to a basic file sink and not to the console. The path is
// ZUI_LOG_FILE, or zui.log in the process working directory when that is unset.
// The file is opened on the first ZUI_FILE_* call and appended to.

#ifndef ZUI_SOURCE_LOG_LOG_H
#define ZUI_SOURCE_LOG_LOG_H

namespace zui {
namespace log {

enum class level {
    trace = 0,
    debug = 1,
    info = 2,
    warn = 3,
    err = 4,
    critical = 5,
};

}  // namespace log
}  // namespace zui

#if defined(ZUI_WITH_SPDLOG)

#ifndef SPDLOG_COMPILED_LIB
#define SPDLOG_COMPILED_LIB
#endif
#ifndef SPDLOG_DISABLE_DEFAULT_LOGGER
#define SPDLOG_DISABLE_DEFAULT_LOGGER
#endif

#pragma push_macro("spdlog")
#pragma push_macro("fmt")
#undef spdlog
#undef fmt
#define spdlog zui_spdlog
#define fmt zui_fmt

#include <spdlog/logger.h>
#include <spdlog/spdlog.h>

#pragma pop_macro("fmt")
#pragma pop_macro("spdlog")

namespace zui {
namespace log {
namespace detail {

::zui_spdlog::logger& logger();
::zui_spdlog::logger& file_logger();
void flush_file();

inline ::zui_spdlog::level::level_enum to_spd(level lv) {
    switch (lv) {
    case level::trace:
        return ::zui_spdlog::level::trace;
    case level::debug:
        return ::zui_spdlog::level::debug;
    case level::info:
        return ::zui_spdlog::level::info;
    case level::warn:
        return ::zui_spdlog::level::warn;
    case level::err:
        return ::zui_spdlog::level::err;
    case level::critical:
        return ::zui_spdlog::level::critical;
    }
    return ::zui_spdlog::level::off;
}

}  // namespace detail

template <typename... Args>
inline void write(level lv, const char* file, int line,
                  ::zui_spdlog::format_string_t<Args...> text, Args&&... args) {
    ::zui_spdlog::logger& lg = detail::logger();
    const ::zui_spdlog::level::level_enum mapped = detail::to_spd(lv);
    if (!lg.should_log(mapped)) {
        return;
    }
    lg.log(::zui_spdlog::source_loc{file, line, SPDLOG_FUNCTION}, mapped, text,
           static_cast<Args&&>(args)...);
}

template <typename... Args>
inline void write_file(level lv, const char* file, int line,
                       ::zui_spdlog::format_string_t<Args...> text, Args&&... args) {
    ::zui_spdlog::logger& lg = detail::file_logger();
    const ::zui_spdlog::level::level_enum mapped = detail::to_spd(lv);
    if (!lg.should_log(mapped)) {
        return;
    }
    lg.log(::zui_spdlog::source_loc{file, line, SPDLOG_FUNCTION}, mapped, text,
           static_cast<Args&&>(args)...);
}

inline void flush() {
    detail::logger().flush();
    detail::flush_file();
}

}  // namespace log
}  // namespace zui

#define ZUI_TRACE(...) \
    ::zui::log::write(::zui::log::level::trace, __FILE__, __LINE__, __VA_ARGS__)
#define ZUI_DEBUG(...) \
    ::zui::log::write(::zui::log::level::debug, __FILE__, __LINE__, __VA_ARGS__)
#define ZUI_INFO(...) \
    ::zui::log::write(::zui::log::level::info, __FILE__, __LINE__, __VA_ARGS__)
#define ZUI_WARN(...) \
    ::zui::log::write(::zui::log::level::warn, __FILE__, __LINE__, __VA_ARGS__)
#define ZUI_ERROR(...) \
    ::zui::log::write(::zui::log::level::err, __FILE__, __LINE__, __VA_ARGS__)
#define ZUI_CRITICAL(...) \
    ::zui::log::write(::zui::log::level::critical, __FILE__, __LINE__, __VA_ARGS__)
#define ZUI_FILE_TRACE(...) \
    ::zui::log::write_file(::zui::log::level::trace, __FILE__, __LINE__, __VA_ARGS__)
#define ZUI_FILE_DEBUG(...) \
    ::zui::log::write_file(::zui::log::level::debug, __FILE__, __LINE__, __VA_ARGS__)
#define ZUI_FILE_INFO(...) \
    ::zui::log::write_file(::zui::log::level::info, __FILE__, __LINE__, __VA_ARGS__)
#define ZUI_FILE_WARN(...) \
    ::zui::log::write_file(::zui::log::level::warn, __FILE__, __LINE__, __VA_ARGS__)
#define ZUI_FILE_ERROR(...) \
    ::zui::log::write_file(::zui::log::level::err, __FILE__, __LINE__, __VA_ARGS__)
#define ZUI_FILE_CRITICAL(...) \
    ::zui::log::write_file(::zui::log::level::critical, __FILE__, __LINE__, __VA_ARGS__)

#else

#define ZUI_TRACE(...) ((void)0)
#define ZUI_DEBUG(...) ((void)0)
#define ZUI_INFO(...) ((void)0)
#define ZUI_WARN(...) ((void)0)
#define ZUI_ERROR(...) ((void)0)
#define ZUI_CRITICAL(...) ((void)0)
#define ZUI_FILE_TRACE(...) ((void)0)
#define ZUI_FILE_DEBUG(...) ((void)0)
#define ZUI_FILE_INFO(...) ((void)0)
#define ZUI_FILE_WARN(...) ((void)0)
#define ZUI_FILE_ERROR(...) ((void)0)
#define ZUI_FILE_CRITICAL(...) ((void)0)

#endif

#endif
