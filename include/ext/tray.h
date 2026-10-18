#ifndef ZUI_EXT_TRAY_H
#define ZUI_EXT_TRAY_H

#include <zui/plugin.h>

#include <string_view>

namespace zui {

class TrayService {
public:
    virtual ~TrayService() = default;
    virtual plugin::OsResult item(std::string_view tip, plugin::Handler on_activate) = 0;
    virtual plugin::OsResult badge(int count) = 0;
    virtual int badge_count() const = 0;
};

}  // namespace zui

#endif
