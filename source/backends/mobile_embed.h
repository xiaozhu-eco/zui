#ifndef ZUI_BACKENDS_MOBILE_EMBED_H
#define ZUI_BACKENDS_MOBILE_EMBED_H

/// Native mobile / embedder surface (not part of include/zui).
///
/// Interface abstraction stays in App / IPlatform / IWindow. Device hosts
/// include this header from their ObjC++/JNI glue and call these entry points
/// when the OS delivers files or finishes a document picker.
///
/// Typical iOS / Android host loop:
///   1. Create App, load HTML, create backends (or call App::run setup path).
///   2. Each display tick: App::pump_frame().
///   3. On share / open-with: window->inject_opened_paths({utf8_path...}).
///      Next pump_frame drains them into ondrop (same as desktop file drop).
///   4. App::pick_*_async calls IPlatform::begin_pick_*. On iOS that presents
///      UIDocumentPicker (ios_host.mm). On Android the app must call
///      zui.ZuiHost.attach and forward onActivityResult
///      (source/backends/android/zui/ZuiHost.java); the picker copies the
///      chosen document into the cache and completes via mobile_complete_pick_*.
///      A headless stub leaves the handler pending until mobile_complete_pick_*.
///   5. Safe-area comes from SDL_GetWindowSafeArea (desktop insets stay 0).
///      Keyboard height: iOS UIKeyboard frame, Android WindowInsets.ime after
///      ZuiHost.attach. Lifecycle / orientation test inject still works:
///      window->set_safe_area_insets_for_test(...);
///      window->set_keyboard_bottom_for_test(height_css_px);
///      window->set_visibility_for_test(HostVisibility::Hidden|Visible);
///      window->set_orientation_for_test(HostOrientation::Portrait|Landscape);
///      App copies insets into HostMediaFacts → env(safe-area-inset-*) /
///      env(keyboard-inset-bottom); take_visibility_change → visibilitychange;
///      take_orientation_change → orientationchange.
///
/// Do not put HWND / NSURL / jobject into include/zui or application HTML.

#include "backends/platform.h"
#include "backends/window.h"

#include <zui/app.h>
#include <zui/host_types.h>

#endif
