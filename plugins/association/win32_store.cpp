#include "plugins/association/win32_store.h"

#include "plugins/association/win32_registry.h"

#if defined(_WIN32)
#include "plugins/win32_msg.h"
#endif

namespace zui {
namespace association_win32 {

bool Win32AssociationStore::register_extension(std::string_view extension, std::string_view prog_id,
                                               std::string_view exe) {
#if defined(_WIN32)
    std::wstring ext = plugin_detail::widen(extension);
    if (ext.empty() || ext[0] != L'.') {
        ext.insert(ext.begin(), L'.');
    }
    const std::wstring prog = plugin_detail::widen(prog_id);
    HKEY hive = nullptr;
    const std::wstring ext_key = L"Software\\Classes\\" + ext;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, ext_key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hive, nullptr) !=
        ERROR_SUCCESS) {
        return false;
    }
    const LONG rc = RegSetValueExW(hive, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(prog.c_str()),
                                   static_cast<DWORD>((prog.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(hive);
    return rc == ERROR_SUCCESS && write_command(L"Software\\Classes\\" + prog + L"\\shell\\open\\command",
                                                plugin_detail::widen(exe));
#else
    (void)extension;
    (void)prog_id;
    (void)exe;
    return false;
#endif
}

bool Win32AssociationStore::register_scheme(std::string_view scheme, std::string_view prog_id, std::string_view exe) {
#if defined(_WIN32)
    const std::wstring prog = plugin_detail::widen(prog_id);
    const std::wstring key = L"Software\\Classes\\" + plugin_detail::widen(scheme);
    HKEY hive = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hive, nullptr) !=
        ERROR_SUCCESS) {
        return false;
    }
    const wchar_t* empty = L"";
    RegSetValueExW(hive, L"URL Protocol", 0, REG_SZ, reinterpret_cast<const BYTE*>(empty), sizeof(wchar_t));
    const LONG rc = RegSetValueExW(hive, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(prog.c_str()),
                                   static_cast<DWORD>((prog.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(hive);
    return rc == ERROR_SUCCESS && write_command(L"Software\\Classes\\" + prog + L"\\shell\\open\\command",
                                                plugin_detail::widen(exe));
#else
    (void)scheme;
    (void)prog_id;
    (void)exe;
    return false;
#endif
}

bool Win32AssociationStore::unregister_prog(std::string_view prog_id, std::string_view extension_or_scheme) {
#if defined(_WIN32)
    const std::wstring prog = L"Software\\Classes\\" + plugin_detail::widen(prog_id);
    const std::wstring ext = L"Software\\Classes\\" + plugin_detail::widen(extension_or_scheme);
    const LONG a = RegDeleteTreeW(HKEY_CURRENT_USER, prog.c_str());
    const LONG b = RegDeleteTreeW(HKEY_CURRENT_USER, ext.c_str());
    return gone(a) || gone(b);
#else
    (void)prog_id;
    (void)extension_or_scheme;
    return false;
#endif
}

bool Win32AssociationStore::write(const association_detail::Record& record, unsigned long& error) {
#if defined(_WIN32)
    const std::wstring prog = plugin_detail::widen(record.prog_id);
    const std::wstring name = plugin_detail::widen(record.app_name);
    const std::wstring command = plugin_detail::widen(record.command);
    const std::wstring classes = L"Software\\Classes\\" + prog;
    if (!set_sz(classes, nullptr, name, error)) {
        return false;
    }
    if (!set_sz(classes + L"\\shell\\open\\command", nullptr, command, error)) {
        return false;
    }
    if (!set_sz(classes + L"\\Application", L"ApplicationName", name, error)) {
        return false;
    }
    if (!set_sz(classes + L"\\Capabilities", L"ApplicationName", name, error)) {
        return false;
    }
    if (!record.extension.empty()) {
        const std::wstring ext = plugin_detail::widen(record.extension);
        if (!set_sz(classes + L"\\Capabilities\\FileAssociations", ext.c_str(), prog, error)) {
            return false;
        }
        if (!set_none(L"Software\\Classes\\" + ext + L"\\OpenWithProgids", prog, error)) {
            return false;
        }
        if (!record.mime.empty() &&
            !set_sz(L"Software\\Classes\\" + ext, L"Content Type", plugin_detail::widen(record.mime), error)) {
            return false;
        }
    }
    if (!record.mime.empty() && !record.extension.empty()) {
        const std::wstring mime_key =
            L"Software\\Classes\\MIME\\Database\\Content Type\\" + plugin_detail::widen(record.mime);
        if (!set_sz(mime_key, L"Extension", plugin_detail::widen(record.extension), error)) {
            return false;
        }
    }
    if (!record.scheme.empty()) {
        const std::wstring scheme = plugin_detail::widen(record.scheme);
        if (!set_sz(classes + L"\\Capabilities\\UrlAssociations", scheme.c_str(), prog, error)) {
            return false;
        }
        if (!set_sz(L"Software\\Classes\\" + scheme, L"URL Protocol", L"", error)) {
            return false;
        }
        if (!set_none(L"Software\\Classes\\" + scheme + L"\\OpenWithProgids", prog, error)) {
            return false;
        }
    }
    // Relative to HKCU. This names the app in Default Programs; it is not UserChoice.
    if (!set_sz(L"Software\\RegisteredApplications", prog.c_str(), L"Software\\Classes\\" + prog + L"\\Capabilities",
                error)) {
        return false;
    }
    return true;
#else
    (void)record;
    (void)error;
    return false;
#endif
}

void Win32AssociationStore::remove(const association_detail::Record& record) {
#if defined(_WIN32)
    if (!record.prog_id.empty()) {
        const std::wstring prog = plugin_detail::widen(record.prog_id);
        delete_value(L"Software\\RegisteredApplications", prog);
        delete_tree(L"Software\\Classes\\" + prog);
        if (!record.extension.empty()) {
            const std::wstring ext_key = L"Software\\Classes\\" + plugin_detail::widen(record.extension);
            const std::wstring open_with = ext_key + L"\\OpenWithProgids";
            delete_value(open_with, prog);
            delete_if_empty(open_with);
            if (read_sz(ext_key, L"Content Type") == plugin_detail::widen(record.mime)) {
                delete_value(ext_key, L"Content Type");
            }
            delete_if_empty(ext_key);
        }
        if (!record.scheme.empty()) {
            const std::wstring scheme_key = L"Software\\Classes\\" + plugin_detail::widen(record.scheme);
            const std::wstring open_with = scheme_key + L"\\OpenWithProgids";
            delete_value(open_with, prog);
            delete_if_empty(open_with);
            delete_value(scheme_key, L"URL Protocol");
            delete_if_empty(scheme_key);
        }
    }
    if (!record.mime.empty()) {
        const std::wstring mime_key =
            L"Software\\Classes\\MIME\\Database\\Content Type\\" + plugin_detail::widen(record.mime);
        if (record.extension.empty() || read_sz(mime_key, L"Extension") == plugin_detail::widen(record.extension)) {
            delete_value(mime_key, L"Extension");
            delete_if_empty(mime_key);
        }
    }
#else
    (void)record;
#endif
}

}  // namespace association_win32
}  // namespace zui
