set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/shaders/glyph_cover.glsl")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/shaders/glyph_cover.glsl" ZUI_GLYPH_COVER_GLSL)
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/shaders/glyph_cover_glsl.h.in"
  "${ZUI_GENERATED_INCLUDE_DIR}/zui/glyph_cover_glsl.h"
  @ONLY
)

zui_add_interface_sources(zui_src_backends
  factory.cpp
  group_alpha.cpp
  host_media.cpp
  noop_platform.cpp
  win32/platform.cpp
  linux/platform.cpp
  macos/platform.cpp
  mobile/platform.cpp
  permission_policy.cpp
  glyph_atlas.cpp
  glyph_outlines.cpp
  ime_composer.cpp
  win32/ime_caret.cpp
  draw_effect.cpp
  image_textures.cpp
  sdl3/window.cpp
  glfw3/window.cpp
  mobile/window.cpp
  opengl/renderer.cpp
  opengl/gl_context.cpp
  opengl/gl_programs.cpp
  opengl/gl_textures.cpp
  opengl/gl_commands.cpp
  vulkan/renderer.cpp
  vulkan/submit.cpp
  vulkan/device.cpp
  vulkan/swapchain.cpp
  vulkan/uploads.cpp
  vulkan/frame.cpp
  vulkan/pipelines.cpp
  vulkan/curves.cpp
)

# Device translation units stay out of the desktop build. The same IPlatform /
# IWindow entry points call into them when the binary is built for that OS.
if(IOS OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
  enable_language(OBJCXX)
  target_sources(zui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/ios/host.mm")
  target_link_libraries(zui PRIVATE
    "-framework Foundation"
    "-framework UIKit"
    "-framework UniformTypeIdentifiers"
    "-framework AVFoundation"
    "-framework CoreBluetooth"
    "-framework CoreLocation"
    "-framework CoreMotion"
    "-framework Contacts"
    "-framework EventKit"
    "-framework LocalAuthentication"
    "-framework MediaPlayer"
    "-framework Photos"
    "-framework UserNotifications"
    "-framework CoreNFC"
    "-framework Network")
endif()
if(ANDROID)
  target_sources(zui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/android/host.cpp")
  target_link_libraries(zui PRIVATE android log)
endif()
