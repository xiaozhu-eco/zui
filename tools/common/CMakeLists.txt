# What zui_create and zui-pack share: the project declaration and its reader,
# the file templates, the terminal style, and the packaging primitives (zip,
# PE images, process calls, signing). Both tools link this; neither owns it.

add_library(zui_tool_common STATIC
  project.cpp
  template.cpp
  project_files.cpp
  style.cpp
  subprocess.cpp
  zip.cpp
  pe.cpp
  elf.cpp
  md5.cpp
  archive.cpp
  apple_bundle.cpp
  package_text.cpp
  sign.cpp
  icon.cpp)

target_include_directories(zui_tool_common PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(zui_tool_common PUBLIC ZLIB::ZLIB)
if(WIN32)
  # The executable and launcher icons are drawn with GDI+.
  target_link_libraries(zui_tool_common PUBLIC gdiplus)
endif()
if(MSVC)
  target_compile_options(zui_tool_common PRIVATE /utf-8)
endif()
