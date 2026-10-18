# Vendored sources in third_party/. Do not FetchContent / CPM / system packages
# for these libraries. Pattern matches xz-assistant/cmake/deps.cmake.

if(NOT DEFINED ZUI_ROOT)
  set(ZUI_ROOT ${CMAKE_SOURCE_DIR})
endif()
set(ZUI_THIRD_PARTY ${ZUI_ROOT}/third_party)

function(zui_need_dir dir)
  if(NOT EXISTS "${dir}")
    message(FATAL_ERROR "Missing vendored source: ${dir}\nSee third_party/README.md")
  endif()
endfunction()

option(ZUI_WITH_SDL "Build / link vendored SDL3" ON)
option(ZUI_WITH_GLFW "Build vendored GLFW (window backend)" ON)
option(ZUI_WITH_FREETYPE "Build vendored FreeType for glyph metrics and coverage bitmaps" ON)
option(ZUI_WITH_LEXBOR "Build vendored lexbor (HTML5 + CSS syntax)" ON)

# --- SDL3 (static on desktop, aligned with xz-assistant) ---
if(ZUI_WITH_SDL)
  zui_need_dir(${ZUI_THIRD_PARTY}/SDL)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
  set(SDL_INSTALL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_UNINSTALL OFF CACHE BOOL "" FORCE)
  set(SDL_ANDROID_JAR OFF CACHE BOOL "" FORCE)
  set(SDL_VENDOR_INFO "zui" CACHE STRING "" FORCE)
  set(SDL_SYSTEM_ICONV OFF CACHE BOOL "" FORCE)
  set(SDL_LIBICONV OFF CACHE BOOL "" FORCE)
  unset(ICONV_IN_LIBC CACHE)
  unset(ICONV_IN_LIBICONV CACHE)
  if(WIN32 OR APPLE OR UNIX)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
  endif()
  add_subdirectory(${ZUI_THIRD_PARTY}/SDL ${CMAKE_BINARY_DIR}/third_party/SDL EXCLUDE_FROM_ALL)
  if(NOT TARGET SDL3::SDL3)
    message(FATAL_ERROR "vendored SDL3 did not create SDL3::SDL3")
  endif()
endif()

if(ZUI_WITH_GLFW)
  zui_need_dir(${ZUI_THIRD_PARTY}/glfw)
  if(NOT EXISTS "${ZUI_THIRD_PARTY}/glfw/CMakeLists.txt")
    message(FATAL_ERROR "ZUI_WITH_GLFW=ON but third_party/glfw has no CMakeLists.txt yet")
  endif()
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
  # Clipboard text for GLFW lives in zui_plugin_clipboard and calls glfw.
  # A static glfw inside both zui and a shared plugin is two copies, and the
  # plugin copy never sees glfwInit. Share one image: shared glfw when plugins
  # are shared, static glfw absorbed into zui when plugins are static.
  set(_zui_plugin_mode "AUTO")
  if(DEFINED ZUI_PLUGIN_SHARED AND NOT "${ZUI_PLUGIN_SHARED}" STREQUAL "")
    set(_zui_plugin_mode "${ZUI_PLUGIN_SHARED}")
  endif()
  set(_zui_plugins_shared ON)
  if(_zui_plugin_mode STREQUAL "OFF" OR _zui_plugin_mode STREQUAL "0" OR _zui_plugin_mode STREQUAL "FALSE")
    set(_zui_plugins_shared OFF)
  elseif(_zui_plugin_mode STREQUAL "ON" OR _zui_plugin_mode STREQUAL "1" OR _zui_plugin_mode STREQUAL "TRUE")
    set(_zui_plugins_shared ON)
  elseif(NOT BUILD_SHARED_LIBS)
    set(_zui_plugins_shared OFF)
  endif()
  set(_zui_saved_shared_glfw ${BUILD_SHARED_LIBS})
  if(_zui_plugins_shared)
    set(BUILD_SHARED_LIBS ON)
  else()
    set(BUILD_SHARED_LIBS OFF)
  endif()
  add_subdirectory(${ZUI_THIRD_PARTY}/glfw ${CMAKE_BINARY_DIR}/third_party/glfw EXCLUDE_FROM_ALL)
  set(BUILD_SHARED_LIBS ${_zui_saved_shared_glfw})
