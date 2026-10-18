# zdb

SQLite browser. The same `index.html` runs in a browser and under `zui_zdb`. C++ binds
handlers and fills widgets. SQLite is this example's own link, not a zui API.

Open and export use `Host::pick_open` / `pick_save`. Native also accepts a window file
drop (`<body ondrop="on_drop">`, first path). The original file is read-only in memory;
export writes a new file. "Sample library" builds 竹间书肆. Foreign keys are on after
open. Objects named `sqlite_*` stay out of the sidebar.

The sidebar, data page, structure page, and query page all use `set` / `set_model`.
Query results are a nested model: `#query-tables` (native-only host) stamps one card
per statement; each card's `.ztable` is a nested `TableModel`. Row 0 of that table is
the header. Cells get `{{class}}` and `data-cell` from `cell_value` (`cell-null`,
`cell-blob`, `cell-empty`, `cell-number`, `is-picked`). Clicks read `Event::data("cell")`.
The browser builds its own result DOM and ignores `#query-tables`.

Ctrl+Enter in `#sql-input` runs the same path as Execute (`onkeydown="on_sql_key"`).
The caret stays on the shared prefix; the field is read only when Execute runs.

## Run

```bash
cmake --build --preset windows-x64 --target zui_zdb
build/windows-x64/zdb/zui_zdb.exe
```

The executable lives in `build/windows-x64/zdb/` with its own `index.html`, `css/`, and
`zui.dll`.

Browser (WASM is blocked from `file:`):

```bash
cd example/applications/zdb
python -m http.server 8765
# http://127.0.0.1:8765/   add ?sample=1 for 竹间书肆
```

## Layout

| Path | Role |
| --- | --- |
| `index.html`, `css/` | Shell. `onclick` names bind in C++; `js/bridge.js` no-ops them in the browser |
| `js/sql/` | Browser sql.js: open, catalog, page, execute, sample |
| `js/ui/`, `js/app/` | Browser paint and state |
| `cpp/` | Native: pickers, SQLite, `set` / `set_model` |
| `sqlite/` | SQLite 3.49.1 amalgamation, linked only into `zui_zdb` |
| `vendor/` | sql.js for the browser |
| `main.cpp` | load, bind, run |
