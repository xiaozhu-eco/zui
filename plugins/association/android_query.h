#ifndef ZUI_PLUGIN_ASSOCIATION_ANDROID_QUERY_H
#define ZUI_PLUGIN_ASSOCIATION_ANDROID_QUERY_H

#include <zui/plugin.h>

#include <string_view>

namespace zui {
namespace association_android {

class AndroidAssociationQuery {
public:
    plugin::OpenerReport query(std::string_view type);
};

}  // namespace association_android
}  // namespace zui

#endif
