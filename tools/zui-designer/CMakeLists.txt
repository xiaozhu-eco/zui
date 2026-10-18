# Browser acceptance uses tools/zui-designer/js. The native target does not copy it.

add_executable(zui_designer
  cpp/main.cpp
  cpp/document.cpp
  cpp/codegen.cpp
  cpp/publish.cpp
  cpp/session.cpp
)
target_include_directories(zui_designer PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/cpp")
target_link_libraries(zui_designer PRIVATE zui nlohmann_json::nlohmann_json)
set_target_properties(zui_designer PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/designer"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/designer"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/designer"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/designer"
  RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/designer"
)
# zui.dll loads these plugins at process start. They have to sit beside the
# designer or Windows never reaches main, missing page or not.
add_custom_command(TARGET zui_designer POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "$<TARGET_FILE:zui>"
    "$<TARGET_FILE:zui_plugin_clipboard>"
    "$<TARGET_FILE:zui_plugin_gamepad>"
    "$<TARGET_FILE:zui_plugin_image>"
    "$<TARGET_FILE:zui_plugin_pick>"
    "$<TARGET_FILE:zui_plugin_shell>"
    "$<TARGET_FILE:zui_plugin_tasks>"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/index.html"
    "$<TARGET_FILE_DIR:zui_designer>"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/css"
    "$<TARGET_FILE_DIR:zui_designer>/css"
)

# SDK tools share one directory with zui_create. This is not install(TARGETS zui).
install(TARGETS zui_designer RUNTIME DESTINATION tools COMPONENT zui_tools)
install(FILES
  "$<TARGET_FILE:zui>"
  "$<TARGET_FILE:zui_plugin_clipboard>"
  "$<TARGET_FILE:zui_plugin_gamepad>"
  "$<TARGET_FILE:zui_plugin_image>"
  "$<TARGET_FILE:zui_plugin_pick>"
  "$<TARGET_FILE:zui_plugin_shell>"
  "$<TARGET_FILE:zui_plugin_tasks>"
  "${CMAKE_CURRENT_SOURCE_DIR}/ui/index.html"
  DESTINATION tools
  COMPONENT zui_tools)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/ui/css" DESTINATION tools COMPONENT zui_tools)
