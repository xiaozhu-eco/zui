# Capability sources live in the subdirectories. Host tests still link the
# aggregate `zui_plugins` target; a blank app does not. The per-capability
# libraries are registered as their sources are wired.

add_library(zui_plugins INTERFACE)
add_library(zui::plugins ALIAS zui_plugins)

# Codec adapters only. The CPU SVG parser is part of zui_image; the SVG
# driver calls it. software_raster.cpp is the driver's CPU fallback.
zui_add_plugin(image
  image/jpeg_driver.cpp
  image/png_driver.cpp
  image/register.cpp
  image/software_raster.cpp
  image/svg_driver.cpp
)
target_link_libraries(zui_plugin_image PUBLIC png_static jpeg-static)
target_link_libraries(zui_plugin_image PRIVATE zui_image)

zui_add_plugin(clipboard
  clipboard/register.cpp
  clipboard/win32.cpp
  clipboard/linux.cpp
  clipboard/macos.cpp
  clipboard/ios.cpp
  clipboard/android.cpp
)
zui_add_plugin(pick
  pick/register.cpp
  pick/command.cpp
  pick/win32.cpp
  pick/linux.cpp
  pick/macos.cpp
  pick/ios.cpp
  pick/android.cpp
)
zui_add_plugin(shell
  shell/register.cpp
  shell/layout.cpp
  shell/win32.cpp
  shell/linux.cpp
  shell/macos.cpp
  shell/ios.cpp
  shell/android.cpp
  shell/noop.cpp
)
zui_add_plugin(gamepad gamepad/sdl.cpp)
zui_add_plugin(tasks tasks/pool.cpp)
zui_add_plugin(notify
  notify/register.cpp
  notify/win32.cpp
  notify/linux.cpp
  notify/macos.cpp
  notify/ios.cpp
  notify/android.cpp
)
zui_add_plugin(tray
  tray/register.cpp
  tray/item.cpp
  tray/win32.cpp
  tray/linux.cpp
  tray/macos.cpp
  tray/ios.cpp
  tray/android.cpp
)
zui_add_plugin(hotkey
  hotkey/register.cpp
  hotkey/chord.cpp
  hotkey/win32.cpp
  hotkey/linux.cpp
  hotkey/macos.cpp
  hotkey/ios.cpp
  hotkey/android.cpp
)
zui_add_plugin(media_key
  media_key/register.cpp
  media_key/win32.cpp
  media_key/linux.cpp
  media_key/macos.cpp
  media_key/ios.cpp
  media_key/android.cpp
)
zui_add_plugin(instance
  instance/register.cpp
  instance/endpoint.cpp
  instance/win32.cpp
  instance/linux.cpp
  instance/macos.cpp
  instance/ios.cpp
  instance/android.cpp
)
zui_add_plugin(association
  association/record.cpp
  association/text.cpp
  association/register.cpp
  association/win32.cpp
  association/win32_registry.cpp
  association/win32_store.cpp
  association/win32_query.cpp
  association/win32_launch.cpp
  association/linux.cpp
  association/linux_store.cpp
  association/linux_query.cpp
  association/linux_launch.cpp
  association/macos.cpp
  association/macos_store.cpp
  association/macos_query.cpp
  association/macos_launch.cpp
  association/ios.cpp
  association/ios_store.cpp
  association/ios_query.cpp
  association/ios_launch.cpp
  association/android.cpp
  association/android_store.cpp
  association/android_query.cpp
  association/android_launch.cpp
)
zui_add_plugin(update update/update.cpp)
zui_add_plugin(extra_codec extra_codec/codec.cpp)
zui_add_plugin(view_commit view_commit/view.cpp)

if(ZUI_WITH_SDL)
  target_compile_definitions(zui_plugin_gamepad PRIVATE ZUI_WITH_SDL=1)
  target_link_libraries(zui_plugin_gamepad PRIVATE SDL3::SDL3)
endif()

if(WIN32)
  foreach(plugin
      zui_plugin_pick
      zui_plugin_clipboard
      zui_plugin_shell
      zui_plugin_tray
      zui_plugin_notify
      zui_plugin_hotkey
      zui_plugin_media_key
      zui_plugin_instance
      zui_plugin_association)
    target_link_libraries(${plugin} PRIVATE ole32 shell32 uuid user32 advapi32)
  endforeach()
endif()

# builtin.cpp names these register functions. Linking them here keeps a static
# plugin in zui.dll and records the import library when plugins are shared.
target_link_libraries(zui PRIVATE
  zui_plugin_image
  zui_plugin_clipboard
  zui_plugin_pick
  zui_plugin_shell
  zui_plugin_gamepad
  zui_plugin_tasks
)

target_link_libraries(zui_plugins INTERFACE
  zui_plugin_image
  zui_plugin_clipboard
  zui_plugin_pick
  zui_plugin_shell
  zui_plugin_gamepad
  zui_plugin_tasks
  zui_plugin_notify
  zui_plugin_tray
  zui_plugin_hotkey
  zui_plugin_media_key
  zui_plugin_instance
  zui_plugin_association
  zui_plugin_update
  zui_plugin_extra_codec
  zui_plugin_view_commit
)
