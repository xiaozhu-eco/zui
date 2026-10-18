#ifndef ZUI_PLUGIN_ASSOCIATION_WIN32_STORE_H
#define ZUI_PLUGIN_ASSOCIATION_WIN32_STORE_H

#include "plugins/association/record.h"

#include <string_view>

namespace zui {
namespace association_win32 {

class Win32AssociationStore {
public:
    bool register_extension(std::string_view extension, std::string_view prog_id, std::string_view exe);
    bool register_scheme(std::string_view scheme, std::string_view prog_id, std::string_view exe);
    bool unregister_prog(std::string_view prog_id, std::string_view extension_or_scheme);
    bool write(const association_detail::Record& record, unsigned long& error);
    void remove(const association_detail::Record& record);
};

}  // namespace association_win32
}  // namespace zui

#endif
