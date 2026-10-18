add_library(zui_kanban_session OBJECT
  cpp/store.cpp
  cpp/session.cpp
  cpp/editor.cpp
  cpp/drag.cpp
)
target_include_directories(zui_kanban_session PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/cpp"
  "${ZUI_PUBLIC_INCLUDE_DIR}"
  "${ZUI_GENERATED_INCLUDE_DIR}"
)
target_link_libraries(zui_kanban_session PUBLIC zui nlohmann_json::nlohmann_json)

if(ZUI_BUILD_EXAMPLES)
  zui_add_app(zui_kanban SESSION zui_kanban_session SOURCES main.cpp)
endif()
