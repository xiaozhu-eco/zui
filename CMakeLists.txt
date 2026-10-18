cmake_minimum_required(VERSION 3.21)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The Windows preset is clang-cl, and this block is one of the reasons.
#
# MSVC prints the dependency note of `/showIncludes` in the machine's UI language, and ninja
# recognizes the note by the `msvc_deps_prefix` CMake probes for at configure time. Measured on
# VS 18 / cl 19.51 with a zh-CN console, the prefix CMake recorded in `rules.ninja` was the mojibake
# of the compiler's own output, so *no* object recorded header dependencies: `ninja -C <dir> -t deps
# <object>` showed `#deps 0`, a change to a widely included header never triggered a rebuild, and the
# resulting mixed objects disagreed about an ABI (`LayoutStyle`, `Model`, `detail.h` structs). Two
# such mismatches surfaced as an access violation far from the edit, after a fresh build directory
# still did not help. clang-cl prints the note as ASCII (`Note: including file: `) unconditionally,
# so that path records real dependencies (`layout/engine.cpp.obj: #deps 65, VALID`). The pin stays
# below for a machine that still configures with real cl.exe.
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  set(ENV{VSLANG} 1033)
endif()

project(zui VERSION 0.1.0 LANGUAGES C CXX)

# The same language pin for the compile steps: `cmake -E env` keeps cl.exe's note ASCII whatever
# the caller's shell looks like, so the prefix CMake recorded above always matches. clang-cl needs
# neither the pin nor the extra process it costs per translation unit.
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
  set(CMAKE_C_COMPILER_LAUNCHER "${CMAKE_COMMAND};-E;env;VSLANG=1033")
  set(CMAKE_CXX_COMPILER_LAUNCHER "${CMAKE_COMMAND};-E;env;VSLANG=1033")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Every source file in the tree is UTF-8: the comments carry `…`, `→`, `§` and a few Chinese file
# names. Without `/utf-8` MSVC reads each one under the machine's code page, reports C4819, and a
# multi-byte sequence whose last byte is `\` swallows the next character. One flag for the whole
# tree beats repeating it per target (and forgetting a target is exactly how the warning came back).
# clang-cl reads sources as UTF-8 on its own, but `MSVC` is true for that driver too, so the flag
# reaches both compilers and the two flag sets stay identical.
if(MSVC)
  add_compile_options(/utf-8)
endif()

set(ZUI_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
list(APPEND CMAKE_MODULE_PATH ${ZUI_ROOT}/cmake)

option(BUILD_SHARED_LIBS "Build ZUI libraries as shared" ON)
option(ZUI_BUILD_EXAMPLES "Build example programs" ON)
option(ZUI_BUILD_BINDINGS "Build the C ABI and its C conformance test" ON)
# Test probes live in source/ and stay out of include/zui/. Release product builds leave them
# off. Debug (and any configure that does not say Release) leaves them on. See docs/TESTING.md.
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  set(_ZUI_PROBE_DEFAULT OFF)
else()
  set(_ZUI_PROBE_DEFAULT ON)
endif()
option(ZUI_PROBE "Compile source test probes. Not an application API." ${_ZUI_PROBE_DEFAULT})

include(deps)
include(zui_runtime)
include(zui_helpers)
include(zui_plugins)
include(GoogleTest)
enable_testing()
file(TO_CMAKE_PATH "${ZUI_ROOT}" ZUI_ROOT_FWD)

set(ZUI_PUBLIC_INCLUDE_DIR "${ZUI_ROOT}/include")
set(ZUI_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated/include")
file(MAKE_DIRECTORY "${ZUI_GENERATED_INCLUDE_DIR}")

# add_subdirectory changes CMAKE_CURRENT_BINARY_DIR. Keep DLLs and test
# executables in the top-level build dir so Windows loads zui.dll next to
# zui_host / zui_app_interact (CTest PRE_TEST discovery runs those binaries).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_subdirectory(source)
add_subdirectory(plugins)
# Example session OBJECT libraries are required by zui_app_interact even when
# the example executables themselves are off.
add_subdirectory(example)
add_subdirectory(tests)
if(ZUI_BUILD_EXAMPLES)
  add_subdirectory(tools/zui-designer)
  # What zui_create and zui-pack share.
  add_subdirectory(tools/common)
  add_subdirectory(tools/zui_create)
  add_subdirectory(tools/zui_pack)
endif()
if(ZUI_BUILD_BINDINGS)
  add_subdirectory(bindings)
endif()
