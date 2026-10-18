add_library(zui_chat_session OBJECT
  cpp/session.cpp
  cpp/store.cpp
  cpp/commands.cpp
  cpp/rows.cpp
  cpp/clock.cpp
  cpp/list_view.cpp
  cpp/log_view.cpp
)
target_include_directories(zui_chat_session PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/cpp"
  "${ZUI_PUBLIC_INCLUDE_DIR}"
  "${ZUI_GENERATED_INCLUDE_DIR}"
)
target_link_libraries(zui_chat_session PUBLIC zui nlohmann_json::nlohmann_json)

if(ZUI_BUILD_EXAMPLES)
  zui_add_app(zui_chat SESSION zui_chat_session SOURCES main.cpp)
endif()
