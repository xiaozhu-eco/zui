#ifndef ZUI_EXT_NOTIFY_H
#define ZUI_EXT_NOTIFY_H

#include <zui/plugin.h>

#include <string_view>

namespace zui {

class NotifyService {
public:
    virtual ~NotifyService() = default;
    virtual plugin::OsResult notify(std::string_view title, std::string_view body) = 0;
};

}  // namespace zui

#endif
