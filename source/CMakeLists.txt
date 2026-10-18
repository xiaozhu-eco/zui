# Internal modules of the public `zui` target. Modules are folders, not
# installed libraries; see docs/ARCHITECTURE.md. INTERFACE collections (zui_src_*)
# exist so kernel-plane tests can compile the same translation units without
# linking App / IWindow.
#
# add_subdirectory order is bottom-up: a directory may only depend on ones
# listed above it.

include(GenerateExportHeader)

add_subdirectory(trace)
add_subdirectory(perf)
add_subdirectory(image)
add_subdirectory(text)
add_subdirectory(html)
add_subdirectory(css)

add_library(zui)
add_library(zui::zui ALIAS zui)

add_subdirectory(layout)
add_subdirectory(paint)
add_subdirectory(input)
add_subdirectory(backends)
add_subdirectory(app)

target_sources(zui PRIVATE
  ext/slots.cpp
  ext/builtin.cpp
  ext/plugin_facade.cpp
)

target_link_libraries(zui PRIVATE
  zui_src_trace
  zui_image
  zui_src_svg_gpu
  zui_src_text
  zui_src_html
  zui_src_css
  zui_src_layout
  zui_src_paint
  zui_src_input
  zui_src_backends
  zui_src_app
)
# zui_src_perf is INTERFACE-linked from zui_src_layout (and from kernel tests
# that skip layout). Linking it here as well would compile perf_budget.cpp twice.

if(ZUI_PROBE)
  target_compile_definitions(zui PRIVATE ZUI_PROBE=1 ZUI_PROBE_FRAME=1)
else()
  target_compile_definitions(zui PRIVATE ZUI_PROBE=0 ZUI_PROBE_FRAME=0)
endif()

if(ZUI_WITH_LEXBOR)
  target_link_libraries(zui PRIVATE lexbor_static)
  target_compile_definitions(zui PRIVATE ZUI_WITH_LEXBOR=1)
endif()
if(ZUI_WITH_FREETYPE)
  target_link_libraries(zui PRIVATE freetype)
  if(ZUI_WITH_HARFBUZZ)
    target_link_libraries(zui PRIVATE harfbuzz)
    target_compile_definitions(zui PRIVATE ZUI_WITH_HARFBUZZ=1)
  endif()
endif()
if(ZUI_WITH_SDL)
  target_link_libraries(zui PRIVATE SDL3::SDL3)
  target_compile_definitions(zui PRIVATE ZUI_WITH_SDL=1)
endif()
if(ZUI_WITH_GLFW)
  target_link_libraries(zui PRIVATE glfw)
  target_compile_definitions(zui PRIVATE ZUI_WITH_GLFW=1)
endif()
if(ANDROID)
  # NDK libvulkan.so. The host Vulkan SDK import library is a Windows .lib
  # and must not be linked into this .so.
  target_link_libraries(zui PRIVATE vulkan)
  target_compile_definitions(zui PRIVATE ZUI_HAS_VULKAN=1)
else()
  find_package(Vulkan QUIET)
  if(Vulkan_FOUND)
    target_link_libraries(zui PRIVATE Vulkan::Vulkan)
    target_compile_definitions(zui PRIVATE ZUI_HAS_VULKAN=1)
  endif()
endif()
if(WIN32)
  target_link_libraries(zui PRIVATE opengl32 comdlg32 ole32 shell32 uuid user32 advapi32 winhttp imm32)
endif()

# The Perfetto writer. Only the targets that opt in link it: everything else
# compiles source/trace/trace.h to inline no-ops, which keeps the SDK out of
# the layout test binaries.
if(ZUI_WITH_PERFETTO AND TARGET zui_perfetto)
  target_link_libraries(zui PRIVATE zui_perfetto)
  target_compile_definitions(zui PRIVATE ZUI_TRACE_PERFETTO=1)
endif()

# Debug / RelWithDebInfo only. Release leaves ZUI_WITH_SPDLOG unset, and
# source/log/log.h expands to empty macros without including spdlog.
zui_enable_spdlog(zui)

target_include_directories(zui
  PUBLIC
    "$<BUILD_INTERFACE:${ZUI_PUBLIC_INCLUDE_DIR}>"
    "$<BUILD_INTERFACE:${ZUI_GENERATED_INCLUDE_DIR}>"
    "$<INSTALL_INTERFACE:include>"
  PRIVATE
    "${ZUI_ROOT}/source"
)
zui_bind_source_dir(zui)

generate_export_header(zui
  BASE_NAME ZUI
  EXPORT_FILE_NAME "${ZUI_GENERATED_INCLUDE_DIR}/zui/export.h"
)

set_target_properties(zui PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
)
