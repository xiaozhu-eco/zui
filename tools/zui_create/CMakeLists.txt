# SDK tool. It does not link zui. The binary sits next to zui_designer and installs
# into the same tools/ directory.

add_executable(zui_create
  main.cpp
  cli.cpp)
target_include_directories(zui_create PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(zui_create PRIVATE zui_tool_common)
set_target_properties(zui_create PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/zui_create"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/zui_create"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/zui_create"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/zui_create"
  RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/zui_create"
)
if(MSVC)
  target_compile_options(zui_create PRIVATE /utf-8)
endif()

install(TARGETS zui_create RUNTIME DESTINATION tools COMPONENT zui_tools)
