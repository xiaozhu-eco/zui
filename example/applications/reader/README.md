# Reader

Text-book shelf and pager. `reader.html` is the page. A shelf of `.txt` / `.epub` / `.pdf` files sits in
`books/` next to the executable (created on first run). Opening a book pages it;
find, marks, and heading jumps use HTML fragments under `fragments/`.

The same page plus `reader.js` runs as a web app (`manifest.webmanifest`, `sw.js`).
zui does not run that script. Native handlers live in `cpp/` (`on_accel`, shelf
search, sort, theme).

`zui.json` is the packer declaration (portable / installer / Android). The Android
shared library is `zui_reader_android`, output name `main` (`libmain.so` for SDL).

## Run

```bash
cmake --build --preset windows-x64 --target zui_reader
build/windows-x64/zui_reader.exe
```

`--count-chapters [dir]` and `--dump-chapters [path]` print chapter tables without a
window. POST_BUILD copies `reader.html`, `reader.css`, `reader.js`, `chapter.html`,
`manifest.webmanifest`, `sw.js`, `icon.svg`, and `fragments/` next to the exe.

Browser:

```bash
cd example/applications/reader
python -m http.server 8774
# http://127.0.0.1:8774/reader.html
```

## Layout

```
reader.html          shelf + pager
reader.css
chapter.html
fragments/           hits, marks, measure
cpp/book.*           load, codec, headings, text
cpp/session.*        bind, shelf, pager
cpp/shelf*.cpp       catalog, query, order
cpp/find.cpp
cpp/page_mode.cpp
cpp/nav.cpp
zui.json             packer
main.cpp
```

## Persistence

- `reader-progress.txt` (next to the exe): tab-separated
  `key`, `chapter`, `opened_at`, `page`, `scroll`, `para`, `ch`.
  Older 3-field lines still load. Scroll mode restores `scroll`; page mode restores `page`.
- `reader-marks.json`: bookmark array of `{id,key,ci,pi,ch,page,scroll,label,snip,t}`.
  Mark / find-hit click restores chapter + `#paN` paragraph (or page/scroll fallback).
- Progress autosaves ~400ms after scroll/page changes while a book is open.
