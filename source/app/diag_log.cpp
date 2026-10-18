#include <zui/app.h>

#include "app/impl.h"

#include <filesystem>
#include <fstream>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace zui {

namespace {

#if defined(_WIN32)
std::string g_crash_path;

LONG WINAPI diagnostic_crash_filter(EXCEPTION_POINTERS* info) {
    const unsigned code =
        info != nullptr && info->ExceptionRecord != nullptr ? info->ExceptionRecord->ExceptionCode : 0;
    const char* name = "EXCEPTION";
    if (code == EXCEPTION_ACCESS_VIOLATION) {
        name = "ACCESS_VIOLATION";
    }
    std::ofstream out(g_crash_path, std::ios::out | std::ios::trunc);
    if (out) {
        out << name << " 0x" << std::hex << code << "\n";
    }
    ExitProcess(1);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

}  // namespace

bool App::enable_diagnostics(std::string_view log_path) {
    if (log_path.empty()) {
        return false;
    }
    const std::string path(log_path);
    {
        std::ofstream out(path, std::ios::out | std::ios::trunc);
        if (!out) {
            return false;
        }
    }
    m_impl->diag_path = path;
    m_impl->diag_on = true;
#if defined(_WIN32)
    g_crash_path = std::filesystem::path(path).replace_extension(".crash").string();
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(diagnostic_crash_filter);
#endif
    return true;
}

void App::log_line(std::string_view line) {
    if (!m_impl->diag_on || m_impl->diag_path.empty()) {
        return;
    }
    std::ofstream out(m_impl->diag_path, std::ios::out | std::ios::app);
    if (!out) {
        return;
    }
    out << line << '\n';
}

}  // namespace zui
