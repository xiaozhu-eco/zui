#ifndef ZUI_PLUGIN_ASSOCIATION_ANDROID_LAUNCH_H
#define ZUI_PLUGIN_ASSOCIATION_ANDROID_LAUNCH_H

#include <zui/plugin.h>

#include <string_view>

namespace zui {
namespace association_android {

class AndroidAssociationLaunch {
public:
    plugin::OpenerReport launch(std::string_view executable, std::string_view file_path);
};

}  // namespace association_android
}  // namespace zui

#endif
