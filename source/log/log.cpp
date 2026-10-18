#include "log/log.h"

#if defined(ZUI_WITH_SPDLOG)

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

// The color sink header is included only from this file. It pulls in
// windows.h on Win32; the call-site header must not.
#pragma push_macro("spdlog")
#pragma push_macro("fmt")
#undef spdlog
#undef fmt
#define spdlog zui_spdlog
#define fmt zui_fmt
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#pragma pop_macro("fmt")
#pragma pop_macro("spdlog")

namespace zui {
namespace log {
namespace {

::zui_spdlog::level::level_enum level_from_env() {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
    const char* value = std::getenv("ZUI_LOG");
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    if (value == nullptr || value[0] == '\0') {
        return ::zui_spdlog::level::debug;
    }
    std::string level(value);
    for (char& ch : level) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    if (level == "trace") {
        return ::zui_spdlog::level::trace;
    }
    if (level == "debug") {
        return ::zui_spdlog::level::debug;
    }
    if (level == "info") {
        return ::zui_spdlog::level::info;
    }
    if (level == "warn" || level == "warning") {
        return ::zui_spdlog::level::warn;
    }
    if (level == "error" || level == "err") {
        return ::zui_spdlog::level::err;
    }
    if (level == "critical") {
        return ::zui_spdlog::level::critical;
    }
    if (level == "off") {
        return ::zui_spdlog::level::off;
    }
    return ::zui_spdlog::level::debug;
}

}  // namespace

namespace detail {

::zui_spdlog::logger& logger() {
    // Private instance. Not passed to register_logger / set_default_logger,
    // so an application using spdlog keeps its own registry and default logger.
    static std::shared_ptr<::zui_spdlog::logger> holder = [] {
        // Console sink. spdlog 1.15 has no ConsoleSink type; this is the
        // stderr color sink (wincolor_stderr_sink_mt on Windows).
        auto sink = std::make_shared<::zui_spdlog::sinks::stderr_color_sink_mt>();
        auto created = std::make_shared<::zui_spdlog::logger>("zui", std::move(sink));
        created->set_level(level_from_env());
        created->set_pattern("[%H:%M:%S.%e] [zui] [%^%l%$] %s:%# %v");
        created->flush_on(::zui_spdlog::level::trace);
        // Must not log: the sink already failed, and a ZUI_* call would recurse.
        created->set_error_handler([](const std::string& message) {
            std::fprintf(stderr, "[zui] log error: %s\n", message.c_str());
        });
        return created;
    }();
    return *holder;
}

namespace {

std::shared_ptr<::zui_spdlog::logger>& file_slot() {
    static std::shared_ptr<::zui_spdlog::logger> slot;
    return slot;
}

std::shared_ptr<::zui_spdlog::logger> open_file_logger() {
    try {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
        const char* from_env = std::getenv("ZUI_LOG_FILE");
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
        const std::string path =
            (from_env != nullptr && from_env[0] != '\0') ? std::string(from_env) : std::string("zui.log");
        const std::filesystem::path file(path);
        if (file.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(file.parent_path(), ec);
        }
        // Append. Several test processes can share one file; truncate would drop the others.
        auto sink = std::make_shared<::zui_spdlog::sinks::basic_file_sink_mt>(path, false);
        auto created = std::make_shared<::zui_spdlog::logger>("zui-file", std::move(sink));
        created->set_level(level_from_env());
        created->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [zui] [%l] %s:%# %v");
        created->flush_on(::zui_spdlog::level::trace);
        created->set_error_handler([](const std::string& message) {
            std::fprintf(stderr, "[zui] log error: %s\n", message.c_str());
        });
        return created;
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "[zui] log file: %s\n", ex.what());
        auto created = std::make_shared<::zui_spdlog::logger>("zui-file");
        created->set_level(::zui_spdlog::level::off);
        return created;
    }
}

}  // namespace

::zui_spdlog::logger& file_logger() {
    static std::once_flag once;
    std::call_once(once, [] { file_slot() = open_file_logger(); });
    return *file_slot();
}

void flush_file() {
    if (const std::shared_ptr<::zui_spdlog::logger>& slot = file_slot()) {
        slot->flush();
    }
}

}  // namespace detail
}  // namespace log
}  // namespace zui

// Layout tests include trace.h but do not compile trace.cpp. The no-SDK
// warning still has to reach this sink, so the definition lives next to it.
namespace zui {
namespace trace {
namespace detail {

void warn_without_sdk() {
    static const bool warned = [] {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
        const char* path = std::getenv("ZUI_TRACE");
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
        if (path != nullptr) {
            ZUI_WARN("[trace] {} requested, but this binary was built without ZUI_WITH_PERFETTO",
                     path);
        }
        return true;
    }();
    (void)warned;
}

}  // namespace detail
}  // namespace trace
}  // namespace zui

#endif
