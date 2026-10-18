#ifndef ZUI_EXT_CLIPBOARD_H
#define ZUI_EXT_CLIPBOARD_H

#include <zui/host_types.h>

#include <string_view>

namespace zui {

class ClipboardService {
public:
    virtual ~ClipboardService() = default;
    virtual ClipboardText text() = 0;
    virtual bool set_text(std::string_view text) = 0;
    virtual ClipboardImage image() = 0;
    virtual bool set_image(const ClipboardImage& image) = 0;
};

}  // namespace zui

#endif
