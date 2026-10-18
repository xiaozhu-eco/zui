# Bindings

The C ABI is a mirror of `include/zui`. Header: `bindings/c/include/zui_capi.h`. The
implementation compiles into the `zui` shared library so `ListModel` / `TableModel`
subclasses stay in that module. C++ programs include `<zui/app.h>` only.

Python (ctypes) and Rust (hand-written FFI, load the shared library at runtime) call
these C functions.

## Rules

- Every `zui_app_*` call runs on the thread that created the app. Only `zui_task_work`
  runs on a worker, and it may call `zui_task_cancelled` and `zui_task_set_progress`
  only.
- Callbacks may re-enter. Do not destroy the app from a callback. Do not destroy a
  model while `set_model` still holds it.
- Input strings are UTF-8, NUL-terminated. NULL is empty. A NUL inside the string is
  rejected.
- Output uses the caller's buffer. The return value is the byte length without the NUL;
  a return `>=` capacity means truncation.
- Model keys are pointer plus length. They need not be NUL-terminated.
- `zui_event`, pick results, and `zui_task_signal` passed into a callback are valid for
  that callback only.
- Paths and clipboard images the library allocated are released with the matching
  `zui_*_free`, not `free()`.
- Boolean results are 0 or 1. `zui_app_run` returns the process exit code.

## Build and test

`ZUI_BUILD_BINDINGS` is on by default. That compiles the ABI into `zui` and adds:

- `zui_capi_test` — headless C conformance (`capi_conformance` in CTest)
- `zui_c_hello` — windowed C sample loading `c_hello.html` (tests do not launch it)
- CTest `capi_python` when Python is found
- CTest `capi_rust` when cargo is found

```bash
cmake --build --preset windows-x64 --target zui_capi_test zui_c_hello
build/windows-x64/zui_capi_test.exe
```

Python and Rust load the shared library from `ZUI_LIB_DIR`, or search `build/windows-x64`
when that is unset.

```bash
set ZUI_LIB_DIR=build\windows-x64
set PYTHONPATH=bindings\python
python bindings\python\test_capi.py

cargo test --offline --manifest-path bindings\rust\Cargo.toml
```

The Rust sample is outside `cargo test`. For a window, run `zui_c_hello` from the build
directory, or `cargo run --example hello --manifest-path bindings/rust/zui/Cargo.toml`
with the path to `c_hello.html`.
