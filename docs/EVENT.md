# Events and models

Frozen against `include/zui/event.h`. New kinds append `is_*()` payloads. Existing field
meanings stay. There is no flat kitchen-sink `Event` and no fat `Handler` vtable.

zui has no `<script>`. Every application reaction to input goes through `bind`.

Chrome comparison covers structure and style. `onclick="on_save"` is not JavaScript, so
behavior has no browser oracle. Dispatch assertions are engine self-checks
([TESTING.md](./TESTING.md)). Borrow semantics from HTML / UI Events when they exist.
Invent as little as possible.

## 1. Author model

1. HTML writes a handler name on a standard event attribute.
2. C++ binds that name to a callback.

```html
<button id="send" onclick="on_send" onkeydown="on_send">Send</button>
```

```cpp
app.document().bind("on_send", [](zui::Event& e) { /* ... */ });
```

Invariants:

- The receiving element is chosen by HTML (which element wrote `on*`).
- The handler name is the only string that joins HTML and C++. Names are free.
- `Document::bind` keys on the handler name. It does not key on element id. The same
  name may appear on many elements and many event kinds.
- The callback stays `std::function<void(Event&)>`.
- `Event` reports kind and payload for this firing. It is not copyable and is valid only
  until the callback returns. Payload is read-only. The only mutable state is
  `prevent_default()`.

## 2. Five questions

| # | Question | Answered by | On `Event` |
| --- | --- | --- | --- |
| 1 | Which callback runs | HTML attribute *value* (handler name) | `name()`; `bind` table |
| 2 | Which element fired | hit-test + the element that wrote `on*` | `source()`, `data()` |
| 3 | What kind of event | which `on*` attribute fired | `type()` |
| 4 | What data this firing carries | kind | `is_key()` / `is_pointer()` / `is_wheel()` / `is_drop()` / `is_drag()` / `value()` |
| 5 | Whether the engine still does its default | after the callback | `prevent_default()` |

```text
HTML  onclick="on_send"   onkeydown="on_send"
         kind=click          kind=keydown
         name=on_send        name=on_send
              └────── bind table "on_send" ──────┘
                         Event&
```

`bind(id, handler)` would split the structure source. A required `Handler` base class
would freeze the set of kinds in a public vtable.

Identity of a list row lives on `data-*`, read with `Event::data("id")`. `value()` is the
control payload (textbox text, checkbox `"true"` / `"false"`), not the row id.

## 3. Payloads

`type()` is the HTML attribute name without the `on` prefix (`onclick` → `"click"`).

| Kind | Predicate | Payload |
| --- | --- | --- |
| `keydown` / `keyup` | `is_key()` | `Key`: `key` as `KeyboardEvent.key`, modifiers, `composing` |
| `click` / `contextmenu` / `auxclick` / `pointermove` / `mousemove` / `pointerdown` / `pointerup` | `is_pointer()` | `Pointer`: CSS-px coordinates, button, buttons mask, click count, modifiers, pointer id / type / pressure |
| `wheel` | `is_wheel()` | `Wheel`: CSS-px deltas (positive `delta_y` increases `scrollTop`), modifiers |
| window file drop | `is_drop()` | `Drop`: UTF-8 paths |
| `dragstart` / `drag` / `dragenter` / `dragover` / `dragleave` / `drop` / `dragend` | `is_drag()` | `Drag`: CSS-px, source id, text/plain, uri-list, drop effect. `drag_data()` reads `data-*` on the dragged element |

Coordinates are CSS logical pixels, the same space as layout and hit-test. `ctrl` is the
primary accelerator (Command on macOS).

`prevent_default()` cancels the engine default after the callback returns:

- `beforeunload` does not close
- Enter in a `<textarea>` does not insert a newline
- `wheel` does not scroll
- `dragstart` does not start a drag
- a cancelled `dragover` is what *allows* the drop (HTML default of `dragover` is reject)

Parse collects every `onxxx="handler_name"` on the element into `(kind, name)` pairs.
That is a language extension in `html/`. `include/` does not grow a new event API for a
new standard kind.

## 4. Models

Scalar widgets are not models. A progress bar, a slider, a `<meter>` use
`set(id, "value", …)` and the engine's default control behavior.

A model is used when structure repeats, the repeat count comes from data, and C++ is the
only data source. Selection, sort, filter, group, and pagination stay in the data the
application already holds (model fields, or a second `set_model`).

Three shapes. A-tier grows no fourth type. A tree is a list of lists.

| Shape | Type | Template |
| --- | --- | --- |
| Linear | `ListModel` | one `<template>` under the host |
| Table | `TableModel : ListModel` | row template; optional per-column cells |
| Nested | `ListModel::with_children(row, visit)` | inner `<template>`'s parent is the nested host |

`row_key(row)` is stable identity. Empty (the default) matches by position. Equal
rendered rows are kept; an insert or move does not re-stamp the rest.

`value(row, key)` fills `{{key}}` in text and attributes. The engine also supplies
`{{index}}` (0-based) when `value` returns empty. Templates that contain `{{index}}` or
`{{row}}` re-stamp those rows on reorder.

`with_children` is called at most once per row, inside the `set_model` that asked, and
the engine does not keep the nested model. Not calling `visit` means the row has no
nested content and retires rows already stamped there. A kept parent still reconciles
children this way. The nested model may be a `TableModel`.

`TableModel::cell_value(row, column, key)` is how a cell gets its own class or `data-*`.
The default maps `"text"`, `"key"`, and `column_key()` onto `cell()`.

`set_model` queues the host only (`pending_attach`). The rebuild root is that host. Created
boxes stay inside rows whose rendered values changed. Re-applying an equal model is a
no-op ([PERF.md](./PERF.md) I2, I4, I5).

Ids inside a stamped row enter the global id table; later rows overwrite earlier ones.
Row identity for events therefore lives on `data-*`, not on stamped `id`.

## 5. Selection

Document Selection is two boundary points (`anchor`, `focus`) on `html::Document`.
Collapsed is the caret; a range is the highlight. Pointer drag, Shift+arrow,
double-click (word), and triple-click (line) write it on editing hosts and on
ordinary laid-out text. Geometry is the Range↔line-box mapping
([LAYOUT.md](./LAYOUT.md) §6). `select` fires when an editable range becomes
non-collapsed. `contenteditable` insert / delete / wrap split and wrap existing
nodes at that range. IME composition stays on the focused control.
