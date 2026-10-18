zui_add_fixture_program(zui_hello
  DIR "${CMAKE_CURRENT_SOURCE_DIR}/hello"
  ASSETS
    "${CMAKE_CURRENT_SOURCE_DIR}/hello/hello.html"
    "${CMAKE_CURRENT_SOURCE_DIR}/hello/hello.css"
)
zui_add_fixture_program(zui_studio
  DIR "${CMAKE_CURRENT_SOURCE_DIR}/studio"
  ASSETS
    "${CMAKE_CURRENT_SOURCE_DIR}/studio/studio.html"
    "${CMAKE_CURRENT_SOURCE_DIR}/studio/studio.css"
    "${CMAKE_CURRENT_SOURCE_DIR}/studio/export.html"
    "${CMAKE_CURRENT_SOURCE_DIR}/studio/dawn.css"
)
zui_add_fixture_program(zui_motion
  DIR "${CMAKE_CURRENT_SOURCE_DIR}/transition"
  ASSETS
    "${CMAKE_CURRENT_SOURCE_DIR}/transition/transition.html"
    "${CMAKE_CURRENT_SOURCE_DIR}/transition/transition.css"
)
zui_add_fixture_program(zui_text
  DIR "${CMAKE_CURRENT_SOURCE_DIR}/text"
  ASSETS
    "${CMAKE_CURRENT_SOURCE_DIR}/text/text.html"
    "${CMAKE_CURRENT_SOURCE_DIR}/text/text.css"
)
zui_add_fixture_program(zui_checks
  DIR "${CMAKE_CURRENT_SOURCE_DIR}/checks"
  ASSETS
    "${CMAKE_CURRENT_SOURCE_DIR}/checks/checks.html"
    "${CMAKE_CURRENT_SOURCE_DIR}/checks/checks.css"
)
zui_add_fixture_program(zui_flow
  DIR "${CMAKE_CURRENT_SOURCE_DIR}/flow"
  ASSETS
    "${CMAKE_CURRENT_SOURCE_DIR}/flow/flow.html"
    "${CMAKE_CURRENT_SOURCE_DIR}/flow/flow.css"
    "${CMAKE_CURRENT_SOURCE_DIR}/flow/mark.png"
)
zui_add_fixture_program(zui_style
  DIR "${CMAKE_CURRENT_SOURCE_DIR}/style"
  ASSETS
    "${CMAKE_CURRENT_SOURCE_DIR}/style/style.html"
    "${CMAKE_CURRENT_SOURCE_DIR}/style/style.css"
)
zui_add_fixture_program(zui_table
  DIR "${CMAKE_CURRENT_SOURCE_DIR}/table"
  ASSETS
    "${CMAKE_CURRENT_SOURCE_DIR}/table/table.html"
    "${CMAKE_CURRENT_SOURCE_DIR}/table/table.css"
)
