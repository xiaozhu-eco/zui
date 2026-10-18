add_library(zui_share_session OBJECT
  cpp/bench.cpp
  cpp/bench_ops.cpp
  cpp/browser.cpp
  cpp/net.cpp
  cpp/net_discover.cpp
  cpp/net_listen.cpp
  cpp/net_recv.cpp
  cpp/net_send.cpp
  cpp/net_snapshot.cpp
  cpp/net_transfer.cpp
  cpp/rows.cpp
  cpp/session.cpp
  cpp/session_actions.cpp
  cpp/session_view.cpp
  cpp/util.cpp
  cpp/util_fs.cpp
  cpp/util_list.cpp
)
target_include_directories(zui_share_session PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/cpp"
  "${ZUI_PUBLIC_INCLUDE_DIR}"
  "${ZUI_GENERATED_INCLUDE_DIR}"
)
target_link_libraries(zui_share_session PUBLIC zui)
if(MSVC)
  # getenv / _wfopen are what xz-share/src/util.cpp uses; the deprecation note is noise here.
  target_compile_definitions(zui_share_session PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(WIN32)
  # net.cpp talks to Winsock directly (same code as xz-share/src/net.cpp).
  target_link_libraries(zui_share_session PUBLIC ws2_32)
endif()

# Android: zui-pack packages a shared library whose main() SDL calls on its
# native thread. The library is named main so the APK carries libmain.so, the
# name SDL's Java side looks for. This is the android.target in zui.json.
if(ANDROID)
  add_library(zui_share_android SHARED main.cpp)
  target_include_directories(zui_share_android PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/cpp"
  )
  target_link_libraries(zui_share_android PRIVATE zui_share_session zui)
  set_target_properties(zui_share_android PROPERTIES OUTPUT_NAME "main")
endif()

if(ZUI_BUILD_EXAMPLES)
  zui_add_app(zui_share SESSION zui_share_session SOURCES main.cpp)
  # zui_add_app ships index.html, css/ and js/; the name-screen icon rides along separately.
  add_custom_command(TARGET zui_share POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/icon.png"
      "$<TARGET_FILE_DIR:zui_share>"
  )
endif()
