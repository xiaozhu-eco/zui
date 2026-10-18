#include "plugins/association/win32_query.h"

#include "plugins/association/record.h"
#include "plugins/association/win32_registry.h"

#if defined(_WIN32)
#include "plugins/win32_msg.h"
#endif

namespace zui {
namespace association_win32 {
namespace {

#if defined(_WIN32)
std::wstring prog_with_command(const std::wstring& open_with_key) {
    HKEY hive = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, open_with_key.c_str(), 0, KEY_READ, &hive) != ERROR_SUCCESS) {
        return {};
    }
    std::wstring found;
    for (DWORD index = 0; found.empty(); ++index) {
        wchar_t name[512];
        DWORD count = 512;
        if (RegEnumValueW(hive, index, name, &count, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
            break;
        }
        const std::wstring prog(name, count);
        const std::wstring command = read_sz(L"Software\\Classes\\" + prog + L"\\shell\\open\\command", nullptr);
        if (!command.empty()) {
            found = prog;
        }
    }
    RegCloseKey(hive);
    return found;
}
#endif

}  // namespace

plugin::OpenerReport Win32AssociationQuery::query(std::string_view type) {
    plugin::OpenerReport report;
#if defined(_WIN32)
    report.ran = true;
    const association_detail::Classified kind = association_detail::classify(type);
    std::wstring open_with;
    if (kind.kind == association_detail::TypeKind::Extension) {
        open_with = L"Software\\Classes\\" + plugin_detail::widen(kind.value) + L"\\OpenWithProgids";
        report.extension = kind.value;
    } else if (kind.kind == association_detail::TypeKind::Mime) {
        report.mime = kind.value;
        const std::wstring ext =
            read_sz(L"Software\\Classes\\MIME\\Database\\Content Type\\" + plugin_detail::widen(kind.value),
                    L"Extension");
        if (ext.empty()) {
            return report;
        }
        report.extension = narrow(ext);
        open_with = L"Software\\Classes\\" + ext + L"\\OpenWithProgids";
    } else if (kind.kind == association_detail::TypeKind::Scheme) {
        report.scheme = kind.value;
        open_with = L"Software\\Classes\\" + plugin_detail::widen(kind.value) + L"\\OpenWithProgids";
    } else {
        return report;
    }
    const std::wstring prog = prog_with_command(open_with);
    if (prog.empty()) {
        return report;
    }
    const std::wstring command = read_sz(L"Software\\Classes\\" + prog + L"\\shell\\open\\command", nullptr);
    const std::wstring name = read_sz(L"Software\\Classes\\" + prog, nullptr);
    report.command = narrow(command);
    report.app_name = narrow(name);
    report.executable = association_detail::executable_from_command(report.command);
    report.ok = !report.executable.empty();
#else
    (void)type;
#endif
    return report;
}

}  // namespace association_win32
}  // namespace zui
