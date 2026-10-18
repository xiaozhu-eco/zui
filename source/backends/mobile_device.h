#ifndef ZUI_BACKENDS_MOBILE_DEVICE_H
#define ZUI_BACKENDS_MOBILE_DEVICE_H

#include "backends/platform.h"

#include <string_view>

namespace zui {
namespace backends {

/// Device IPlatform. Linked only when this binary is built for that OS.
/// Desktop builds keep the headless stub from mobile_platform.cpp.
std::unique_ptr<IPlatform> create_ios_platform_device();
std::unique_ptr<IPlatform> create_android_platform_device();

/// One-shot pick injection shared with the headless stub. Device pick_* checks
/// these first so host tests can still complete a pick without OS UI.
bool mobile_consume_injected_open(PickPaths& out);
bool mobile_consume_injected_save(PickPath& out);
bool mobile_consume_injected_folder(PickPath& out);

/// Defined in the device translation unit (ios_host.mm / android_host.cpp).
/// Desktop TUs must not call them.
bool device_apply_host_media(HostMediaFacts& facts);
float device_keyboard_bottom();
void device_install_keyboard_observer();
ClipboardText device_clipboard_text();
bool device_set_clipboard_text(std::string_view text);

}  // namespace backends
}  // namespace zui

#endif
