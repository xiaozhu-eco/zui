# Kernel-plane tests compile engine translation units themselves and do not
# link App / IWindow. Shared file lists live on the zui_src_* INTERFACE
# libraries under source/. Subset layout tests keep an explicit file list.

zui_add_kernel_executable(zui_layout_hello
  LEXBOR FREETYPE IMAGE GTEST
  MODULES html css layout text input
  SOURCES layout_hello.cpp
)
zui_add_kernel_executable(zui_layout_grid
  LEXBOR FREETYPE IMAGE GTEST
  MODULES html css layout text input
  SOURCES layout_grid.cpp
)
zui_add_kernel_executable(zui_layout_flex_auto
  LEXBOR FREETYPE IMAGE GTEST
  MODULES html css layout text input
  SOURCES layout_flex_auto.cpp missing_gaps.cpp
)
zui_add_kernel_executable(zui_layout_translate
  LEXBOR FREETYPE IMAGE GTEST
  MODULES html css layout text input
  SOURCES layout_translate.cpp
)
zui_add_kernel_executable(zui_layout_text
  LEXBOR FREETYPE IMAGE GTEST
  MODULES html css layout text input
  SOURCES layout_text.cpp
          ${CMAKE_SOURCE_DIR}/source/backends/draw_effect.cpp
          ${CMAKE_SOURCE_DIR}/source/backends/glyph_outlines.cpp
)
zui_add_kernel_executable(zui_layout_flow
  LEXBOR FREETYPE IMAGE GTEST
  MODULES html css layout text paint input
  SOURCES layout_flow.cpp
)
zui_add_kernel_executable(zui_layout_geometry
  LEXBOR FREETYPE IMAGE GTEST
  MODULES html css layout text paint input
  SOURCES layout_geometry.cpp
)
zui_add_kernel_executable(zui_paint_hello
  LEXBOR FREETYPE IMAGE GTEST
  MODULES html css layout text paint input
  SOURCES paint_hello.cpp
)
zui_add_kernel_executable(zui_image_level
  GTEST
  SOURCES
    image_level.cpp
    ${ZUI_ROOT}/source/backends/image_textures.cpp
)
zui_add_kernel_executable(zui_paint_round
  LEXBOR FREETYPE IMAGE GTEST
  MODULES html css layout text paint input
  SOURCES paint_round.cpp
)
zui_add_kernel_executable(zui_dump
  LEXBOR FREETYPE IMAGE WINHTTP PERFETTO
  MODULES html css layout text paint input trace
  SOURCES dump.cpp
)
zui_bind_source_dir(zui_dump)

zui_add_kernel_executable(zui_ui_probe
  LEXBOR FREETYPE IMAGE
  MODULES html css layout text input
  SOURCES ui_probe.cpp
)
zui_bind_source_dir(zui_ui_probe)

zui_add_kernel_executable(zui_transition
  LEXBOR FREETYPE GTEST
  MODULES html css text
  SOURCES transition.cpp
)
zui_add_kernel_executable(zui_spec
  LEXBOR FREETYPE IMAGE GTEST
  MODULES html css layout text input
  SOURCES spec.cpp
)
zui_add_kernel_executable(zui_shorthand
  LEXBOR FREETYPE GTEST
  MODULES html css_shorthand css_engine css_values text
  SOURCES shorthand.cpp
)
zui_add_kernel_executable(zui_model
  LEXBOR GTEST
  MODULES html
  SOURCES model.cpp
)

# style_table.cpp resolves viewport units through the intern module's shared
# viewport. Used sizes of calc()/min()/max()/clamp() resolve in css/values
# once the basis is known.
zui_add_kernel_executable(zui_layout_inline
  LEXBOR FREETYPE IMAGE GTEST
  MODULES text css_values perf layout_algo
  SOURCES
    layout_inline.cpp
    ${ZUI_ROOT}/source/html/literal_atom.cpp
)
zui_add_kernel_executable(zui_layout_cache
  LEXBOR FREETYPE IMAGE GTEST
  MODULES text css_values perf layout_algo
  SOURCES
    layout_cache.cpp
    ${ZUI_ROOT}/source/html/literal_atom.cpp
)

