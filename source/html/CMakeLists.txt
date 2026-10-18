zui_add_interface_sources(zui_src_html
  document.cpp
  selection.cpp
  selection_edit.cpp
  literal_atom.cpp
  loader.cpp
  model.cpp
  model_rows.cpp
  model_nested.cpp
  model_table.cpp
  model_window.cpp
  select.cpp
)
# loader.cpp notes html_nodes. The TU lives here, the lowest engine module
# that calls perf, so html-only binaries (zui_model) link it and html+layout
# binaries do not compile it twice.
target_link_libraries(zui_src_html INTERFACE zui_src_perf)
