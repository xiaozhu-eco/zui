#ifndef ZUI_EXT_PLATFORM_H
#define ZUI_EXT_PLATFORM_H

// Platform token for the services that take one: file dialogs and the shell.
// Other extension headers do not include this file.

namespace zui {

enum class PlatformKind { Win32, Linux, Macos, Ios, Android, Noop };

}  // namespace zui

#endif
