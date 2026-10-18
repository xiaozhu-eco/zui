# SDK tool. It does not link zui. zui-pack reads a zui_create project and writes
# packages. The installer stub is a separate binary that sits next to zui-pack.

add_executable(zui_pack
  main.cpp
  windows_package.cpp
  packer_linux.cpp
  packer_apple.cpp
  android.cpp)
target_include_directories(zui_pack PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../common")
target_link_libraries(zui_pack PRIVATE zui_tool_common)
set_target_properties(zui_pack PROPERTIES
  OUTPUT_NAME "zui-pack"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/designer"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/designer"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/designer"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/designer"
  RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/designer"
)
if(MSVC)
  target_compile_options(zui_pack PRIVATE /utf-8)
endif()

if(WIN32)
  add_executable(zui_setup_stub
    installer_main.cpp
    setup_ui.cpp)
  target_include_directories(zui_setup_stub PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../common")
  target_link_libraries(zui_setup_stub PRIVATE zui_tool_common)
  set_target_properties(zui_setup_stub PROPERTIES
    WIN32_EXECUTABLE TRUE
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/designer"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/designer"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/designer"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/designer"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/designer"
  )
  if(MSVC)
    target_compile_options(zui_setup_stub PRIVATE /utf-8)
  endif()
  # Renders the installer screens to PNG files for design review. It shares
  # setup_ui with the installer, so the images are the window's client area.
  # Not installed: no package uses it.
  add_executable(zui_setup_preview
    setup_preview_main.cpp
    setup_ui.cpp)
  target_include_directories(zui_setup_preview PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../common")
  set_target_properties(zui_setup_preview PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/designer"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/designer"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/designer"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/designer"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/designer"
  )
  if(MSVC)
    target_compile_options(zui_setup_preview PRIVATE /utf-8)
  endif()
  add_dependencies(zui_pack zui_setup_stub)
  install(TARGETS zui_pack zui_setup_stub RUNTIME DESTINATION tools COMPONENT zui_tools)

  add_executable(zui_pack_cli_test
    "${CMAKE_SOURCE_DIR}/tests/tools/pack_cli_test.cpp")
  target_include_directories(zui_pack_cli_test PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../common"
    "${CMAKE_SOURCE_DIR}/include")
  target_link_libraries(zui_pack_cli_test PRIVATE GTest::gmock_main zui_tool_common)
  # Version information is what the Windows metadata test reads back.
  target_link_libraries(zui_pack_cli_test PRIVATE version)
  if(MSVC)
    target_compile_options(zui_pack_cli_test PRIVATE /utf-8)
  endif()
  add_dependencies(zui_pack_cli_test zui_create zui_pack zui_setup_stub zui_setup_preview)
  file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/pack_cli_paths.h" CONTENT
"#pragma once
#define ZUI_CREATE_EXE R\"($<TARGET_FILE:zui_create>)\"
#define ZUI_PACK_EXE R\"($<TARGET_FILE:zui_pack>)\"
#define ZUI_SETUP_PREVIEW_EXE R\"($<TARGET_FILE:zui_setup_preview>)\"
#define ZUI_ROOT_DIR R\"(${ZUI_ROOT})\"
#define ZUI_BUILD_DIR R\"(${CMAKE_BINARY_DIR})\"
")
  include(GoogleTest)
  gtest_discover_tests(zui_pack_cli_test
    DISCOVERY_MODE PRE_TEST
    PROPERTIES TIMEOUT 600
  )
else()
  install(TARGETS zui_pack RUNTIME DESTINATION tools COMPONENT zui_tools)
endif()
