#ifndef ZUI_PLUGIN_INSTANCE_ENDPOINT_H
#define ZUI_PLUGIN_INSTANCE_ENDPOINT_H

// Socket and kernel-object names. No OS headers: a Windows test can read the
// path a Linux, macOS, iOS, or Android build would claim.

#include <string>
#include <string_view>

namespace zui {
namespace instance_detail {

std::string safe_name(std::string_view identity);

std::string win32_mutex_name(std::string_view identity);
std::string win32_pipe_name(std::string_view identity);

std::string linux_socket_path(std::string_view runtime_dir, std::string_view identity);
std::string macos_socket_path(std::string_view home, std::string_view identity);
std::string ios_socket_path(std::string_view container, std::string_view identity);
std::string android_socket_path(std::string_view files_dir, std::string_view identity);
std::string ios_notification_name(std::string_view identity);

}  // namespace instance_detail
}  // namespace zui

#endif
