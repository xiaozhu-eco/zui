add_library(zui_writer_session OBJECT
  cpp/session.cpp
  cpp/views.cpp
  cpp/edits.cpp
  cpp/storage.cpp
  cpp/store.cpp
  cpp/desk.cpp
  cpp/textutil.cpp
  cpp/rows.cpp
)
target_include_directories(zui_writer_session PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/cpp"
  "${ZUI_PUBLIC_INCLUDE_DIR}"
  "${ZUI_GENERATED_INCLUDE_DIR}"
)
target_link_libraries(zui_writer_session PUBLIC zui nlohmann_json::nlohmann_json)

if(ZUI_BUILD_EXAMPLES)
  zui_add_app(zui_writer SESSION zui_writer_session SOURCES main.cpp)
  add_custom_command(TARGET zui_writer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/pages"
      "$<TARGET_FILE_DIR:zui_writer>/pages"
  )
endif()
