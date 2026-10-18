#ifndef ZUI_EXT_MEDIA_KEY_H
#define ZUI_EXT_MEDIA_KEY_H

#include <zui/plugin.h>

namespace zui {

class MediaKeyService {
public:
    virtual ~MediaKeyService() = default;
    virtual plugin::OsResult media_key(plugin::MediaKey action, plugin::Handler on_fire) = 0;
};

}  // namespace zui

#endif
