# Writer

Local notes. The shell is `index.html`. After open, the writing pane is the only
surface. Hovering the left edge shows the document tree; hovering the right edge shows
the current piece's outline. The bottom bar holds account, settings, today's progress,
and page switches.

Desk, calendar, history, bar, and storage are HTML fragments. `show` loads them with
`load_fragment` into `#stage` or `#dock`. The browser `fetch`es the same files into the
shell (`file:` URLs cannot). There is no History API, so the address bar does not back
through these pages.

The editor is a `<textarea>`. Format buttons insert markdown markers at the caret or
selection (`insert` / `select`). Reading mode copies the body into `#reader` and hides
plus disables `#editor`. History snapshots are the saved plain text.

On a phone there is no hover: the "目录" button toggles class `show-tree`. C++ does not
write geometry.

## Run

```bash
cd example/applications/writer
python -m http.server 8766
# http://127.0.0.1:8766/
```

```bash
cmake --build --preset windows-x64 --target zui_writer
build/windows-x64/writer/zui_writer.exe
```

The browser port is 8766 so it does not collide with calendar on 8769. POST_BUILD copies
`pages/` next to the executable.

## How it is cut

The same HTML and CSS run in the browser and in zui. zui does not run `js/`. Native
handlers live in `cpp/` and bind the same `onclick` names. New handlers are bound in
both `cpp/session.cpp` and `js/view.js` / `js/bridge.js`.

Native pieces live in a chosen directory (default `user_data_dir()`): `library.json`,
`pieces/*.txt`, `history/<id>/*.txt`. The storage-location field submits a path. The
browser uses localStorage keyed by that location. The two stores do not share records.

Tree rows use depth classes `d0`–`d6` (capped at 6). Those class names stay so geometry
tests keep matching.

```
index.html
pages/         desk, calendar, history, bar, storage
css/           tokens, shell, workspace, motion, theme
js/            boot, bridge, store, view
main.cpp
cpp/           session, desk, store, storage, edits, views, rows
```
