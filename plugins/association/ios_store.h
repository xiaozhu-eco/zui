#ifndef ZUI_PLUGIN_ASSOCIATION_IOS_STORE_H
#define ZUI_PLUGIN_ASSOCIATION_IOS_STORE_H

#include "plugins/association/record.h"

namespace zui {
namespace association_ios {

class IosAssociationStore {
public:
    plugin::OpenerReport write(const association_detail::Record& record, plugin::OpenerReport report);
    bool remove(const association_detail::Record& record);
};

}  // namespace association_ios
}  // namespace zui

#endif
