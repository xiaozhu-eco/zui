add_library(zui_reader_session OBJECT
  cpp/book.cpp
  cpp/book_codec.cpp
  cpp/book_text.cpp
  cpp/book_heading.cpp
  cpp/book_parse.cpp
  cpp/book_window.cpp
  cpp/book_format.cpp
  cpp/book_zip.cpp
  cpp/book_epub.cpp
  cpp/book_pdf.cpp
  cpp/session.cpp
  cpp/session_detail.cpp
  cpp/nav.cpp
  cpp/shelf.cpp
  cpp/shelf_query.cpp
  cpp/find.cpp
  cpp/page_mode.cpp
)
target_include_directories(zui_reader_session PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/cpp"
  "${ZUI_PUBLIC_INCLUDE_DIR}"
  "${ZUI_GENERATED_INCLUDE_DIR}"
)
target_link_libraries(zui_reader_session PUBLIC
  zui
  nlohmann_json::nlohmann_json
  zui_minizip
  zui_pugixml
  zlibstatic
)

# Android: zui-pack packages a shared library whose main() SDL calls on its
# native thread. The library is named main so the APK carries libmain.so, the
# name SDL's Java side looks for. This is the android.target in zui.json.
if(ANDROID)
  add_library(zui_reader_android SHARED main.cpp)
  target_include_directories(zui_reader_android PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/cpp"
  )
  target_link_libraries(zui_reader_android PRIVATE zui_reader_session zui nlohmann_json::nlohmann_json zui_minizip zui_pugixml zlibstatic)
  set_target_properties(zui_reader_android PROPERTIES OUTPUT_NAME "main")
endif()

if(ZUI_BUILD_EXAMPLES)
  add_executable(zui_reader
    main.cpp
  )
  target_include_directories(zui_reader PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/cpp"
  )
  target_link_libraries(zui_reader PRIVATE zui_reader_session zui nlohmann_json::nlohmann_json zui_minizip zui_pugixml zlibstatic)
  add_custom_command(TARGET zui_reader POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/reader.html"
      "${CMAKE_CURRENT_SOURCE_DIR}/reader.css"
      "${CMAKE_CURRENT_SOURCE_DIR}/reader.js"
      "${CMAKE_CURRENT_SOURCE_DIR}/chapter.html"
      "${CMAKE_CURRENT_SOURCE_DIR}/manifest.webmanifest"
      "${CMAKE_CURRENT_SOURCE_DIR}/sw.js"
      "${CMAKE_CURRENT_SOURCE_DIR}/icon.svg"
      "$<TARGET_FILE_DIR:zui_reader>"
    COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_FILE_DIR:zui_reader>/fragments"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/fragments"
      "$<TARGET_FILE_DIR:zui_reader>/fragments"
  )

  # Same page and fixture as Chrome, played from tests/interact/reader_oracle.py.
  add_executable(zui_reader_oracle
    ${ZUI_ROOT}/tests/app_interact/reader_oracle.cpp
  )
  target_include_directories(zui_reader_oracle PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/cpp"
    "${ZUI_ROOT}/tests/app_interact"
    "${ZUI_ROOT}/source"
  )
  target_link_libraries(zui_reader_oracle PRIVATE zui_reader_session zui nlohmann_json::nlohmann_json zui_minizip zui_pugixml zlibstatic)
  zui_bind_source_dir(zui_reader_oracle)
  add_test(NAME ReaderOracle.Shell
    COMMAND python "${ZUI_ROOT}/tests/interact/reader_oracle.py"
            --oracle "$<TARGET_FILE:zui_reader_oracle>"
            --scenario "${ZUI_ROOT}/tests/interact/scenarios/reader-shell.json"
    WORKING_DIRECTORY "${ZUI_ROOT}"
  )
  set_tests_properties(ReaderOracle.Shell PROPERTIES TIMEOUT 180)
endif()

