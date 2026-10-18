#ifndef ZUI_PLUGIN_ASSOCIATION_IOS_LAUNCH_H
#define ZUI_PLUGIN_ASSOCIATION_IOS_LAUNCH_H

#include <zui/plugin.h>

#include <string_view>

namespace zui {
namespace association_ios {

class IosAssociationLaunch {
public:
    plugin::OpenerReport launch(std::string_view executable, std::string_view file_path);
};

}  // namespace association_ios
}  // namespace zui

#endif