endif()

# --- FreeType (static; coverage bitmaps via FT_LOAD_RENDER) ---
# FT_DISABLE_HARFBUZZ stays ON: FreeType's optional auto-hinter HB loop is separate from zui's
# hb_shape path below. Enabling both would introduce a configure-time FreeType鈫擧arfBuzz cycle.
if(ZUI_WITH_FREETYPE)
  zui_need_dir(${ZUI_THIRD_PARTY}/freetype)
  set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
  set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
  set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
  set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
  set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
  set(FT_ENABLE_ERROR_STRINGS ON CACHE BOOL "" FORCE)
  set(_zui_saved_shared_ft ${BUILD_SHARED_LIBS})
  set(BUILD_SHARED_LIBS OFF)
  add_subdirectory(${ZUI_THIRD_PARTY}/freetype ${CMAKE_BINARY_DIR}/third_party/freetype EXCLUDE_FROM_ALL)
  set(BUILD_SHARED_LIBS ${_zui_saved_shared_ft})
  if(NOT TARGET freetype)
    message(FATAL_ERROR "vendored FreeType did not create freetype target")
  endif()
endif()

# --- HarfBuzz (static; hb_shape + hb-ft for layout advances / kerning) ---
option(ZUI_WITH_HARFBUZZ "Build vendored HarfBuzz and shape text with hb_shape" ON)
if(ZUI_WITH_HARFBUZZ)
  if(NOT ZUI_WITH_FREETYPE)
    message(FATAL_ERROR "ZUI_WITH_HARFBUZZ=ON requires ZUI_WITH_FREETYPE=ON")
  endif()
  zui_need_dir(${ZUI_THIRD_PARTY}/harfbuzz)
  if(NOT EXISTS "${ZUI_THIRD_PARTY}/harfbuzz/CMakeLists.txt")
    message(FATAL_ERROR "ZUI_WITH_HARFBUZZ=ON but third_party/harfbuzz has no CMakeLists.txt\n"
                        "See third_party/harfbuzz/VENDOR.txt")
  endif()
  set(HB_HAVE_FREETYPE ON CACHE BOOL "" FORCE)
  set(HB_HAVE_GLIB OFF CACHE BOOL "" FORCE)
  set(HB_HAVE_ICU OFF CACHE BOOL "" FORCE)
  set(HB_HAVE_GRAPHITE2 OFF CACHE BOOL "" FORCE)
  set(HB_HAVE_CAIRO OFF CACHE BOOL "" FORCE)
  set(HB_HAVE_GOBJECT OFF CACHE BOOL "" FORCE)
  set(HB_HAVE_INTROSPECTION OFF CACHE BOOL "" FORCE)
  set(HB_BUILD_UTILS OFF CACHE BOOL "" FORCE)
  set(HB_BUILD_SUBSET OFF CACHE BOOL "" FORCE)
  if(WIN32)
    set(HB_HAVE_UNISCRIBE OFF CACHE BOOL "" FORCE)
    set(HB_HAVE_GDI OFF CACHE BOOL "" FORCE)
    set(HB_HAVE_DIRECTWRITE OFF CACHE BOOL "" FORCE)
  endif()
  set(_zui_saved_shared_hb ${BUILD_SHARED_LIBS})
  set(BUILD_SHARED_LIBS OFF)
  add_subdirectory(${ZUI_THIRD_PARTY}/harfbuzz ${CMAKE_BINARY_DIR}/third_party/harfbuzz EXCLUDE_FROM_ALL)
  set(BUILD_SHARED_LIBS ${_zui_saved_shared_hb})
  if(NOT TARGET harfbuzz)
    message(FATAL_ERROR "vendored HarfBuzz did not create harfbuzz target")
  endif()
endif()

