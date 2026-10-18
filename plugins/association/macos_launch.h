#ifndef ZUI_PLUGIN_ASSOCIATION_MACOS_LAUNCH_H
#define ZUI_PLUGIN_ASSOCIATION_MACOS_LAUNCH_H

#include <zui/plugin.h>

#include <string_view>

namespace zui {
namespace association_macos {

class MacAssociationLaunch {
public:
    plugin::OpenerReport launch(std::string_view executable, std::string_view file_path);
};

}  // namespace association_macos
}  // namespace zui

#endif
