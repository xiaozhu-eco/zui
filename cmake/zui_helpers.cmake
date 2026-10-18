# Project-local CMake helpers. Included once from the root CMakeLists.
# Target recipes live next to their sources; this file only holds the shared
# functions so each directory CMakeLists.txt stays a list of targets.

include_guard(GLOBAL)

include(CMakeParseArguments)

function(zui_bind_source_dir target)
  target_compile_definitions(${target} PRIVATE "ZUI_SOURCE_DIR=\"${ZUI_ROOT_FWD}\"")
endfunction()

function(zui_gtest target)
  zui_bind_source_dir(${target})
  target_link_libraries(${target} PRIVATE GTest::gmock_main)
  gtest_discover_tests(${target}
    DISCOVERY_MODE PRE_TEST
    PROPERTIES TIMEOUT 120
  )
endfunction()

# INTERFACE library whose INTERFACE sources are compiled into each consumer.
# Paths are made absolute here (CMake requires that for INTERFACE sources).
function(zui_add_interface_sources name)
  add_library(${name} INTERFACE)
  set(_abs)
  foreach(src IN LISTS ARGN)
    if(IS_ABSOLUTE "${src}")
      list(APPEND _abs "${src}")
    else()
      list(APPEND _abs "${CMAKE_CURRENT_SOURCE_DIR}/${src}")
    endif()
  endforeach()
  target_sources(${name} INTERFACE ${_abs})
endfunction()

# Link the private spdlog build into one binary. No-op in Release, where
# zui_spdlog does not exist. Each binary gets its own log.cpp: the shared
# zui library does not export those symbols, so a kernel executable that
# compiles engine sources itself cannot call into the DLL's copy.
function(zui_enable_spdlog tgt)
  if(NOT ZUI_WITH_SPDLOG OR NOT TARGET zui_spdlog)
    return()
  endif()
  target_sources(${tgt} PRIVATE "${ZUI_ROOT}/source/log/log.cpp")
  target_link_libraries(${tgt} PRIVATE zui_spdlog)
  target_compile_definitions(${tgt} PRIVATE ZUI_WITH_SPDLOG=1)
endfunction()

# Common include / vendor wiring for kernel-plane binaries that compile
# engine translation units themselves instead of linking the zui shared lib.
function(zui_configure_kernel_target tgt)
  cmake_parse_arguments(ARG "LEXBOR;FREETYPE;IMAGE;WINHTTP;PERFETTO" "" "" ${ARGN})
  target_include_directories(${tgt} PRIVATE
    "${ZUI_PUBLIC_INCLUDE_DIR}"
    "${ZUI_GENERATED_INCLUDE_DIR}"
    "${ZUI_ROOT}/source"
  )
  if(ARG_LEXBOR)
    target_compile_definitions(${tgt} PRIVATE LEXBOR_STATIC)
    if(ZUI_WITH_LEXBOR)
      target_link_libraries(${tgt} PRIVATE lexbor_static)
    endif()
  endif()
  if(ARG_FREETYPE)
    if(ZUI_WITH_FREETYPE)
      target_link_libraries(${tgt} PRIVATE freetype)
      if(ZUI_WITH_HARFBUZZ)
        target_link_libraries(${tgt} PRIVATE harfbuzz)
        target_compile_definitions(${tgt} PRIVATE ZUI_WITH_HARFBUZZ=1)
      endif()
    endif()
  endif()
  if(ARG_IMAGE)
    target_sources(${tgt} PRIVATE "${ZUI_ROOT}/tests/image_boot.cpp")
    target_link_libraries(${tgt} PRIVATE zui_image zui_plugin_image)
    if(ZUI_PLUGINS_ARE_SHARED)
      target_compile_definitions(${tgt} PRIVATE ZUI_PLUGIN_SHARED=1)
    endif()
  endif()
  if(ARG_WINHTTP AND WIN32)
    target_link_libraries(${tgt} PRIVATE winhttp)
  endif()
  if(ARG_PERFETTO AND ZUI_WITH_PERFETTO AND TARGET zui_perfetto)
    target_link_libraries(${tgt} PRIVATE zui_perfetto)
    target_compile_definitions(${tgt} PRIVATE ZUI_TRACE_PERFETTO=1)
  endif()
  zui_enable_spdlog(${tgt})
endfunction()

# Kernel-plane executable. MODULES are zui_src_* INTERFACE source collections
# defined under source/<module>/CMakeLists.txt.
function(zui_add_kernel_executable name)
  cmake_parse_arguments(ARG
    "LEXBOR;FREETYPE;IMAGE;WINHTTP;PERFETTO;GTEST"
    ""
    "SOURCES;MODULES"
    ${ARGN}
  )
  add_executable(${name} ${ARG_SOURCES})
  foreach(mod IN LISTS ARG_MODULES)
    target_link_libraries(${name} PRIVATE zui_src_${mod})
  endforeach()
  set(_wire)
  if(ARG_LEXBOR)
    list(APPEND _wire LEXBOR)
  endif()
  if(ARG_FREETYPE)
    list(APPEND _wire FREETYPE)
  endif()
  if(ARG_IMAGE)
    list(APPEND _wire IMAGE)
  endif()
  if(ARG_WINHTTP)
    list(APPEND _wire WINHTTP)
  endif()
  if(ARG_PERFETTO)
    list(APPEND _wire PERFETTO)
  endif()
  zui_configure_kernel_target(${name} ${_wire})
  if(ARG_GTEST)
    zui_gtest(${name})
  endif()
endfunction()

# Shared-output-dir example apps (contacts / chat / calendar / kanban / album).
# Each ships its own index.html, so the binary lives in CMAKE_BINARY_DIR/<folder>.
function(zui_add_app name)
  cmake_parse_arguments(APP "" "SESSION" "SOURCES" ${ARGN})
  add_executable(${name} ${APP_SOURCES})
  string(REGEX REPLACE "^zui_" "" folder ${name})
  if(APP_SESSION)
    target_link_libraries(${name} PRIVATE ${APP_SESSION})
  endif()
  target_include_directories(${name} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/cpp")
  target_link_libraries(${name} PRIVATE zui nlohmann_json::nlohmann_json)
  set_target_properties(${name} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${folder}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/${folder}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/${folder}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/${folder}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/${folder}"
  )
  add_custom_command(TARGET ${name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:zui>"
      "${CMAKE_CURRENT_SOURCE_DIR}/index.html"
      "$<TARGET_FILE_DIR:${name}>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_RUNTIME_DLLS:${name}>"
      "$<TARGET_FILE_DIR:${name}>"
    COMMAND_EXPAND_LISTS
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/css"
      "$<TARGET_FILE_DIR:${name}>/css"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/js"
      "$<TARGET_FILE_DIR:${name}>/js"
  )
endfunction()

# example/tests/* programs: one main.cpp plus a handful of sidecar assets.
function(zui_add_fixture_program name)
  cmake_parse_arguments(ARG "" "DIR" "ASSETS" ${ARGN})
  add_executable(${name} "${ARG_DIR}/main.cpp")
  target_link_libraries(${name} PRIVATE zui)
  add_custom_command(TARGET ${name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      ${ARG_ASSETS}
      "$<TARGET_FILE_DIR:${name}>"
  )
endfunction()
