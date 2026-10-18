# Album

Local photo library. Photos sit in albums. Opening one shows a lightbox that is the photo
and its two paging arrows: a click on the photo closes it, and so does Escape. `信息` (the
`i` key) opens the tools over the photo - the bar with the name and the zoom buttons, the
filmstrip, and the form that edits the caption and the album. Five zoom classes (`z1`–`z5`)
size the photo; `#stage` pans by pointer drag while it is zoomed. Captions and album
membership can change while the tools are open.

Six sample SVGs ship in `pictures/` (ferry, lighthouse, coast, bamboo, night, paper)
across three albums. First launch writes them. "All" is a view of every photo.
"Unsorted" holds imports that landed in All and photos from deleted albums. Those two
albums cannot be deleted. "Restore samples" drops imports and returns to the six.

## Run

```bash
cd example/applications/album
python -m http.server 8771
# http://127.0.0.1:8771/
```

```bash
cmake --build --preset windows-x64 --target zui_album
build/windows-x64/album/zui_album.exe
```

## How it is cut

The same HTML and CSS run in the browser and in zui. zui does not run `js/`. Native
handlers live in `cpp/` and bind the same `onclick` names. `js/bridge.js` fills those
names with no-ops in the browser.

Native import uses `Host::pick_open`. Accepted suffixes are PNG, JPEG, and SVG. Files
copy into `user_data_dir()/album-photos/`; the catalog is
`user_data_dir()/native-data/album.json`. The browser uses IndexedDB `xiaozhu-album`
and refuses a single file over 12 MB. The two stores do not share records.

Lightbox album options come from a `<template>` inside `<select id="light-album">` via
`set_model`. `#album-picks` stays in the page; native hides it. Ctrl/Cmd+wheel on
`#stage` zooms (`onwheel` + `prevent_default`). Body `onkeydown="on_accel"` pages,
zooms, deletes, and opens the tools with `i`.

The tools are one disclosure: `#light-inspector` (信息) turns one `:has()` rule that hides
the bar, the filmstrip and the form, so the lightbox opens on the photo alone. Native flips
the same checkbox from `i`; the panel's own 信息 control closes it again.

```
index.html
css/           tokens, shell, gallery, motion, theme
js/            boot, bridge, db, view
pictures/      six sample SVGs (POST_BUILD-copied next to the exe)
main.cpp
cpp/           session, gallery, lightbox, store, rows, pick
```
