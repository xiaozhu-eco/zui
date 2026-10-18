#ifndef ZUI_PLUGIN_SHELL_LAYOUT_H
#define ZUI_PLUGIN_SHELL_LAYOUT_H

// Directory strings for the non-Windows shells. Joins with '/' so a Windows
// test sees the platform path without calling that platform.

#include <string>
#include <string_view>

namespace zui {
namespace shell_layout {

struct Roots {
    std::string home;
    std::string xdg_data_home;
    std::string xdg_cache_home;
    std::string ios_container;
    std::string android_files;
    std::string android_cache;
};

std::string effective_id(std::string_view app_id);
std::string linux_user_data(const Roots& roots, std::string_view app_id);
std::string linux_cache(const Roots& roots, std::string_view app_id);
std::string macos_user_data(const Roots& roots, std::string_view app_id);
std::string macos_cache(const Roots& roots, std::string_view app_id);
std::string ios_user_data(const Roots& roots, std::string_view app_id);
std::string ios_cache(const Roots& roots, std::string_view app_id);
std::string android_user_data(const Roots& roots, std::string_view app_id);
std::string android_cache(const Roots& roots, std::string_view app_id);

const char* ios_desktop_data_leaf();
const char* ios_desktop_cache_leaf();
const char* android_desktop_data_leaf();
const char* android_desktop_cache_leaf();

}  // namespace shell_layout
}  // namespace zui

#endif