# --- lexbor (static; HTML5 + CSS Syntax/Selectors) ---
if(ZUI_WITH_LEXBOR)
  zui_need_dir(${ZUI_THIRD_PARTY}/lexbor)
  set(LEXBOR_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(LEXBOR_BUILD_STATIC ON CACHE BOOL "" FORCE)
  set(LEXBOR_BUILD_SEPARATELY OFF CACHE BOOL "" FORCE)
  set(LEXBOR_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(LEXBOR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(LEXBOR_BUILD_TESTS_CPP OFF CACHE BOOL "" FORCE)
  set(LEXBOR_BUILD_UTILS OFF CACHE BOOL "" FORCE)
  set(LEXBOR_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
  set(LEXBOR_INSTALL_HEADERS OFF CACHE BOOL "" FORCE)
  set(LEXBOR_WITHOUT_THREADS ON CACHE BOOL "" FORCE)
  set(_zui_saved_shared_lx ${BUILD_SHARED_LIBS})
  set(BUILD_SHARED_LIBS OFF)
  add_subdirectory(${ZUI_THIRD_PARTY}/lexbor ${CMAKE_BINARY_DIR}/third_party/lexbor EXCLUDE_FROM_ALL)
  set(BUILD_SHARED_LIBS ${_zui_saved_shared_lx})
  if(NOT TARGET lexbor_static)
    message(FATAL_ERROR "vendored lexbor did not create lexbor_static")
  endif()
endif()

# --- nlohmann/json 3.11.3 (header-only; the difftest harness reads case/steps and writes
# trace/golden JSON). Vendored but previously unwired, so it was not visible to any target.
# Interface target only: header-only, nothing to build.
zui_need_dir(${ZUI_THIRD_PARTY}/nlohmann_json)
if(NOT TARGET nlohmann_json::nlohmann_json)
  add_library(zui_nlohmann_json INTERFACE)
  target_include_directories(zui_nlohmann_json INTERFACE
    "${ZUI_THIRD_PARTY}/nlohmann_json/include")
  add_library(nlohmann_json::nlohmann_json ALIAS zui_nlohmann_json)
endif()

# --- GoogleTest 1.18 (gtest + gmock, static, shared CRT) ---
zui_need_dir(${ZUI_THIRD_PARTY}/googletest)
set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(GTEST_HAS_ABSL OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(gtest_build_tests OFF CACHE BOOL "" FORCE)
set(gtest_build_samples OFF CACHE BOOL "" FORCE)
set(gmock_build_tests OFF CACHE BOOL "" FORCE)
set(_zui_saved_shared_gtest ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(${ZUI_THIRD_PARTY}/googletest ${CMAKE_BINARY_DIR}/third_party/googletest EXCLUDE_FROM_ALL)
set(BUILD_SHARED_LIBS ${_zui_saved_shared_gtest})
if(NOT TARGET GTest::gmock_main)
  message(FATAL_ERROR "vendored GoogleTest did not create GTest::gmock_main")
endif()

# --- Perfetto Tracing SDK (amalgamated client, Apache-2.0) ---
#
# Diagnostic instrumentation, not a dependency of the library: only the targets that opt in link
# it (see CMakeLists.txt). `source/trace/trace.h` compiles to no-ops everywhere else, which keeps
# the 8 MB header out of the layout test binaries and keeps their builds fast.
option(ZUI_WITH_PERFETTO "Build the vendored Perfetto Tracing SDK" ON)

if(ZUI_WITH_PERFETTO)
  zui_need_dir(${ZUI_THIRD_PARTY}/perfetto)
  if(NOT EXISTS "${ZUI_THIRD_PARTY}/perfetto/perfetto.cc")
    message(FATAL_ERROR "ZUI_WITH_PERFETTO=ON but third_party/perfetto/perfetto.cc is missing\n"
                        "See third_party/perfetto/VENDOR.txt")
  endif()
  find_package(Threads REQUIRED)
  add_library(zui_perfetto STATIC "${ZUI_THIRD_PARTY}/perfetto/perfetto.cc")
  target_include_directories(zui_perfetto SYSTEM PUBLIC "${ZUI_THIRD_PARTY}/perfetto")
  target_compile_features(zui_perfetto PUBLIC cxx_std_17)
  target_link_libraries(zui_perfetto PUBLIC Threads::Threads)
  if(WIN32)
    # The amalgamation is one very large translation unit. C4819 is the source-encoding warning
    # for the non-ASCII bytes in vendored upstream comments, which is not ours to fix.
    target_compile_options(zui_perfetto PRIVATE /bigobj /wd4819)
    # perfetto.h includes windows.h. These have to reach the consumers too, or the legacy `min` /
    # `max` macros leak into every file that includes trace.h.
    target_compile_definitions(zui_perfetto PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(zui_perfetto PUBLIC ws2_32)
  endif()
endif()

# --- zlib 1.3.2 + libpng 1.6.59 ---------------------------------------------
# PNG read/write. image::load picks a driver from the file suffix.
# No FetchContent: the sources live under third_party/.
#
# zlib's CMakeLists.txt `file(READ)`s ${zlib_SOURCE_DIR}/zconf.h. The git
# snapshot gitignores that file; the release tarball ships it as a copy of
# zconf.h.in. third_party/zlib/zconf.h is that tarball file.
zui_need_dir(${ZUI_THIRD_PARTY}/zlib)
zui_need_dir(${ZUI_THIRD_PARTY}/libpng)
if(NOT EXISTS "${ZUI_THIRD_PARTY}/zlib/zconf.h")
  message(FATAL_ERROR
    "Missing ${ZUI_THIRD_PARTY}/zlib/zconf.h\n"
    "The zlib release tarball ships this file (copy of zconf.h.in).\n"
    "See third_party/zlib/VENDOR.txt")
endif()

set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
set(_zui_saved_shared_zlib ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(${ZUI_THIRD_PARTY}/zlib ${CMAKE_BINARY_DIR}/third_party/zlib EXCLUDE_FROM_ALL)
set(BUILD_SHARED_LIBS ${_zui_saved_shared_zlib})
if(NOT TARGET zlibstatic)
  message(FATAL_ERROR "vendored zlib did not create zlibstatic")
endif()

# libpng calls find_package(ZLIB). CMake's FindZLIB looks for an installed
# library, so point it at the static target from the subdirectory above.
set(_zui_find_zlib_dir "${CMAKE_BINARY_DIR}/third_party/cmake")
file(MAKE_DIRECTORY "${_zui_find_zlib_dir}")
file(WRITE "${_zui_find_zlib_dir}/FindZLIB.cmake"
"if(NOT TARGET ZLIB::ZLIB)
  add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif()
set(ZLIB_FOUND TRUE)
set(ZLIB_LIBRARY ZLIB::ZLIB)
set(ZLIB_LIBRARIES ZLIB::ZLIB)
set(ZLIB_INCLUDE_DIR \"${CMAKE_BINARY_DIR}/third_party/zlib\")
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ZLIB REQUIRED_VARS ZLIB_LIBRARY ZLIB_INCLUDE_DIR)
")
list(INSERT CMAKE_MODULE_PATH 0 "${_zui_find_zlib_dir}")

set(PNG_SHARED OFF CACHE BOOL "" FORCE)
set(PNG_STATIC ON CACHE BOOL "" FORCE)
set(PNG_FRAMEWORK OFF CACHE BOOL "" FORCE)
set(PNG_TESTS OFF CACHE BOOL "" FORCE)
set(PNG_TOOLS OFF CACHE BOOL "" FORCE)
unset(PNG_EXECUTABLES CACHE)
set(PNG_LIBCONF_HEADER "${ZUI_THIRD_PARTY}/libpng/scripts/pnglibconf.h.prebuilt" CACHE FILEPATH "" FORCE)
set(_zui_saved_shared_png ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
# CMake 4 rejects install(EXPORT) of png_static because zlibstatic is not in
# that export set. zui does not install these vendored libraries.
set(SKIP_INSTALL_ALL ON)
add_subdirectory(${ZUI_THIRD_PARTY}/libpng ${CMAKE_BINARY_DIR}/third_party/libpng EXCLUDE_FROM_ALL)
unset(SKIP_INSTALL_ALL)
set(BUILD_SHARED_LIBS ${_zui_saved_shared_png})
if(NOT TARGET png_static)
  message(FATAL_ERROR "vendored libpng did not create png_static")
endif()

# --- libjpeg-turbo 3.2.0 ----------------------------------------------------
# JPEG read/write, picked by the file suffix like PNG.
#
# libjpeg-turbo is the one vendored dependency that cannot come in through
# add_subdirectory(): its own CMakeLists.txt rejects that on purpose and points
# downstream projects at ExternalProject_Add() (see the banner at the top of
# third_party/libjpeg-turbo/CMakeLists.txt).  Take that route rather than patch
# the vendored tree: configure and build it once as its own project into a
# private prefix under the build tree, then import the resulting static archive
# as the `jpeg-static` target the rest of the build links.
#
# SIMD stays off because that path needs NASM, which zui does not require.
# WITH_CRT_DLL keeps the DLL C run-time (/MD in Release, /MDd in Debug) so the
# archive matches the run-time zui itself links against.
zui_need_dir(${ZUI_THIRD_PARTY}/libjpeg-turbo)
include(ExternalProject)

set(_zui_jpeg_src "${ZUI_THIRD_PARTY}/libjpeg-turbo")
set(_zui_jpeg_build "${CMAKE_BINARY_DIR}/third_party/libjpeg-turbo-build")
set(_zui_jpeg_prefix "${CMAKE_BINARY_DIR}/third_party/libjpeg-turbo")
# MSVC keeps the target name; every other toolchain renames the output to `jpeg`.
if(MSVC)
  set(_zui_jpeg_archive "${_zui_jpeg_prefix}/lib/jpeg-static.lib")
else()
  set(_zui_jpeg_archive "${_zui_jpeg_prefix}/lib/libjpeg.a")
endif()
# An imported target may only carry include directories that exist at configure
# time, but the install that creates them runs at build time.
file(MAKE_DIRECTORY "${_zui_jpeg_prefix}/include")

set(_zui_jpeg_cmake_args
  # libjpeg-turbo treats a blank CMAKE_INSTALL_DOCDIR as a fatal configure error.
  "-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>"
  "-DCMAKE_INSTALL_DOCDIR=doc"
  "-DENABLE_SHARED=0"
  "-DENABLE_STATIC=1"
  "-DWITH_SIMD=0"
  "-DWITH_TURBOJPEG=0"
  "-DWITH_TOOLS=0"
  "-DWITH_TESTS=0"
  "-DWITH_CRT_DLL=1"
)
if(CMAKE_BUILD_TYPE)
  list(APPEND _zui_jpeg_cmake_args "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
endif()
if(CMAKE_POSITION_INDEPENDENT_CODE)
  list(APPEND _zui_jpeg_cmake_args "-DCMAKE_POSITION_INDEPENDENT_CODE=ON")
endif()
# A cross compile (Android) must use the same toolchain. Passing only the
# compiler path drops --target and the sysroot, and the archive is the wrong ABI.
if(CMAKE_TOOLCHAIN_FILE)
  file(TO_CMAKE_PATH "${CMAKE_TOOLCHAIN_FILE}" _zui_jpeg_toolchain)
  list(APPEND _zui_jpeg_cmake_args "-DCMAKE_TOOLCHAIN_FILE=${_zui_jpeg_toolchain}")
elseif(CMAKE_C_COMPILER)
  list(APPEND _zui_jpeg_cmake_args "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}")
endif()

ExternalProject_Add(zui_libjpeg_turbo
  SOURCE_DIR "${_zui_jpeg_src}"
  BINARY_DIR "${_zui_jpeg_build}"
  INSTALL_DIR "${_zui_jpeg_prefix}"
  CMAKE_ARGS ${_zui_jpeg_cmake_args}
  BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config $<CONFIG> --target jpeg-static
  INSTALL_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config $<CONFIG> --target install
  BUILD_BYPRODUCTS "${_zui_jpeg_archive}"
  # The vendored sources are a fixed snapshot, and the directory is part of the
  # zui checkout rather than its own clone.
  UPDATE_COMMAND ""
)

add_library(jpeg-static STATIC IMPORTED)
set_target_properties(jpeg-static PROPERTIES
  IMPORTED_LOCATION "${_zui_jpeg_archive}"
  INTERFACE_INCLUDE_DIRECTORIES "${_zui_jpeg_prefix}/include")
add_dependencies(jpeg-static zui_libjpeg_turbo)

# --- spdlog 1.15.1 (internal debug log) ------------------------------------
# Compiled only for Debug and RelWithDebInfo. Release and MinSizeRel do not
# configure the target, so the sources are not built.
#
# The target is zui_spdlog, not spdlog::spdlog, so an application CMake that
# also vendors spdlog does not collide on the target name. The sources are
# compiled into namespaces zui_spdlog and zui_fmt (see source/log/log.h) and
# the logger there is never registered with spdlog's process-wide registry.
string(TOUPPER "${CMAKE_BUILD_TYPE}" _zui_spdlog_build_type)
set(ZUI_WITH_SPDLOG OFF)
if(_zui_spdlog_build_type STREQUAL "DEBUG" OR _zui_spdlog_build_type STREQUAL "RELWITHDEBINFO")
  set(ZUI_WITH_SPDLOG ON)
endif()

if(ZUI_WITH_SPDLOG)
  zui_need_dir(${ZUI_THIRD_PARTY}/spdlog)
  if(NOT EXISTS "${ZUI_THIRD_PARTY}/spdlog/src/spdlog.cpp")
    message(FATAL_ERROR "ZUI spdlog is on but third_party/spdlog/src/spdlog.cpp is missing")
  endif()
  find_package(Threads REQUIRED)
  set(_zui_spdlog_root "${ZUI_THIRD_PARTY}/spdlog")
  add_library(zui_spdlog STATIC
    "${_zui_spdlog_root}/src/spdlog.cpp"
    "${_zui_spdlog_root}/src/stdout_sinks.cpp"
    "${_zui_spdlog_root}/src/color_sinks.cpp"
    "${_zui_spdlog_root}/src/file_sinks.cpp"
    "${_zui_spdlog_root}/src/async.cpp"
    "${_zui_spdlog_root}/src/cfg.cpp"
    "${_zui_spdlog_root}/src/bundled_fmtlib_format.cpp"
  )
  target_include_directories(zui_spdlog SYSTEM PUBLIC "${_zui_spdlog_root}/include")
  target_compile_definitions(zui_spdlog PRIVATE
    SPDLOG_COMPILED_LIB
    SPDLOG_DISABLE_DEFAULT_LOGGER
    spdlog=zui_spdlog
    fmt=zui_fmt
  )
  target_link_libraries(zui_spdlog PUBLIC Threads::Threads)
  set_target_properties(zui_spdlog PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    POSITION_INDEPENDENT_CODE ON
  )
  message(STATUS "zui spdlog: ON (${CMAKE_BUILD_TYPE})")
else()
  message(STATUS "zui spdlog: OFF (${CMAKE_BUILD_TYPE})")
endif()

# --- minizip (unzip only) from vendored zlib contrib ----------------
# Used by zui_reader EPUB. NOCRYPT/NOUNCRYPT: no password ZIP support.
if(NOT TARGET zui_minizip)
  set(_zui_minizip_dir "${ZUI_THIRD_PARTY}/zlib/contrib/minizip")
  if(NOT EXISTS "${_zui_minizip_dir}/unzip.c")
    message(FATAL_ERROR "vendored minizip missing: ${_zui_minizip_dir}/unzip.c")
  endif()
  add_library(zui_minizip STATIC
    "${_zui_minizip_dir}/unzip.c"
    "${_zui_minizip_dir}/ioapi.c"
  )
  target_include_directories(zui_minizip PUBLIC "${_zui_minizip_dir}")
  target_compile_definitions(zui_minizip PRIVATE NOCRYPT NOUNCRYPT)
  target_link_libraries(zui_minizip PUBLIC zlibstatic)
  set_target_properties(zui_minizip PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden
  )
endif()

# --- pugixml (static; EPUB OPF/NCX/XHTML) ---------------------------
if(NOT TARGET pugixml AND NOT TARGET pugixml-static)
  zui_need_dir(${ZUI_THIRD_PARTY}/pugixml)
  set(PUGIXML_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(_zui_saved_shared_pugi ${BUILD_SHARED_LIBS})
  set(BUILD_SHARED_LIBS OFF)
  add_subdirectory(${ZUI_THIRD_PARTY}/pugixml ${CMAKE_BINARY_DIR}/third_party/pugixml EXCLUDE_FROM_ALL)
  set(BUILD_SHARED_LIBS ${_zui_saved_shared_pugi})
endif()
if(TARGET pugixml-static)
  add_library(zui_pugixml ALIAS pugixml-static)
elseif(TARGET pugixml)
  add_library(zui_pugixml ALIAS pugixml)
else()
  message(FATAL_ERROR "vendored pugixml did not create pugixml or pugixml-static")
endif()

