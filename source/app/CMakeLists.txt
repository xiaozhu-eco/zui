# App facade: frame loop, bind tables, load/set. Depends on every engine module.

zui_add_interface_sources(zui_src_app
  app.cpp
  data.cpp
  window.cpp
  tasks.cpp
  frame.cpp
  frame_session.cpp
  probe.cpp
  child.cpp
  diag_log.cpp
  style_flush.cpp
)
