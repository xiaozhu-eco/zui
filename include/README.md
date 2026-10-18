# include/

Applications include `zui/app.h` only. That header is the application entry. It pulls in:

- `event.h` — `Event`
- `model.h` — `ListModel`, `TableModel`
- `host_types.h` — host POD and enums
- `element_box.h` — `ElementBox`, the CSSOM View measurement result
- `plugin.h` — `HostServices` and `zui::plugin` payload types. Calls go through `Host::services()`.
- `document.h` — `Document` (HTML, data, theme, classes, timers, focus)
- `host.h` — `Host` (dialogs, clipboard, paths, gamepads, tasks)
- `window.h` — `Window` (chrome and child documents)

`host_surface.h` holds backend surface types. `app.h` does not include it.

`App` owns one `Document`, one `Host`, and one `Window`. It does not inherit them. Those objects live in the session implementation. `document()`, `host()`, and `window()` return them:

```cpp
app.document().load("window.html");
app.document().bind("on_save", [](zui::Event& e) { (void)e; });
app.document().set("status", "text", "Ready");
app.host().pick_open();
app.window().set_title("zui");
```

`enable_diagnostics` and `log_line` stay on `App`. `diagnostics.h` declares no type.

Do not add `layout.h`, `style.h`, widget types, or HTML/CSS parser types here.
The product surface is `docs/CONTRACT.md`.
