# Calendar

Local month / week agenda. Create, edit, delete. Overlapping events sit side by side
in the week view.

Date and time fields are plain text (`YYYY-MM-DD`, `HH:MM`). Native has no click
coordinates on empty month cells: a click on a day column in week view uses
`Event::is_pointer()` plus `measure()` to pick the hour (44 px per hour). Keyboard
activation opens 09:00–10:00.

## Run

```bash
cd example/applications/calendar
python -m http.server 8769
# http://127.0.0.1:8769/
```

```bash
cmake --build --preset windows-x64 --target zui_calendar
build/windows-x64/calendar/zui_calendar.exe
```

## How it is cut

The same HTML and CSS run in the browser and in zui. zui does not run `js/`. Native
handlers live in `cpp/` and bind the same `onclick` names.

Native data is `user_data_dir()/native-data/calendar.json`. The browser uses
localStorage `xiaozhu-calendar`. Sample events are relative to "today" and stay
off disk until the user edits.

The month grid is one `<template>`: 7 weekday headers and 42 day buttons. Each day
shows at most three titles (`{{i0}}`–`{{i2}}` as `data-id` on inner `<span>`s). A
title click fires `on_event`; the day button fires `on_day`. A `.cell-inner` wrapper
keeps the stretched `<button>` from vertically centering its contents.

Week blocks write `top` / `height` / `left` / `width` through template `style`
placeholders. Identity is `data-id` / `Event::data("id")`. Header ISO dates still
travel as `value` on those nodes.

```
index.html
css/           tokens, shell, board, motion, theme
js/            boot, bridge, dates, store, view
main.cpp
cpp/           session, board, editor, store, dates, rows
```
