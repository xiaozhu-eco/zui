#ifndef ZUI_BACKENDS_WIN32_CLIPBOARD_IMAGE_H
#define ZUI_BACKENDS_WIN32_CLIPBOARD_IMAGE_H

#include "backends/window.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace zui {
namespace backends {

ClipboardImage clipboard_image_win32();
bool set_clipboard_image_win32(int width, int height, const std::uint8_t* rgba, std::size_t bytes);
ClipboardText clipboard_text_win32();
bool set_clipboard_text_win32(std::string_view text);

}  // namespace backends
}  // namespace zui

#endif
