# Contacts

Local address book. Search by name, phone, or workplace. Filter by group (colleague,
family, friend, other) or starred. Name is required; email is checked. "Restore
samples" resets the list.

## Run

```bash
cd example/applications/contacts
python -m http.server 8767
# http://127.0.0.1:8767/
```

```bash
cmake --build --preset windows-x64 --target zui_contacts
build/windows-x64/contacts/zui_contacts.exe
```

## How it is cut

The same HTML and CSS run in the browser and in zui. zui does not run `js/`. Native
handlers live in `cpp/` and bind the same `onclick` names.

Native data is `user_data_dir()/native-data/contacts.json`. The browser uses
localStorage `xiaozhu-contacts`. Sample rows stay off disk until the user edits.

Group is `<select id="f-group">`. Native hides the `#group-picks` chip row. Phone and
email in the detail pane are `<a href>` with `onclick="on_open_uri"`: C++ writes
`tel:` / `mailto:` then `Host::open_uri` and `prevent_default()`. Empty href, `#`, and
`javascript:` do not open.

Search redraws the list on `oninput` and does not write the field back. Escape on
`<body onkeydown="on_accel">` closes confirm, then the editor.

```
index.html
css/           tokens, shell, people, motion, theme
js/            boot, bridge, store, view
main.cpp
cpp/           session, store, editor, filter, rows
```
