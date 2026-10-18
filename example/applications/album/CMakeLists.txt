add_library(zui_album_session OBJECT
  cpp/session.cpp
  cpp/gallery.cpp
  cpp/input.cpp
  cpp/store.cpp
  cpp/lightbox.cpp
  cpp/commands.cpp
  cpp/rows.cpp
  cpp/native_file.cpp
)
target_include_directories(zui_album_session PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/cpp"
  "${ZUI_PUBLIC_INCLUDE_DIR}"
  "${ZUI_GENERATED_INCLUDE_DIR}"
)
target_link_libraries(zui_album_session PUBLIC zui nlohmann_json::nlohmann_json)

if(ZUI_BUILD_EXAMPLES)
  zui_add_app(zui_album SESSION zui_album_session SOURCES main.cpp)
  add_custom_command(TARGET zui_album POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/pictures"
      "$<TARGET_FILE_DIR:zui_album>/pictures"
  )
endif()
