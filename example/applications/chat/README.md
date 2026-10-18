# Chat

Local conversations. Switch threads, send, create, delete. There is no server.

Scrolling up stays put. Sending or "jump to bottom" pins to the latest message.
"Jump to bottom" uses `measure()` (`scroll_height` vs `client_height`, 64 px slack).
Pinning to the end writes a large `scroll_top`; the frame loop clamps it.

## Run

```bash
cd example/applications/chat
python -m http.server 8768
# http://127.0.0.1:8768/
```

```bash
cmake --build --preset windows-x64 --target zui_chat
build/windows-x64/chat/zui_chat.exe
```

## How it is cut

The same HTML and CSS run in the browser and in zui. zui does not run `js/`. Native
handlers live in `cpp/` and bind the same `onclick` names.

Native data is `user_data_dir()/native-data/chat.json`. The browser uses localStorage
`xiaozhu-chat`. Sample threads stay off disk until the user edits.

The draft box is `<textarea onkeydown="on_draft_key">`. Enter sends
(`prevent_default()`). Shift+Enter inserts a newline. IME Enter (`Key::composing`)
commits composition and does not send. Escape on `<body onkeydown="on_accel">` closes
the confirm / new-thread dialogs.

Typing does not write the draft back. `set(id, "value")` keeps the caret on the shared
prefix.

```
index.html
css/           tokens, shell, thread, motion, theme
js/            boot, bridge, store, view
main.cpp
cpp/           session, store, rows, list/log views
```