# The interner resolves computed lengths (em / rem / calc), so this one
# style-layer file comes with the layout sources rather than with the CSS engine.
zui_add_kernel_executable(zui_layout_box
  LEXBOR FREETYPE IMAGE GTEST
  MODULES html text css_values
  SOURCES
    layout_box.cpp
    ${ZUI_ROOT}/source/layout/box.cpp
    ${ZUI_ROOT}/source/layout/intern_style.cpp
    ${ZUI_ROOT}/source/layout/intern_style_as.cpp
    ${ZUI_ROOT}/source/layout/read_style.cpp
    ${ZUI_ROOT}/source/layout/read_flex.cpp
    ${ZUI_ROOT}/source/layout/read_table_intrinsic.cpp
    ${ZUI_ROOT}/source/layout/intern_grid.cpp
    ${ZUI_ROOT}/source/layout/style_table.cpp
    ${ZUI_ROOT}/source/layout/box_tree/tree.cpp
    ${ZUI_ROOT}/source/layout/box_tree/builder.cpp
    ${ZUI_ROOT}/source/layout/box_tree/srcset.cpp
    ${ZUI_ROOT}/source/layout/box_tree/sync.cpp
    ${ZUI_ROOT}/source/layout/box_tree/match.cpp
    ${ZUI_ROOT}/source/layout/box_tree/table.cpp
    ${ZUI_ROOT}/source/layout/cache/layout_invalidation.cpp
    ${ZUI_ROOT}/source/layout/cache/layout_key.cpp
    ${ZUI_ROOT}/source/layout/cache/constraint_space.cpp
    ${ZUI_ROOT}/source/layout/cache/exclusion_space.cpp
    ${ZUI_ROOT}/source/layout/cache/box_cache.cpp
    ${ZUI_ROOT}/source/layout/cache/out_of_flow.cpp
    ${ZUI_ROOT}/source/layout/cache/in_flow.cpp
    ${ZUI_ROOT}/source/layout/cache/layout_context.cpp
    ${ZUI_ROOT}/source/layout/cache/layout_context_store.cpp
    ${ZUI_ROOT}/source/layout/cache/layout_context_text.cpp
    ${ZUI_ROOT}/source/layout/cache/ask_fuse.cpp
    ${ZUI_ROOT}/source/layout/flow/flow.cpp
    ${ZUI_ROOT}/source/layout/flow/shrink.cpp
    ${ZUI_ROOT}/source/layout/flow/finish.cpp
    ${ZUI_ROOT}/source/layout/flow/plain_block.cpp
    ${ZUI_ROOT}/source/layout/flow/special.cpp
    ${ZUI_ROOT}/source/layout/flow/margins.cpp
    ${ZUI_ROOT}/source/layout/flow/widget_size.cpp
    ${ZUI_ROOT}/source/layout/flow/used_values.cpp
    ${ZUI_ROOT}/source/layout/flow/exclusion.cpp
    ${ZUI_ROOT}/source/layout/flow/atomic.cpp
    ${ZUI_ROOT}/source/layout/flow/pieces.cpp
    ${ZUI_ROOT}/source/layout/flow/sticky.cpp
    ${ZUI_ROOT}/source/layout/flow/lines.cpp
    ${ZUI_ROOT}/source/layout/flow/line_wrap.cpp
    ${ZUI_ROOT}/source/layout/flow/line_wrap_finish.cpp
    ${ZUI_ROOT}/source/layout/flow/inline_pieces.cpp
    ${ZUI_ROOT}/source/layout/flow/inline_content.cpp
    ${ZUI_ROOT}/source/layout/flow/blocks.cpp
    ${ZUI_ROOT}/source/layout/flow/fragmentation.cpp
    ${ZUI_ROOT}/source/layout/inline/script.cpp
    ${ZUI_ROOT}/source/layout/inline/bidi.cpp
    ${ZUI_ROOT}/source/layout/inline/shape.cpp
    ${ZUI_ROOT}/source/layout/flex/flex.cpp
    ${ZUI_ROOT}/source/layout/flex/pass.cpp
    ${ZUI_ROOT}/source/layout/flex/container.cpp
    ${ZUI_ROOT}/source/layout/flex/items.cpp
    ${ZUI_ROOT}/source/layout/flex/lines.cpp
    ${ZUI_ROOT}/source/layout/flex/pack.cpp
    ${ZUI_ROOT}/source/layout/flex/placement.cpp
    ${ZUI_ROOT}/source/layout/flex/finish.cpp
    ${ZUI_ROOT}/source/layout/flex/detail.cpp
    ${ZUI_ROOT}/source/layout/grid/grid.cpp
    ${ZUI_ROOT}/source/layout/grid/measure.cpp
    ${ZUI_ROOT}/source/layout/grid/measure_area.cpp
    ${ZUI_ROOT}/source/layout/grid/geometry.cpp
    ${ZUI_ROOT}/source/layout/grid/tracks.cpp
    ${ZUI_ROOT}/source/layout/grid/placement.cpp
    ${ZUI_ROOT}/source/layout/grid/templates.cpp
    ${ZUI_ROOT}/source/layout/multicol/multicol.cpp
    ${ZUI_ROOT}/source/layout/multicol/multicol_columns.cpp
    ${ZUI_ROOT}/source/layout/table/table.cpp
    ${ZUI_ROOT}/source/layout/table/place.cpp
    ${ZUI_ROOT}/source/layout/table/model.cpp
    ${ZUI_ROOT}/source/layout/table/geometry.cpp
    ${ZUI_ROOT}/source/layout/paint_map_frames.cpp
    ${ZUI_ROOT}/source/layout/clip_path.cpp
    ${ZUI_ROOT}/source/layout/freetype_text_metrics.cpp
)

