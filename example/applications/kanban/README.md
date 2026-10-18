# Kanban

Local task board. Cards move between columns. Columns reorder. Adding a card happens
at the bottom of a column. Opening a card edits title, notes, or column. Deleting a
column moves its cards to a neighbour.

## Run

```bash
cd example/applications/kanban
python -m http.server 8770
# http://127.0.0.1:8770/
```

```bash
cmake --build --preset windows-x64 --target zui_kanban
build/windows-x64/kanban/zui_kanban.exe
```

## How it is cut

The same HTML and CSS run in the browser and in zui. zui does not run `js/`. Native
handlers live in `cpp/` and bind the same `onclick` names.

Native data is `user_data_dir()/native-data/kanban.json`. The browser uses localStorage
`xiaozhu-kanban`. Sample boards stay off disk until the user edits.

`#board` holds a column `<template>`; each column's `.cards` holds a nested
`<template>`. C++ hands `Board::with_children` to `set_model`. Column and card identity
is `data-id` / `Event::data("id")`. The browser's first `replaceChildren()` drops those
templates and sorts with pointer code in `js/view.js`.

Native drag is HTML drag-and-drop: `draggable="true"` on the column handle and on
cards, `prevent_default()` on `dragover`, insert classes `drop-before` / `drop-end`.
The engine draws the drag ghost. The board auto-scrolls near its left and right edges.
"Move up" / "Move down" still swap inside a column without dragging.

Column names write the model on `oninput` and do not rebuild the board (that would
kill the caret). Empty names are ignored. The card editor's column picker is
`<select id="f-column">` filled by `set_model`; native hides `#column-picks`.

```
index.html
css/           tokens, shell, board, motion, theme
js/            boot, bridge, store, view
main.cpp
cpp/           session, store, editor, drag
```
