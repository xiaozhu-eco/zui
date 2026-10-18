add_library(zui_contacts_session OBJECT
  cpp/session.cpp
  cpp/list.cpp
  cpp/editor.cpp
  cpp/store.cpp
  cpp/filter.cpp
  cpp/commands.cpp
  cpp/text.cpp
  cpp/rows.cpp
)
target_include_directories(zui_contacts_session PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/cpp"
  "${ZUI_PUBLIC_INCLUDE_DIR}"
  "${ZUI_GENERATED_INCLUDE_DIR}"
)
target_link_libraries(zui_contacts_session PUBLIC zui nlohmann_json::nlohmann_json)

if(ZUI_BUILD_EXAMPLES)
  zui_add_app(zui_contacts SESSION zui_contacts_session SOURCES main.cpp)
endif()
