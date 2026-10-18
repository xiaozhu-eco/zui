add_library(zui_calendar_session OBJECT
  cpp/session.cpp
  cpp/board.cpp
  cpp/editor.cpp
  cpp/store.cpp
  cpp/dates.cpp
  cpp/layout.cpp
  cpp/span.cpp
  cpp/commands.cpp
  cpp/rows.cpp
)
target_include_directories(zui_calendar_session PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/cpp"
  "${ZUI_PUBLIC_INCLUDE_DIR}"
  "${ZUI_GENERATED_INCLUDE_DIR}"
)
target_link_libraries(zui_calendar_session PUBLIC zui nlohmann_json::nlohmann_json)

if(ZUI_BUILD_EXAMPLES)
  zui_add_app(zui_calendar SESSION zui_calendar_session SOURCES main.cpp)
endif()
