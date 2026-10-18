#ifndef ZUI_PLUGIN_ASSOCIATION_WIN32_REGISTRY_H
#define ZUI_PLUGIN_ASSOCIATION_WIN32_REGISTRY_H

#include <string>

namespace zui {
namespace association_win32 {

bool set_sz(const std::wstring& key, const wchar_t* name, const std::wstring& value, unsigned long& error);
bool set_none(const std::wstring& key, const std::wstring& name, unsigned long& error);
std::wstring read_sz(const std::wstring& key, const wchar_t* name);
std::string narrow(std::wstring_view text);
bool gone(long code);
void delete_tree(const std::wstring& key);
void delete_value(const std::wstring& key, const std::wstring& name);
void delete_if_empty(const std::wstring& key);
bool write_command(const std::wstring& key, const std::wstring& exe);

}  // namespace association_win32
}  // namespace zui

#endif
