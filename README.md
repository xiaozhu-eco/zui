# zui

Native UI from HTML and CSS.

Structure is HTML5. Layout and skin are CSS. C++ binds handlers and writes data.
The same page, rendered by zui, matches Chrome. There is no Chromium and no JavaScript.

```html
<button id="save" class="primary" onclick="on_save">Save</button>
```

```cpp
#include <zui/app.h>

int main(int argc, char** argv) {
    zui::App app(argc, argv);
    app.document().load("window.html");
    app.document().bind("on_save", [&app](zui::Event&) {
        app.document().set("status", "text", "Saved");
    });
    return app.run();
}
```

Application code includes one header: `<zui/app.h>`.

## Build

Windows (clang-cl + Ninja, MSVC headers and import libraries):

```bash
cmake --preset windows-x64
cmake --build --preset windows-x64 --target zui_hello
build/windows-x64/zui_hello.exe
```

LLVM's `clang-cl` must be on `PATH`. The driver finds the MSVC toolchain and the Windows SDK.
`vcvars64.bat` is not required. Recreate an older `cl.exe` build directory once with
`cmake --preset windows-x64 --fresh`.

```bash
cmake --build --preset windows-x64
ctest --preset windows-x64 --output-on-failure
```

Switch backends with `ZUI_WINDOW=sdl3|glfw3` and `ZUI_RENDERER=vk|gl|gles`.

Vendored libraries live in `third_party/` and are wired by `cmake/deps.cmake`.

## How it is cut

```
HTML / CSS  →  Node tree  →  retained box tree  →  DisplayList  →  GPU
                 bind table      LayoutEngine         paint         IRenderer
```

Appearance changes go through `class`. Data goes through `set` / `set_model`. Interaction
goes through HTML event attributes and `bind`.

## Docs

`docs/` is for people who work on zui. It holds current engine mechanism and contributor
conventions. Application walkthroughs live next to the programs; start at
[example/README.md](./example/README.md).

| File | What it is |
| --- | --- |
| [AGENTS.md](./AGENTS.md) | Hard rules for every change |
| [docs/CONTRACT.md](./docs/CONTRACT.md) | Product surface |
| [docs/ARCHITECTURE.md](./docs/ARCHITECTURE.md) | Modules, one frame, backends, plugins |
| [docs/MARKUP.md](./docs/MARKUP.md) | HTML5 + CSS parse |
| [docs/LAYOUT.md](./docs/LAYOUT.md) | Layout engine |
| [docs/EVENT.md](./docs/EVENT.md) | Events, bind, models |
| [docs/PERF.md](./docs/PERF.md) | Cost invariants and tracing |
| [docs/TESTING.md](./docs/TESTING.md) | Chrome comparison and test seams |
