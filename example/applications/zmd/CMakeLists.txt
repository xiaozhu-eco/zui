add_library(zui_zmd_session OBJECT
  cpp/util.cpp
  cpp/markdown.cpp
  cpp/document.cpp
  cpp/editor.cpp
  cpp/workspace.cpp
  cpp/outline_model.cpp
  cpp/file_list_model.cpp
  cpp/chrome.cpp
  cpp/panels.cpp
  cpp/settings.cpp
  cpp/palette_model.cpp
  cpp/palette.cpp
  cpp/find.cpp
  cpp/session.cpp
)
target_include_directories(zui_zmd_session PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/cpp"
  "${ZUI_PUBLIC_INCLUDE_DIR}"
  "${ZUI_GENERATED_INCLUDE_DIR}"
)
target_link_libraries(zui_zmd_session PUBLIC zui)

if(ZUI_BUILD_EXAMPLES)
  add_executable(zui_zmd
    main.cpp
  )
  target_include_directories(zui_zmd PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/cpp"
  )
  target_link_libraries(zui_zmd PRIVATE zui_zmd_session zui)

  # The page, the sheets, the scripts and the welcome sample travel with the executable:
  # zui loads index.html only, and the css/ js/ samples/ it references have to sit beside it.
  add_custom_command(TARGET zui_zmd POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/index.html"
      "${CMAKE_CURRENT_SOURCE_DIR}/icon.svg"
      "$<TARGET_FILE_DIR:zui_zmd>"
    COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_FILE_DIR:zui_zmd>/css"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/css"
      "$<TARGET_FILE_DIR:zui_zmd>/css"
    COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_FILE_DIR:zui_zmd>/js"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/js"
      "$<TARGET_FILE_DIR:zui_zmd>/js"
    COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_FILE_DIR:zui_zmd>/samples"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/samples"
      "$<TARGET_FILE_DIR:zui_zmd>/samples"
    COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_FILE_DIR:zui_zmd>/icons"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/icons"
      "$<TARGET_FILE_DIR:zui_zmd>/icons"
  )
endif()
