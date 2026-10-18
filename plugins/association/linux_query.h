#ifndef ZUI_PLUGIN_ASSOCIATION_LINUX_QUERY_H
#define ZUI_PLUGIN_ASSOCIATION_LINUX_QUERY_H

#include <zui/plugin.h>

#include <string_view>

namespace zui {
namespace association_linux {

class LinuxAssociationQuery {
public:
    plugin::OpenerReport query(std::string_view type);
};

}  // namespace association_linux
}  // namespace zui

#endif
