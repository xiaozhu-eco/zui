add_library(zui_zdb_session OBJECT
  cpp/session.cpp
  cpp/util.cpp
  cpp/native_file.cpp
  cpp/sql/db.cpp
  cpp/sql/sample.cpp
  cpp/sql/catalog.cpp
  cpp/sql/browse.cpp
  cpp/sql/script.cpp
  cpp/ui/shell.cpp
  cpp/ui/sidebar.cpp
  cpp/ui/grid.cpp
  cpp/ui/structure.cpp
  cpp/ui/query.cpp
  cpp/ui/state.cpp
  cpp/ui/actions.cpp
  sqlite/sqlite3.c
)
target_include_directories(zui_zdb_session PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/sqlite"
  "${ZUI_PUBLIC_INCLUDE_DIR}"
  "${ZUI_GENERATED_INCLUDE_DIR}"
)
target_link_libraries(zui_zdb_session PUBLIC zui)
if(MSVC)
  set_source_files_properties(sqlite/sqlite3.c PROPERTIES
    COMPILE_OPTIONS "/w")
endif()

if(ZUI_BUILD_EXAMPLES)
  add_executable(zui_zdb
    main.cpp
  )
  target_include_directories(zui_zdb PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sqlite"
  )
  target_link_libraries(zui_zdb PRIVATE zui_zdb_session zui)
  # The other examples copy index.html and css/ into the shared build directory.
  # zdb uses those same names, so it gets its own folder and a copy of zui.dll.
  set_target_properties(zui_zdb PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/zdb"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/zdb"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/zdb"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/zdb"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/zdb"
  )
  add_custom_command(TARGET zui_zdb POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:zui>"
      "${CMAKE_CURRENT_SOURCE_DIR}/index.html"
      "${CMAKE_CURRENT_SOURCE_DIR}/icon.svg"
      "$<TARGET_FILE_DIR:zui_zdb>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/css"
      "$<TARGET_FILE_DIR:zui_zdb>/css"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/js"
      "$<TARGET_FILE_DIR:zui_zdb>/js"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/vendor"
      "$<TARGET_FILE_DIR:zui_zdb>/vendor"
  )
endif()
