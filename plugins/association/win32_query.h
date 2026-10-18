#ifndef ZUI_PLUGIN_ASSOCIATION_WIN32_QUERY_H
#define ZUI_PLUGIN_ASSOCIATION_WIN32_QUERY_H

#include <zui/plugin.h>

#include <string_view>

namespace zui {
namespace association_win32 {

class Win32AssociationQuery {
public:
    plugin::OpenerReport query(std::string_view type);
};

}  // namespace association_win32
}  // namespace zui

#endif
