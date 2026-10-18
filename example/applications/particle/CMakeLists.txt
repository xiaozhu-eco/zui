add_library(zui_particle_session OBJECT
  cpp/session.cpp
  cpp/field.cpp
  cpp/controls.cpp
  cpp/motion.cpp
  cpp/shader.cpp
  cpp/rows.cpp
)
target_include_directories(zui_particle_session PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/cpp"
  "${ZUI_PUBLIC_INCLUDE_DIR}"
  "${ZUI_GENERATED_INCLUDE_DIR}"
)
target_link_libraries(zui_particle_session PUBLIC zui nlohmann_json::nlohmann_json)

if(ZUI_BUILD_EXAMPLES)
  zui_add_app(zui_particle SESSION zui_particle_session SOURCES main.cpp)
endif()
