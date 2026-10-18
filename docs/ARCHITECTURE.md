# Architecture

How the engine is cut. Application code includes [`include/zui/app.h`](../include/zui/app.h)
only. This page constrains `source/`.

Layout types and the `ask` / `run` protocol are [LAYOUT.md](./LAYOUT.md). HTML/CSS parse
is [MARKUP.md](./MARKUP.md). Events and models are [EVENT.md](./EVENT.md).

## 1. Two tables

After a document loads, the session keeps two lookup tables. Application code never holds
a `Node`.

| Table | Key | Value | Filled by | Consumed by |
| --- | --- | --- | --- | --- |
| Structure | element `id` | internal `Node*` | HTML `id` | `set` / `add_class` / `remove_class` |
| Behavior | handler name | `Document::Handler` | `Document::bind` | hit-test reads `onclick` (and friends) and looks up the name |

```html
<button id="save" class="primary" onclick="on_save">Save</button>
```

```cpp
app.document().bind("on_save", [](zui::Event& e) { /* e.source() == "save" */ });
app.document().set("save", "text", "Saved");
app.document().add_class("save", "busy");
```

## 2. Directories

One CMake target: `zui`. Modules are folders.

```
include/zui/app.h          application header
source/
  log/       debug log. Debug / RelWithDebInfo: ZUI_* to a color stderr sink,
             ZUI_FILE_* to a file sink (ZUI_LOG_FILE). Release macros are empty.
  trace/     Perfetto / no-op macros
  perf/      Debug work units (FrameWork)
  image/     suffix → cached RGBA8 Bitmap. SVG parser lives here. Drivers register from plugins.
  text/      font chain, hb_shape, coverage bitmaps
  html/      Node / Document; lexbor HTML5; id table; event attrs; stylesheets
  css/       lexbor syntax + selectors; cascade; interned computed; StyleDiff
  layout/    retained box tree: ConstraintSpace cache, lower, flex/grid/flow
  paint/     walk the box tree, emit DisplayList
  input/     pointer / keyboard / scroll / focus; reads boxes, writes Node state
  backends/  IWindow / IRenderer / IPlatform
  app/       frame loop, the two tables, load/bind/set, flush_style
  ext/       plugin slots and registry. The core names abstract types only.
  probe/     ZUI_PROBE macros. Not a library.
plugins/     one domain per directory. Library name zui_plugin_<dir>.
```

Dependencies point down. `source/CMakeLists.txt` `add_subdirectory` order matches this
graph:

```
log, trace, perf
image, text
html
css      → html
layout   → html + css(computed) + text + image
paint    → layout
input    → html + layout
backends → DisplayList, image, text, host_types
app      → everything above
```

`html/` / `css/` / `layout/` / `paint/` / `input/` include `perf/perf_budget.h` for
counters. They do not include `app/`.

`backends/` does not include `html/`, `css/`, `app/`, or `include/zui/app.h`.
`backends/` may include `include/zui/host_types.h` (thin POD / enums; no `App` / `Event` /
`Node`, no `ZUI_EXPORT`, no virtuals).
`html/` does not include SDL / GLFW / GL / Vulkan.

`include/` holds the types `app.h` pulls in, plus `host_types.h`. Layout, style, node, and
control types stay out.

## 3. One frame

`Document::set` / `add_class` / `set_theme` / `set_model` mark dirty. `restyle()` sets
`style_dirty`. The work runs in `flush_style`.

```
IWindow.poll
  → viewport changed: LayoutEngine::layout(document, Viewport)
  → flush_style:
       cascade → CascadeResult{reattach, layout, paint}
       Transitions.sync
       pending_relower → relower_subtree
       reattach parents → sync_children
       layout list → mark_needs_layout (cache.clear up the parent chain)
       LayoutEngine::layout
  → hit last frame's boxes: Pointer → hit_test → onclick → handler table
  → flush_style again
  → Transitions.sample(t) → Node.used; transform → sync_world
  → paint: box tree → DisplayList
  → IRenderer.begin / submit / present
```

| Dirty source | Box tree |
| --- | --- |
| Paint-only (color, theme skin) | cascade; skip layout when the root cache hits |
| Width / flex / font-size | `mark_needs_layout` |
| `display` / `position` | parent `sync_children` |
| `set_model` | host into `pending_attach`; after cascade, `sync_children`. Equal rows are kept; changed rows rebuild. Kept rows refill `geo.runs` after placement. Nested lists use `ListModel::with_children`. |
| `load_fragment` | already attached at load; parent into `pending_attach` |
| `replace_fragment` | the target's children `forget_subtree` + `detach_node` first, then as `load_fragment` |
| Text edit | `sync_text` |
| `unload_fragment` | `forget_subtree` then `detach_node`; parent `mark_needs_layout` |