zui_add_kernel_executable(zui_log_test GTEST
  SOURCES log_test.cpp
)

zui_add_kernel_executable(zui_line_budget GTEST
  SOURCES line_budget.cpp
)

zui_add_kernel_executable(zui_grapheme_break
  FREETYPE GTEST
  MODULES text
  SOURCES grapheme_break_test.cpp
)

zui_add_kernel_executable(zui_caret_service
  LEXBOR FREETYPE IMAGE GTEST
  MODULES html css layout text paint input
  SOURCES caret_service_test.cpp range_selection_test.cpp
)

# Parser symbols come from zui_image. This executable must not link
# zui_plugin_image: that is the check that the parser is inside the engine.
add_executable(zui_svg_parser svg_parser.cpp)
target_include_directories(zui_svg_parser PRIVATE "${ZUI_ROOT}/source")
target_link_libraries(zui_svg_parser PRIVATE zui_image)
zui_gtest(zui_svg_parser)

add_executable(zui_image_codec image_codec.cpp image_boot.cpp)
target_link_libraries(zui_image_codec PRIVATE zui_image zui_plugin_image)
if(ZUI_PLUGINS_ARE_SHARED)
  target_compile_definitions(zui_image_codec PRIVATE ZUI_PLUGIN_SHARED=1)
endif()
zui_gtest(zui_image_codec)

# The codec registry lives in static zui_image. Link that copy into this exe and
# install the built-in image plugin on it (image_boot). Host and view calls go
# through zui.dll, whose own registry builtin.cpp fills in.
add_executable(zui_plugin_slots plugin_ext.cpp image_boot.cpp)
target_include_directories(zui_plugin_slots PRIVATE "${ZUI_ROOT}/source")
target_link_libraries(zui_plugin_slots PRIVATE
  zui
  zui_image
  zui_plugin_image
  zui_plugin_extra_codec
  zui_plugin_view_commit
)
if(ZUI_PLUGINS_ARE_SHARED)
  target_compile_definitions(zui_plugin_slots PRIVATE ZUI_PLUGIN_SHARED=1)
endif()
zui_gtest(zui_plugin_slots)

add_executable(zui_host
  host/damage_isolation_test.cpp
  host/platform_test.cpp
  host/permission_policy_test.cpp
  host/group_alpha_test.cpp
  host/window_chrome_test.cpp
  host/platform_services_test.cpp
  host/plugin_boot.cpp
  host/app_entry_test.cpp
  host/app_entry_ext.cpp
)
target_include_directories(zui_host PRIVATE "${ZUI_ROOT}" "${ZUI_ROOT}/source")
target_link_libraries(zui_host PRIVATE
  zui
  zui_plugin_clipboard
  zui_plugin_pick
  zui_plugin_shell
  zui_plugin_notify
  zui_plugin_tray
  zui_plugin_hotkey
  zui_plugin_media_key
  zui_plugin_instance
  zui_plugin_association
  zui_plugin_update
)
if(ZUI_PLUGINS_ARE_SHARED)
  target_compile_definitions(zui_host PRIVATE ZUI_PLUGIN_SHARED=1)
endif()
if(WIN32)
  target_link_libraries(zui_host PRIVATE imm32)
endif()
zui_gtest(zui_host)

add_executable(zui_designer_board
  tools/designer_board_test.cpp
  ${ZUI_ROOT}/tools/zui-designer/cpp/document.cpp
  ${ZUI_ROOT}/tools/zui-designer/cpp/codegen.cpp
)
target_include_directories(zui_designer_board PRIVATE
  "${ZUI_ROOT}/tools/zui-designer/cpp")
target_link_libraries(zui_designer_board PRIVATE zui nlohmann_json::nlohmann_json)
if(MSVC)
  target_compile_options(zui_designer_board PRIVATE /utf-8)
endif()
zui_gtest(zui_designer_board)

add_executable(zui_svg_raster svg_raster.cpp)
target_include_directories(zui_svg_raster PRIVATE "${ZUI_ROOT}/source")
target_link_libraries(zui_svg_raster PRIVATE zui)
zui_gtest(zui_svg_raster)

add_subdirectory(difftest)
add_subdirectory(app_interact)
