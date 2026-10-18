# zmd

Minimal Markdown editor. `index.html` is the prototype screen copied whole: the same
`#md-editor-mock`, the same `mdw-*` classes, the same `data-act` / `data-fmt` /
`data-set` declarations. `css/app.css` and `js/app.js` are the prototype files
unchanged. zmd's own layer (source / preview / find / three dialogs / view classes)
lives in `css/zmd.css` and `js/zmd.js`.

The browser and zui load the same HTML/CSS. zui does not run scripts, so native
behavior is rewritten in `cpp/`.

## Run

```bash
cd example/applications/zmd
python -m http.server 8773
# http://127.0.0.1:8773/
```

```bash
cmake --build --preset windows-x64 --target zui_zmd
build/windows-x64/zui_zmd.exe
```

The browser port is 8773 so it does not collide with calendar. POST_BUILD copies
`index.html`, `css/`, `js/`, `samples/`, and `icon.svg` next to the executable. A path
argument opens that file; no argument opens the welcome sample.

## How native maps the prototype

`Document::bind` keys on HTML event-attribute *values* (`onclick="on_format_bold"`).
It does not read `data-act`. The page keeps `data-act` / `data-fmt` / `data-set` so
`js/app.js` still runs in the browser, and adds `on*` on the same controls so the
native session receives the click. `js/zmd.js` installs empty `window` stubs for those
names; the browser listener stays the one that acts.

File rows, outline rows, and palette rows are `<template>` hosts (`#mdc-a-files`,
`#mdc-a-outline`, `#mdc-a-pal-list`). `set_model` stamps them. Settings writes classes
the shared sheet already selects (`w660`, `sz16`, `lh172`, `mdw-serif`, `is-focusing`,
`theme-dark`). Colour and corner radius are custom properties in the shared sheet, so
they are steps too: the accent snaps to a twelve-entry palette and the class is named
after the hex the swatch shows (`acc-4a6cf7`), the radius snaps to 4px (`rad12`), and
both controls are written back with the value that was applied. The sheet's values are
stored in `settings.txt` under the host user-data directory.

The writing surface the A-tier can read (`value` / `caret` / `selection`) is the
textarea `#mdc-a-source`. Native adds `zmd-host` on `#md-editor-mock`, and the shared
sheet then shows the rendered preview in 写作 instead of the contenteditable article.
写作 paints the caret itself: the render plants the `zmd-caret` marker in the block that
holds the source offset, and the parked source box asks for no caret of its own
(`caret-color: transparent`; the frame paints none for it), because that box is a
2%-opacity keyboard/IME proxy at the paper's corner and its bar would sit nowhere near
the writing.
Preview HTML is a scratch file under the user-data directory, loaded into
`#mdc-a-preview` with `replace_fragment` - the render names blocks by index, so an
appending load would keep the blocks a shorter render no longer has; a relative image
or link is rendered against the document's own folder, since the scratch file is not
next to the document.

`<body>` keeps `ondrop`: a dropped document opens, and any other dropped file is copied
into the settings' assets folder and inserted as a picture. A dropped folder, or a copy
that does not land, inserts nothing - a link to a picture that is not there renders broken.

The file panel shows one folder at a time (`Workspace::folder`): a folder row enters it, the
panel title names it, and 返回上一级 (only away from the root) walks back up. 新建文件 /
新建文件夹 land in the folder on screen, and `新建位置` chooses between that folder and the
workspace root.

The outline panel shows the reading position. The scroller's `onscroll` runs the prototype's
`syncOutline` rule (the last heading whose box top is at or above the viewport top plus 64px
becomes the current row) and sets the bar's `prog0` … `prog100` class, which the sheet maps to
`--w-progress` - the class pattern the accent swatch and the radius slider use, since the
surface writes no style values.

