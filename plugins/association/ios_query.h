#ifndef ZUI_PLUGIN_ASSOCIATION_IOS_QUERY_H
#define ZUI_PLUGIN_ASSOCIATION_IOS_QUERY_H

#include <zui/plugin.h>

#include <string_view>

namespace zui {
namespace association_ios {

class IosAssociationQuery {
public:
    plugin::OpenerReport query(std::string_view type);
};

}  // namespace association_ios
}  // namespace zui

#endif
