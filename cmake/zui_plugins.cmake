# Plugin linkage. AUTO follows BUILD_SHARED_LIBS. ON forces every plugin shared.
# OFF forces every plugin static. The core `zui` target keeps BUILD_SHARED_LIBS.
# Documented in docs/ARCHITECTURE.md.

include_guard(GLOBAL)

set(ZUI_PLUGIN_SHARED "AUTO" CACHE STRING
  "Plugin linkage: AUTO follows BUILD_SHARED_LIBS, ON shared, OFF static")
set_property(CACHE ZUI_PLUGIN_SHARED PROPERTY STRINGS AUTO ON OFF)

function(zui_plugin_linkage out_var)
  set(_mode "${ZUI_PLUGIN_SHARED}")
  if(_mode STREQUAL "OFF" OR _mode STREQUAL "0" OR _mode STREQUAL "FALSE")
    set(${out_var} STATIC PARENT_SCOPE)
    set(ZUI_PLUGINS_ARE_SHARED OFF PARENT_SCOPE)
  elseif(_mode STREQUAL "ON" OR _mode STREQUAL "1" OR _mode STREQUAL "TRUE")
    set(${out_var} SHARED PARENT_SCOPE)
    set(ZUI_PLUGINS_ARE_SHARED ON PARENT_SCOPE)
  elseif(BUILD_SHARED_LIBS)
    set(${out_var} SHARED PARENT_SCOPE)
    set(ZUI_PLUGINS_ARE_SHARED ON PARENT_SCOPE)
  else()
    set(${out_var} STATIC PARENT_SCOPE)
    set(ZUI_PLUGINS_ARE_SHARED OFF PARENT_SCOPE)
  endif()
endfunction()

# zui_add_plugin(<directory> [sources...])
# Library name is zui_plugin_<directory>. The directory name is the capability,
# with no zui_ or plugin prefix.
function(zui_add_plugin directory)
  zui_plugin_linkage(_kind)
  set(_target "zui_plugin_${directory}")
  add_library(${_target} ${_kind} ${ARGN})
  add_library(zui::plugin::${directory} ALIAS ${_target})
  target_include_directories(${_target} PRIVATE
    "${ZUI_ROOT}"
    "${ZUI_ROOT}/source"
    "${ZUI_PUBLIC_INCLUDE_DIR}"
    "${ZUI_GENERATED_INCLUDE_DIR}"
  )
  target_compile_features(${_target} PRIVATE cxx_std_17)
  if(ZUI_PLUGINS_ARE_SHARED)
    # Register functions are exported with ZUI_PLUGIN_API. Keep the rest of a
    # shared plugin visible as well. The CPU SVG parser is not in that surface:
    # it is compiled into zui_image, and the image plugin calls Parser::parse.
    target_compile_definitions(${_target} PRIVATE ZUI_PLUGIN_SHARED=1 ZUI_PLUGIN_BUILD=1)
    set_target_properties(${_target} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
  endif()
  set_property(GLOBAL APPEND PROPERTY ZUI_PLUGIN_TARGETS ${_target})
endfunction()
