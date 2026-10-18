# Kernel fixtures

Small pages the engine tests drive. Each is a `main.cpp` plus HTML/CSS, built with
`zui_add_fixture_program`. Binaries land in `build/windows-x64/` next to `zui.dll`.

```bash
cmake --build --preset windows-x64 --target zui_hello
build/windows-x64/zui_hello.exe
```

| Target | Page |
| --- | --- |
| `zui_hello` | shell, nav, a field, `bind` + `set` |
| `zui_studio` | larger chrome plus an export fragment |
| `zui_checks` | control gallery |
| `zui_flow` | overflow / scroll |
| `zui_style` | cascade and classes |
| `zui_table` | table layout |
| `zui_text` | text runs |
| `zui_motion` | transitions |

Handlers used by both `main()` and the runtime tests live in `actions.h` next to that
page.
