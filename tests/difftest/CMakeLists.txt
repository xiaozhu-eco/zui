# Differential test harness (see docs/TESTING.md).
# Engine-side driver. Reads a case's steps and writes trace JSON. Capture PNG
# waits on framebuffer readback in source/; this target does not rasterize the
# display list. It compiles the kernel and does not compile source/app/app.cpp.

zui_add_kernel_executable(zui_difftest
  LEXBOR FREETYPE IMAGE WINHTTP
  MODULES html css layout text paint input
  SOURCES
    engine/main.cpp
    engine/trace_export.cpp
    engine/fs_util.cpp
    engine/png_encode.cpp
)
target_include_directories(zui_difftest PRIVATE
  "${ZUI_ROOT}/third_party/glfw/deps"
)
target_link_libraries(zui_difftest PRIVATE nlohmann_json::nlohmann_json zui::zui)
if(MSVC)
  # nlohmann/json is third-party header-only code we cannot fix; keep its
  # warnings out of ours.
  target_compile_options(zui_difftest PRIVATE /W0)
else()
  target_compile_options(zui_difftest PRIVATE -w)
endif()
zui_bind_source_dir(zui_difftest)
add_test(NAME difftest_engine
  COMMAND zui_difftest
    --case "${ZUI_ROOT}/tests/difftest/cases/flow-basic/case.json"
    --repo "${ZUI_ROOT}"
    --out "${CMAKE_BINARY_DIR}/difftest/engine-smoke"
)
set_tests_properties(difftest_engine PROPERTIES TIMEOUT 120)

zui_add_kernel_executable(zui_difftest_selfcheck
  LEXBOR FREETYPE IMAGE WINHTTP GTEST
  MODULES html css layout text paint input
  SOURCES
    ${ZUI_ROOT}/tests/difftest_selfcheck/layout_invalidation_test.cpp
    ${ZUI_ROOT}/tests/difftest_selfcheck/input_events_test.cpp
    ${ZUI_ROOT}/tests/difftest_selfcheck/drag_drop_test.cpp
    ${ZUI_ROOT}/tests/difftest_selfcheck/widgets_interaction_test.cpp
    ${ZUI_ROOT}/tests/difftest_selfcheck/details_interaction_test.cpp
    ${ZUI_ROOT}/tests/difftest_selfcheck/dialog_interaction_test.cpp
    ${ZUI_ROOT}/tests/difftest_selfcheck/range_interaction_test.cpp
    ${ZUI_ROOT}/tests/difftest_selfcheck/scalars_interaction_test.cpp
    ${ZUI_ROOT}/tests/difftest_selfcheck/fragment_tree_test.cpp
    ${ZUI_ROOT}/tests/difftest_selfcheck/fragment_parse_test.cpp
    ${ZUI_ROOT}/tests/difftest_selfcheck/cascade_values_test.cpp
    ${ZUI_ROOT}/tests/difftest_selfcheck/paint_listing_test.cpp
    ${ZUI_ROOT}/tests/difftest_selfcheck/marker_lists_test.cpp
    ${ZUI_ROOT}/tests/difftest_selfcheck/ua_styles_test.cpp
    ${ZUI_ROOT}/tests/difftest_selfcheck/text_layout_test.cpp
    ${ZUI_ROOT}/tests/difftest_selfcheck/form_appearance_test.cpp
    ${ZUI_ROOT}/tests/difftest_selfcheck/paint_effects_test.cpp
    ${ZUI_ROOT}/tests/difftest_selfcheck/fragmentation_test.cpp
    ${ZUI_ROOT}/tests/difftest_selfcheck/scroll_transition_test.cpp
    ${ZUI_ROOT}/tests/difftest_selfcheck/misc_feature_test.cpp
    ${ZUI_ROOT}/tests/difftest_selfcheck/transform_aff2_test.cpp
    ${ZUI_ROOT}/tests/difftest_selfcheck/selector_markup_test.cpp
    ${ZUI_ROOT}/tests/difftest_selfcheck/selector_dir_test.cpp
    ${ZUI_ROOT}/tests/difftest_selfcheck/browser_defaults_test.cpp
)
