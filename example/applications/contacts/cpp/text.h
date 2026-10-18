#ifndef CONTACTS_TEXT_H
#define CONTACTS_TEXT_H

#include <string>
#include <string_view>

namespace contacts {

std::string trim(std::string_view in);
std::string first_char(const std::string& text);
std::string group_label(const std::string& id);
/// Browser check: `/^[^\s@]+@[^\s@]+\.[^\s@]+$/`. Empty is allowed by the caller.
bool valid_email(const std::string& email);
/// 1 when `a` sorts before `b` (Win32 CSTR_LESS_THAN). 2 equal, 3 greater.
int name_cmp(const std::string& a, const std::string& b);

}  // namespace contacts

#endif
