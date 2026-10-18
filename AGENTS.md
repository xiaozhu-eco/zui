# zui

Read [docs/CONTRACT.md](docs/CONTRACT.md), [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md),
[docs/MARKUP.md](docs/MARKUP.md), and [docs/LAYOUT.md](docs/LAYOUT.md) before adding or
changing headers, modules, or examples. Event work follows frozen [docs/EVENT.md](docs/EVENT.md).
Hot-path work (`css/`, `layout/`, `paint/`, `app::flush_style`) follows [docs/PERF.md](docs/PERF.md)
§3. Chrome comparison follows [docs/TESTING.md](docs/TESTING.md). Intra-module file cuts,
the 700-line cap, and record grouping follow [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) §9.

zui is a retained-mode UI library. Structure is HTML5. Layout and skin are CSS. C++ writes
interaction and data.

The framework evolves on the C++ surface in `include/zui`. A C binding only mirrors that
surface: it does not add capabilities and it does not lead the C++ design
([docs/CONTRACT.md](docs/CONTRACT.md) §3).

## Docs

`README.md`, `AGENTS.md`, and `docs/` are for people who work on this repository.

Keep in those files:

- Current engine mechanism (how the machine works today)
- Contributor conventions (what a change may and may not do)

Leave out of those files:

- Application tutorials (those live next to `example/`)
- RFCs, roadmaps, PR ledgers, wave plans
- Dated test reports, coverage ledgers, gap maps
- Status of what landed this week

When a mechanism in `docs/` no longer matches `source/` or `include/zui`, rewrite the page
in the same change that rewrites the code. A stale page is a bug.

## Application C++ (A-tier)

Applications include `include/zui/app.h` only. That header pulls in `document.h`, `host.h`,
`window.h`, `event.h` (`Event`), `model.h` (`ListModel`, `TableModel`),
`host_types.h`, `element_box.h`, and `plugin.h`. It does not pull in `host_surface.h`.
The names below are
[docs/CONTRACT.md](docs/CONTRACT.md) §3; keep this list the same as that section.

- `App`: `run` / `pump_frame` / `quit` / `enable_diagnostics` / `log_line`.
  `document()` / `host()` / `window()` return the three objects `App` owns.
  `App` does not inherit them and does not forward `load` / `bind` / `set` / `open_window`.
- `Document`: `load` / `load_fragment` / `replace_fragment` / `unload_fragment` / `bind` / `unbind` / `set` / `get` /
  `set_model` / `set_theme` / `add_class` / `remove_class` / `watch` / `unwatch` / `measure` /
  `text_offset_at` / `set_text_caret` / `text_caret_rect` / `focus` / `blur` /
  `scroll_into_view` / `defer` / `set_timeout` /
  `set_interval` / `clear_timer`
- `Host`: `set_app_id` / `user_data_dir` / `cache_dir` / `pick_*` / `permission_state` /
  `request_permission` / `clipboard_*` / `open_uri` / `gamepad` / `gamepad_count` /
  `start_task` / `cancel_task` / `services`
- `Window`: `set_title` / `set_window_size` / `set_icon` / `minimize` / `set_maximized` /
  `set_fullscreen` / `set_topmost` / `minimized` / `visible` / `display` / `open_document` /
  `close_document`
- Data by widget id: `text`, `value`, `checked`, `visible`, `disabled`, `src`, `href`,
  `placeholder`, `class`, scroll position, and list models. Writes to `class` go through
  `add_class` / `remove_class`
  (the only visual lever; CSS defines what the class looks like).
- Optional OS services are methods on `HostServices`, obtained from `Host::services()`.
  Payload types live in `zui::plugin` (`plugin.h`). A default-constructed `HostServices`
  stays detached from process services. A call returns a not-ok result until a domain
  plugin registers.
- Shapes are [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). No `HWND` or other OS types.
  `set_window_size` stays the only window-geometry setter.

## Never public

Do not add these to `include/`, examples, or application-facing headers:

- Layout types or layout calls: `Layout`, `HBoxLayout`, `VBoxLayout`, `GridLayout`,
  `set_layout`, `add_widget`, geometry setters
- Style types or style calls: `Style`, `set_style`, `set_property` for CSS, writing
  colors / margins / flex from C++
- Tree authoring for apps: `append_child`, `set_root`, building UI widgets in C++
- Test and automation entry points: `prepare_interaction`, `click_*`, `key`, `wheel_*`,
  `interactive_ids`, and the other `frame::Access` members. `app.h` may friend
  `frame::Access`. It must not add public members for them.

When a feature cannot be expressed, extend HTML/CSS following the specs. Do not add a C++
authoring API for layout or style. Do not invent a custom widget XML dialect.

CSS layout and style follow the official CSS specifications only. The entries are the CSS
snapshot, the current-work list, and the indexes named in [docs/CONTRACT.md](docs/CONTRACT.md).
Implement only standard CSS. Do not implement non-standard dialects, vendor prefixes, or
engine-private behavior.

## Layers

- **A-tier** (`include/zui/app.h`): `App`, `Document`, `Host`, `Window`, `Event`, list models
- **B-tier** (backends): `IWindow` / `IRenderer` / `IPlatform`; application code does not
  include these. Backends may include `include/zui/host_types.h` and
  `include/zui/host_surface.h` among public headers.
- **C-tier** (`source/**`): HTML/CSS parse, cascade, layout engine, paint, dirty flags;
  no stability promise

Control authors (new HTML tags inside the library) may use protected event / paint /
`size_hint` hooks. Application code does not.

## Repo

- Vendored deps live in `third_party/` and are wired by `cmake/deps.cmake`. No FetchContent /
  CPM / system packages for those libraries.
- Comments and commit messages in English. Design discussion in Chinese is fine.
- Do not commit unless asked. Do not `git push`.