写作 follows the write: two frames after an input that moved the caret, the preview block whose
`data-beg` is the nearest at or before the caret goes through `scroll_into_view`. 写作 asks for
`ScrollAlign::Nearest`, so the paper only moves when the caret leaves the fold; 打字机模式 (the
palette command) asks for the middle instead. 源码 and 分栏 leave the paper alone - their caret
belongs to the textarea, which the engine keeps in view itself. A scroll the reader made inside
those two frames wins: the callback remembers the offset it queued against and drops itself when
that offset has moved.

PageUp / PageDown in 写作 move the paper a page. The surface that takes the keyboard there is the
2% proxy at the paper's corner, whose own scroll is invisible, and the browser prototype's
contenteditable scrolls the page for the same key. 源码 and 分栏 show the textarea itself, which
the engine's page key handles: the control's own text when the box is a fixed height, the pane
when it grows with its value.

The palette's format commands (加粗 / 斜体 / 行内代码 / 高亮) act on the selection the palette was
opened over: `Document::selection` belongs to the whole document and typing in the palette's
field replaces it, so the session remembers the range (and the text it covered) and puts it back
before the command runs. 高亮 is `==text==`, rendered as `<mark>`, the sheet's mark token.

查找 counts its hits and the preview shows them: `write_preview` hands the renderer the hit
ranges, and the inline pass wraps each one in `<mark class="mdw-f-mark">` - the sheet's find
mark. Closing the bar re-renders without them. A find jump also scrolls the hit into view:
typing the query, 下一个, and 上一个 move the caret to the hit and run the same
`data-beg` block scroll as 打字机模式, so the block that owns the selection is brought
into the viewport.

The find bar and the palette focus their field one frame after they open: the `hidden` /
`pal-closed` write clears the computed `display` only in that frame's style flush, and a focus
asked for in the same turn is refused. The sheet's edit keys (加粗 / 斜体 / 行内代码 /
撤销 / 重做) answer only while the source textarea has focus - the prototype's
`document.activeElement` guard - which the textarea reports through `on_source_focus` /
`on_source_blur`; every other shortcut is global.

`onvisibilitychange` on `<html>` dims the sheet while the window is away (`is-blurred`, the
prototype's `window blur`): the event says the host visibility moved and `Window::visible()`
says where it went.

Lucide `<i data-lucide>` is replaced in the browser by `js/icons.js`. Native paints the
`.mdw-glyph` label; the browser hides that glyph once the svg exists.

One class per `cpp/*.h` (`command.h` holds `Command` plus two list models).
`Markdown`, `Document`, `Editor`, `Workspace`, and the models do not include zui.
`Session` is the only type that knows `zui::App`.

## Tests

- Logic: `zmd_editor_handlers_test.cpp`, `zmd_files_handlers_test.cpp`,
  `zmd_find_handlers_test.cpp`
- Page writes: `zmd_chrome_handlers_test.cpp`, `zmd_page_handlers_test.cpp`
- Smoke: `AppInteract.Zmd*`
- Daily editor: `zmd_daily_test.cpp` (launch, editing, files, chrome)
- Browser walk: `ZmdPageMatchesTheBrowser`

```
index.html          prototype screen + zmd elements
css/app.css         prototype sheet
css/host.css        box-sizing, [hidden], icon-size hook
css/zmd.css         views, find, dialogs
js/app.js           prototype script
js/icons.js         lucide shim
js/zmd.js           views, find, dialogs, files
samples/welcome.md
cpp/dom.h           live ids and class tokens
cpp/later.h         Document::defer behind the owner that armed it
cpp/session.*       load, bind, open
cpp/markdown.*      Markdown → fragment, outline, counts
cpp/document.*      body, path, dirty, undo
cpp/editor.*        caret, wrap, line-start rules
cpp/workspace.*     one-level file list
cpp/chrome.*        title, status, typing timer
cpp/panels.*        pin state, outline rows, progress
cpp/settings.*      view classes
cpp/palette.*       command table
cpp/find.*          hits
```
