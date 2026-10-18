#ifndef ZUI_PLUGIN_ASSOCIATION_MACOS_QUERY_H
#define ZUI_PLUGIN_ASSOCIATION_MACOS_QUERY_H

#include <zui/plugin.h>

#include <string_view>

namespace zui {
namespace association_macos {

class MacAssociationQuery {
public:
    plugin::OpenerReport query(std::string_view type);
};

}  // namespace association_macos
}  // namespace zui

#endif
