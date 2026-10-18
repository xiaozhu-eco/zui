# Contract

The product surface. Changing this file is a product decision: edit the page, then the code.

## 1. One sentence

The same HTML and CSS, rendered by zui, matches Chrome.

Layout and style follow the official CSS specifications. The entries are the
[CSS Snapshot](https://www.w3.org/TR/CSS/), the
[current-work list](https://www.w3.org/Style/CSS/current-work), and the
[indexes](https://drafts.csswg.org/indexes/). Each behavior follows the module that
defines it.

Structure is HTML5. Layout and skin are CSS. C++ binds interaction and writes data.

## 2. Author surface

| What | Where it lives |
| --- | --- |
| Window, page, control tree, `id`, event names | HTML5 (`.html` / `.zui`) |
| Box model, flex, grid, position, scroll, theme, pseudo-classes, transitions | CSS |
| Clicks, input, I/O, list data | C++ (`bind` + writes by id) |

Runtime appearance changes go through **class** (and semantic data: `text` / `value` /
`checked` / `visible`). Colors, spacing, flex, and sizes live in CSS. Paint records a
`DisplayList` from the box tree.

Interaction follows [EVENT.md](./EVENT.md). Missing expressiveness is an HTML/CSS extension
that follows the specs, or a named data entry. There is no layout type for applications,
and no `append_child` for applications.

## 3. Application C++ (A-tier)

The framework evolves on the C++ public surface. New capability lands in `include/zui`
first. Other languages copy that surface. A C binding is a mirror: it restates signatures
that already exist. Binding headers are outside this author surface.

Applications include [`include/zui/app.h`](../include/zui/app.h) only.

```cpp
#include <zui/app.h>

int main(int argc, char** argv) {
    zui::App app(argc, argv);
    app.document().load("window.html");
    app.document().bind("on_save", [](zui::Event& e) { (void)e; });
    app.document().set("status", "text", "Ready");
    app.document().add_class("title", "error");
    app.window().set_title("zui");
    return app.run();
}
```

`App` owns one `Document`, one `Host`, and one `Window`. It does not inherit them. Those
objects live in the session implementation. There is no `App::load` / `App::bind` /
`App::set` / `App::open_window` forwarding.

- `Document`: `load` / `load_fragment` / `replace_fragment` / `unload_fragment` / `bind` / `unbind` / `set` /
  `get` / `set_model` / `set_theme` / `add_class` / `remove_class` / `watch` / `unwatch` /
  `measure` / `text_offset_at` / `set_text_caret` / `text_caret_rect` / `focus` / `blur` /
  `scroll_into_view` / `defer` / `set_timeout` /
  `set_interval` / `clear_timer`
- `Host`: `set_app_id` / `user_data_dir` / `cache_dir` / `pick_*` / `permission_state` /
  `request_permission` / `clipboard_*` / `open_uri` / `gamepad` / `gamepad_count` /
  `start_task` / `cancel_task` / `services`
- `Window`: `set_title` / `set_window_size` / `set_icon` / `minimize` / `set_maximized` /
  `set_fullscreen` / `set_topmost` / `minimized` / `visible` / `display` / `open_document` /
  `close_document`
- `App`: `run` / `pump_frame` / `quit` / `enable_diagnostics` / `log_line`

Keep this list identical to [`AGENTS.md`](../AGENTS.md).

`Host::permission_state` / `request_permission` take the closed `Permission`
enum in [`host_types.h`](../include/zui/host_types.h). Those names match the
packer catalog. The app lists the names it needs in `zui.json`; a name missing
from that list, or a desktop OS, is `Unsupported`. `Denied` can be asked again.
`Blocked` opens the system settings page on the next `request_permission`.

`Window::open_document` opens another `Document` on the same OS window and returns a
`ChildDocument` handle. The window owns that document. Modal input goes to it.
`close_document` fires `beforeunload` on it first, then destroys it. `set_window_size`
is the only window-geometry setter. `app.h` leaves backend surface types
(`NativeWindowId`, `Cursor`, `HostMediaFacts`) in `host_surface.h`.

`Document::measure` returns `std::optional<ElementBox>`: CSSOM View geometry (border box,
`client_*`, `scroll_*`) together with success. A failed measure has no box. It flushes
before reading. Document Selection is two boundary points (`anchor`, `focus`). Collapsed
is the caret; a non-collapsed range is the highlight. Geometry is the Range↔line-box
mapping in `layout/caret`. Paint draws at most one caret bar from a collapsed Selection.
`Document::text_offset_at` hits a CSS-px point onto that element's laid-out text (the
line under the point, then the nearest glyph edge). Empty when the element has no
laid-out text. `Document::set_text_caret` writes a collapsed Selection at a UTF-8 byte
offset into that element's innerText; it does not clear a form control's node-local
caret. `text_caret_rect` is the bar's CSS-px box after the same flush; a non-collapsed
Selection has no bar. `get` returns semantic fields only (`text` / `value` / `checked` /
`visible` / `disabled` / `src` / `href` / `class` / scroll / `caret` / `selection`).
`set` also accepts `insert` (write at caret or selection), `select` (`start,end` byte
range), and the content attributes a control can carry: `src` / `href` / `placeholder` /
`data-*` / `aria-*`. `caret` / `insert` apply to text entries and to `contenteditable` hosts as
UTF-8 offsets into that host's innerText. `select` / `selection` apply to those
hosts the same way, and also to any other laid-out element as UTF-8 offsets
into that element's innerText: `select` paints Document Selection there and
does not make the element an editing host. `insert` on a non-editing element
is rejected. `contenteditable` default actions insert, split, or wrap existing
nodes at the Selection. `set_model` returns `ModelBind`; a failure carries its reason on that value.

Optional OS services (`notify`, tray, `global_hotkey`, `media_key`, single-instance,
file association, opener, local update) are methods on `HostServices`. `Host::services()`
returns the object for the running session. A default-constructed `HostServices` stays
detached from process services. Payload types live in `zui::plugin` (`plugin.h`, pulled
in by `app.h`). A call returns a not-ok result until a domain plugin registers. See
[ARCHITECTURE.md](./ARCHITECTURE.md).

**Tests and automation stay out of A-tier.** Synthetic clicks, keys, wheels, per-frame
observation, and element enumeration live on C-tier `frame::Access` in
`source/app/frame_session.h`. `app.h` may forward-declare and `friend` that type. It does
not grow public members for them.

These names stay out of `include/`:

- `Layout` / `HBoxLayout` / `VBoxLayout` / `GridLayout`
- `set_layout` / `layout()` / `add_widget` / `add_stretch`
- `Style` / `set_style` / `get_style` / writing CSS properties from C++
- `append_child` / `set_root` / walking a tree to build UI
- Geometry setters (width, height, margin, position)
- Test entry points (`prepare_interaction` / `click_*` / `key` / `wheel_*` / `interactive_ids`)

## 4. Layers

```
A  include/zui/app.h     application header
B  source/backends/      IWindow + IRenderer + IPlatform
C  source/{app,html,css,layout,paint,…}
```

The document tree lives in `html/`. Geometry lives on `layout::Box`. Hit testing is
`LayoutEngine::hit_test`. There is no `source/tree/` and no `source/event/`. Module
boundaries are [ARCHITECTURE.md](./ARCHITECTURE.md).

Control authors (new tags inside the library) may have `protected` event, `paint`,
intrinsic `size_hint`, focus, and timer hooks. Application code does not.

B-tier is not included by application code. C-tier is not installed and does not enter
`include/`.

## 5. Source map

```
include/zui/app.h         session: owns Document, Host, Window
include/zui/document.h    Document
include/zui/host.h        Host
include/zui/window.h      Window
include/zui/event.h       Event
include/zui/model.h       ListModel, TableModel
include/zui/host_types.h  application POD; backends may include this
include/zui/host_surface.h backend surface types; app.h does not include this
include/zui/plugin.h      HostServices and zui::plugin payload types
source/app html css layout paint backends
example/*/                .html + .css + main.cpp that includes <zui/app.h> only
```

Work-unit counters (`perf::FrameWork`) live in C-tier `source/perf/perf_budget.h`.

Third-party libraries compile from `third_party/` via `cmake/deps.cmake`. UI documents
are not parsed with pugixml.
