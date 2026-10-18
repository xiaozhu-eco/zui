#ifndef ZUI_PLUGIN_ASSOCIATION_LINUX_LAUNCH_H
#define ZUI_PLUGIN_ASSOCIATION_LINUX_LAUNCH_H

#include <zui/plugin.h>

#include <string_view>

namespace zui {
namespace association_linux {

class LinuxAssociationLaunch {
public:
    plugin::OpenerReport launch(std::string_view executable, std::string_view file_path);
};

}  // namespace association_linux
}  // namespace zui

#endif