An empty box tree (first frame, after `reset`) does a full lower + layout. Pointer and
paint paths do not call `layout` after `flush_style`.

`computed` on a node is the cascaded specified value, still serialized (`"50%"`,
`"flex"`). Used pixels are layout's job. Transition interpolation writes `Node.used` and
leaves the interned computed map alone.

## 4. Two trees

CSS treats an element and a box as different objects. zui does too.

| Tree | Type | Owns | Holds |
| --- | --- | --- | --- |
| Document | `html::Node` | `unique_ptr` children | tag, id, class, attrs, text, pseudo bits, `computed`, scroll |
| Box | `layout::Box` | `LayoutEngine` arena; children are `Box*` | numeric `LayoutStyle`, `geo`, anonymous boxes |

`LayoutEngine` maps a visible element to its principal box. Geometry lives on the box.

Document Selection lives on `html::Document::selection`: two `BoundaryPoint`s (`anchor`,
`focus`). Collapsed (same point) is the caret; a range is the highlight. Paint reads that
range (`::selection` per run, at most one caret bar). Hit-test and caret geometry are the
Range↔line-box mapping in `layout/caret` (`hit_point`, `caret_rect_at`) over
`range_edit_text`. Form-control `caret` / `select_anchor` are the same-node view of
that range. `contenteditable` default actions insert, split, or wrap existing nodes at
the range; `value` / `text` on the host are innerText. There is no second projection
caret, and the mapping does not special-case application element ids.

- `display: none` generates no box
- Anonymous boxes exist for bare text in a block and for text in flex
- `<template>` is not lowered
- `::before` / `::after` / `::marker` generate boxes; `::selection` is a highlight and
  does not
- `visibility: hidden` keeps the box and skips paint of its content

Lower (`build_tree` / `relower_subtree` / `sync_children`) changes structure. Layout
writes geometry.

## 5. Backends

Three virtual interfaces. The interface is the abstract; each OS supplies the
implementation. Application code and `include/zui` depend on the abstracts.

| Interface | Scope | Typical work |
| --- | --- | --- |
| `IWindow` | one window | create, poll input, framebuffer, `content_scale`, cursor, IME rect, `native_handle`, drop queue |
| `IRenderer` | GPU | `begin_frame` / `submit(DisplayList)` / `present` / `resize` / `read_frame` |
| `IPlatform` | process | `pick_open` / `pick_save` / `pick_folder`, `open_uri`, `user_data_dir` / `cache_dir`, `permission_state` / `begin_permission_request` |

Factories: `create_window` (`ZUI_WINDOW`), `create_renderer` (`ZUI_RENDERER`),
`create_platform` (`ZUI_PLATFORM`). Desktop defaults to sdl3 / glfw. File dialogs and
clipboard calls go through plugins; the window forwards.

`App::run()` creates the backends, then `while (running && pump_frame())`. Mobile hosts
that own the system loop call `pump_frame()` themselves. `Host::pick_*` does not expose
`NativeWindowId`; the session fills the current window handle.

Mobile permissions are process-scoped on `IPlatform`. `Host` maps the closed
`Permission` enum to the stable string `permission_name` returns, and parks one
pending handler. Desktop backends stay `Unsupported`. Android asks the runtime
dialog, or opens a settings page for special names (`manage-storage`, overlay,
exact alarm, and the rest of the settings entries). iOS writes Info.plist usage
keys from the same catalog and maps a permanent denial to `Blocked`. The packer
catalog in `tools/common/project.h` is the only declaration vocabulary; it must
match `Permission` by those strings. `pick_*` is the document picker and does
not consume a storage permission.

## 6. Plugins

The core keeps document, style, layout, paint, and facades. Extra capability is a plugin:
one domain, one library. The core calls an abstract type. It never names a plugin's
concrete class. There is no `LoadLibrary` marketplace and no JSON `invoke`.

| Kind | How the core calls | Examples |
| --- | --- | --- |
| Capability slot | `Driver` / `DriverManager`; first registration for a suffix wins | Image codecs (`plugins/image`: PNG, JPEG, SVG) |
| Out-of-framework service | `ExtensionRegistry`; first registration wins | clipboard, pick, shell, gamepad, tasks; optional notify, tray, hotkey, media key, instance, association, update |
| Framework-layer | `ViewExtension::mount` with a narrow `DocumentCommit` | test plugin `plugins/view_commit`. The core does not embed Vue/React and does not run `<script>` |

