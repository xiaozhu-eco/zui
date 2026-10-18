#ifndef ZUI_EXT_HOTKEY_H
#define ZUI_EXT_HOTKEY_H

#include <zui/plugin.h>

namespace zui {

class HotkeyService {
public:
    virtual ~HotkeyService() = default;
    virtual plugin::OsResult global_hotkey(const plugin::Hotkey& key, plugin::Handler on_fire) = 0;
};

}  // namespace zui

#endif
