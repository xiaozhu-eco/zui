# Headless frame::Access::prepare_interaction driver over example application
# sessions. zmd / zdb / reader own session.h, so they compile as OBJECT libs
# (see example/applications/<app>/CMakeLists.txt) and link into this executable.

set(ZUI_APP_INTERACT_SOURCES
  app_interact_test.cpp
  reader_interact_test.cpp
  frame_loop_test.cpp
  concern_tags_test.cpp
  session_flow_test.cpp
  measure_api_test.cpp
  window_input_test.cpp
  example_programs_test.cpp
  chat_handlers_test.cpp
  contacts_handlers_test.cpp
  calendar_handlers_test.cpp
  kanban_handlers_test.cpp
  share_handlers_test.cpp
  file_explorer_handlers_test.cpp
  album_handlers_test.cpp
  opened_path_test.cpp
  example_tests_handlers_test.cpp
  zdb_handlers_test.cpp
  reader_handlers_test.cpp
  writer_handlers_test.cpp
  particle_handlers_test.cpp
  engine_units_test.cpp
  example_tests_perf_test.cpp
  perf_stage0_test.cpp
  caret_paint_test.cpp
  example_tests_render_test.cpp
  hover_flicker_test.cpp
  event_kinds_test.cpp
  interaction_census_test.cpp
  browser_walk_test.cpp
  perf_debug_probe_test.cpp
  gpu_retain_test.cpp
)
# zmd's session is optional in a partial tree. The handler tests and the call
# sites below stay out until example/applications/zmd/CMakeLists.txt is back.
set(ZUI_APP_INTERACT_SESSIONS
  zui_album_session
  zui_calendar_session
  zui_chat_session
  zui_contacts_session
  zui_kanban_session
  zui_share_session
  zui_zdb_session
  zui_reader_session
  zui_writer_session
  zui_particle_session
)
if(EXISTS "${ZUI_ROOT}/example/applications/zmd/CMakeLists.txt")
  list(APPEND ZUI_APP_INTERACT_SOURCES
    zmd_editor_handlers_test.cpp
    zmd_chrome_handlers_test.cpp
    zmd_files_handlers_test.cpp
    zmd_find_handlers_test.cpp
    zmd_page_handlers_test.cpp
    zmd_daily_test.cpp
  )
  list(APPEND ZUI_APP_INTERACT_SESSIONS zui_zmd_session)
endif()

add_executable(zui_app_interact ${ZUI_APP_INTERACT_SOURCES})
# Test TU includes zmd headers by relative path; zdb needs -I (view.h鈫抯ession.h);
# reader cpp/ is on the include path so tests can #include session.h.
target_include_directories(zui_app_interact PRIVATE
  "${ZUI_ROOT}/example/applications/zdb/cpp"
  "${ZUI_ROOT}/example/applications/zdb/sqlite"
  "${ZUI_ROOT}/example/applications/reader/cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${ZUI_ROOT}/source"
  "${ZUI_ROOT}/tests/host"
)
zui_enable_spdlog(zui_app_interact)
if(NOT EXISTS "${ZUI_ROOT}/example/applications/zmd/CMakeLists.txt")
  target_compile_definitions(zui_app_interact PRIVATE ZUI_WITHOUT_ZMD)
endif()
target_link_libraries(zui_app_interact PRIVATE
  zui
  nlohmann_json::nlohmann_json
  ${ZUI_APP_INTERACT_SESSIONS}
)
zui_gtest(zui_app_interact)

