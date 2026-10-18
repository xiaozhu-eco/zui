# Session OBJECT libraries are always built: zui_app_interact links them even
# when example executables are off. Native binaries stay behind ZUI_BUILD_EXAMPLES.

add_subdirectory(applications)

if(ZUI_BUILD_EXAMPLES)
  add_subdirectory(tests)
endif()
