#include "plugins/association/win32_launch.h"

#if defined(_WIN32)
#include "plugins/win32_msg.h"
#endif

namespace zui {
namespace association_win32 {

plugin::OpenerReport Win32AssociationLaunch::launch(std::string_view executable, std::string_view file_path) {
    plugin::OpenerReport report;
    if (executable.empty() || file_path.empty()) {
        report.ran = true;
        return report;
    }
    report.executable = std::string(executable);
#if defined(_WIN32)
    report.ran = true;
    const std::wstring exe = plugin_detail::widen(executable);
    const std::wstring file = plugin_detail::widen(file_path);
    std::wstring command = L"\"" + exe + L"\" \"" + file + L"\"";
    STARTUPINFOW start{};
    start.cb = sizeof(start);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                        &start, &process)) {
        report.os_error = static_cast<unsigned long>(GetLastError());
        return report;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    report.ok = true;
#else
    (void)file_path;
#endif
    return report;
}

}  // namespace association_win32
}  // namespace zui
