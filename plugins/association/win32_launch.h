#ifndef ZUI_PLUGIN_ASSOCIATION_WIN32_LAUNCH_H
#define ZUI_PLUGIN_ASSOCIATION_WIN32_LAUNCH_H

#include <zui/plugin.h>

#include <string_view>

namespace zui {
namespace association_win32 {

class Win32AssociationLaunch {
public:
    plugin::OpenerReport launch(std::string_view executable, std::string_view file_path);
};

}  // namespace association_win32
}  // namespace zui

#endif