Registration is `zui_plugin_<dir>_register`. Static `.obj` files with no live reference
are dropped by the linker, so plugins cannot rely on a static initializer alone. The
default build calls built-in registers from `source/ext/builtin.cpp`.

Default-linked domains: `image`, `clipboard`, `pick`, `shell`, `gamepad`, `tasks`.
Notify, tray, hotkey, media key, instance, association, and update link separately. With
only the core, those functions exist and report that they did not run.

`ZUI_PLUGIN_SHARED` (`AUTO` / `ON` / `OFF`) chooses plugin linkage independently of
`BUILD_SHARED_LIBS`. Plugins do not link `zui`; `zui` links the plugins it needs and
passes the registry into the register function.

The CPU SVG parser is `source/image/svg/parser`, compiled into `zui_image`.
`plugins/image` registers the PNG, JPEG, and SVG drivers and keeps the software-raster
fallback the SVG driver calls. GPU SVG devices stay in `source/image/svg/render` and
are reached through function pointers on `DriverManager`. Window and embedder JNI /
UIKit calls that share the embedder's process lifetime stay in
`source/backends/android` and `source/backends/ios`.
An OS plugin calls that platform's own facility from
`plugins/<domain>/{win32,linux,macos,ios,android}.cpp`.

## 7. Test seam

`frame::Access` is a C-tier friend. Application code does not call it. The seam covers
frame-loop borrow (`attach` / `detach` / `census` / `advance_clock`) and headless
automation that does not flush (`prepare_interaction` / `sync_observation` / `click_*` /
`key` / `wheel_id` / `interactive_ids` / `has_class` / `box_rect` / `box_center` /
`measure` / `computed` / `hit_chain` / `flush_deferred`). `frame::Access::measure` does not flush.
`Document::measure` does.

Frame-buffer readback and style probes also stay on this seam. Macros live in
`source/probe/probe.h` (`ZUI_PROBE`, `ZUI_PROBE_FRAME`). Tests do not reimplement paint,
cascade, or computed-style lookup. See [TESTING.md](./TESTING.md).

## 8. How to add capability

Extend the language. Grow the public header only when the application needs a new C++
entry.

| Want | Change |
| --- | --- |
| New look (radius, shadow) | CSS property + paint |
| New layout mode | CSS property + layout |
| New control tag | hyphenated custom element; CSS default skin; `onclick` as usual; still a `Node` |
| New interaction name | HTML event attribute; input path fires that name |
| New data field | `Document::set` field string + storage on `Node` |
| New window backend | `backends/` `IWindow` |
| New GPU backend | `backends/` `IRenderer` |
| New process service | a service in `include/ext`, reached through `Host::services()` |

A need that exists only in a test fixture stays on the C-tier seam.

## 9. Intra-module files and records

Module folders and the dependency graph in §2 stay as they are. This section
constrains how a module is cut *inside* its folder.

**One algorithm per translation unit.** The model is `source/layout/flex/`:
`items.cpp`, `lines.cpp`, `pack.cpp`, `pass.cpp`, `placement.cpp` each own one
step of the flex algorithm; shared types live in `flex.h` / `detail.h`. A new
file in `html/`, `css/`, `layout/`, `paint/`, or `input/` follows that cut. Do
not grow an existing unit by appending a second algorithm.

**Line cap.** A hand-written `.cpp` / `.h` / `.hpp` / `.cc` / `.hh` under
`source/html`, `source/css`, `source/layout`, `source/paint`, or `source/input`
is at most **700** physical lines (`std::getline` count: a missing trailing
newline still counts). `tests/line_budget.cpp` (`zui_line_budget`) walks those
trees and fails the build's kernel check when a file is over.

**Exempt** from the cap: generated or embedded blob headers — names ending in
`_spv.h` or `_glsl.h`, and headers under a `shaders/` directory that dump
compiled SPIR-V or GLSL. `source/backends/` and `source/app/` are outside this
rule.

**Records group by writer and lifetime.** `html::Node` and `layout::Box` keep
their fields in comment sections that name who writes them and which phase the
values are valid in (structure / cascade / input-frame / layout-written /
edit-caret). Adding a field goes in the matching section; do not append at the
bottom of the type.
