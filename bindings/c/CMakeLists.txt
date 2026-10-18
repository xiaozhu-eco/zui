# The C ABI lives in the zui library so ListModel / TableModel subclasses do not
# cross a DLL boundary. The header is the only file other languages include.

target_sources(zui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/capi.cpp")
target_include_directories(zui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/example/main.c")
  add_executable(zui_c_hello example/main.c)
  target_include_directories(zui_c_hello PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
  target_link_libraries(zui_c_hello PRIVATE zui)
  set_target_properties(zui_c_hello PROPERTIES LINKER_LANGUAGE CXX)
  zui_stage_runtime(zui_c_hello)
  add_custom_command(TARGET zui_c_hello POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/example/hello.html"
            "$<TARGET_FILE_DIR:zui_c_hello>/c_hello.html"
    COMMENT "Copy c_hello.html next to zui_c_hello")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/conformance.c")
  add_executable(zui_capi_test test/conformance.c)
  target_include_directories(zui_capi_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
  target_link_libraries(zui_capi_test PRIVATE zui)
  set_target_properties(zui_capi_test PROPERTIES LINKER_LANGUAGE CXX)
  zui_stage_runtime(zui_capi_test)
  add_test(NAME capi_conformance COMMAND zui_capi_test)
endif()

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(
    NAME capi_python
    COMMAND ${CMAKE_COMMAND} -E env
            "ZUI_LIB_DIR=${CMAKE_BINARY_DIR}"
            "PYTHONPATH=${ZUI_ROOT}/bindings/python"
            ${Python3_EXECUTABLE} "${ZUI_ROOT}/bindings/python/test_capi.py")
endif()

find_program(ZUI_CARGO cargo)
if(ZUI_CARGO)
  add_test(
    NAME capi_rust
    COMMAND ${CMAKE_COMMAND} -E env
            "ZUI_LIB_DIR=${CMAKE_BINARY_DIR}"
            ${ZUI_CARGO} test --offline --manifest-path "${ZUI_ROOT}/bindings/rust/Cargo.toml")
endif()
