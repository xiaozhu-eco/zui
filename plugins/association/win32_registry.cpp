#include "plugins/association/win32_registry.h"

#if defined(_WIN32)
#include "plugins/win32_msg.h"
#endif

namespace zui {
namespace association_win32 {

bool set_sz(const std::wstring& key, const wchar_t* name, const std::wstring& value, unsigned long& error) {
#if defined(_WIN32)
    HKEY hive = nullptr;
    const LONG opened =
        RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hive, nullptr);
    if (opened != ERROR_SUCCESS) {
        error = static_cast<unsigned long>(opened);
        return false;
    }
    const LONG wrote = RegSetValueExW(hive, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                                      static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(hive);
    if (wrote != ERROR_SUCCESS) {
        error = static_cast<unsigned long>(wrote);
        return false;
    }
    return true;
#else
    (void)key;
    (void)name;
    (void)value;
    (void)error;
    return false;
#endif
}

bool set_none(const std::wstring& key, const std::wstring& name, unsigned long& error) {
#if defined(_WIN32)
    HKEY hive = nullptr;
    const LONG opened =
        RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hive, nullptr);
    if (opened != ERROR_SUCCESS) {
        error = static_cast<unsigned long>(opened);
        return false;
    }
    const LONG wrote = RegSetValueExW(hive, name.c_str(), 0, REG_NONE, nullptr, 0);
    RegCloseKey(hive);
    if (wrote != ERROR_SUCCESS) {
        error = static_cast<unsigned long>(wrote);
        return false;
    }
    return true;
#else
    (void)key;
    (void)name;
    (void)error;
    return false;
#endif
}

std::wstring read_sz(const std::wstring& key, const wchar_t* name) {
#if defined(_WIN32)
    DWORD bytes = 0;
    const LONG sized = RegGetValueW(HKEY_CURRENT_USER, key.c_str(), name, RRF_RT_REG_SZ, nullptr, nullptr, &bytes);
    if (sized != ERROR_SUCCESS || bytes < sizeof(wchar_t)) {
        return {};
    }
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    const LONG read =
        RegGetValueW(HKEY_CURRENT_USER, key.c_str(), name, RRF_RT_REG_SZ, nullptr, value.data(), &bytes);
    if (read != ERROR_SUCCESS || bytes < sizeof(wchar_t)) {
        return {};
    }
    value.resize((bytes / sizeof(wchar_t)) - 1);
    return value;
#else
    (void)key;
    (void)name;
    return {};
#endif
}

std::string narrow(std::wstring_view text) {
#if defined(_WIN32)
    if (text.empty()) {
        return {};
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr,
                                      nullptr);
    if (n <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n, nullptr, nullptr);
    return out;
#else
    (void)text;
    return {};
#endif
}

bool gone(long code) {
#if defined(_WIN32)
    return code == ERROR_SUCCESS || code == ERROR_FILE_NOT_FOUND;
#else
    (void)code;
    return false;
#endif
}

void delete_tree(const std::wstring& key) {
#if defined(_WIN32)
    RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str());
#else
    (void)key;
#endif
}

void delete_value(const std::wstring& key, const std::wstring& name) {
#if defined(_WIN32)
    HKEY hive = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_SET_VALUE, &hive) != ERROR_SUCCESS) {
        return;
    }
    RegDeleteValueW(hive, name.c_str());
    RegCloseKey(hive);
#else
    (void)key;
    (void)name;
#endif
}

void delete_if_empty(const std::wstring& key) {
#if defined(_WIN32)
    HKEY hive = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_READ, &hive) != ERROR_SUCCESS) {
        return;
    }
    DWORD subkeys = 0;
    DWORD values = 0;
    RegQueryInfoKeyW(hive, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, &values, nullptr, nullptr, nullptr,
                     nullptr);
    RegCloseKey(hive);
    if (subkeys == 0 && values == 0) {
        delete_tree(key);
    }
#else
    (void)key;
#endif
}

bool write_command(const std::wstring& key, const std::wstring& exe) {
    unsigned long error = 0;
    return set_sz(key, nullptr, L"\"" + exe + L"\" \"%1\"", error);
}

}  // namespace association_win32
}  // namespace zui
