# Examples

Application programs live in `applications/`. Kernel fixtures live in `tests/`.

Each application is HTML and CSS for structure and skin, plus C++ that binds handlers
and writes data. Most also keep a `js/` tree so the same page runs in a browser; zui
does not execute those scripts.

```bash
cmake --build --preset windows-x64 --target zui_hello
build/windows-x64/zui_hello.exe
```

`zui_add_app` programs land in `build/windows-x64/<name>/`. `zui_reader` and `zui_zmd`
land next to `zui.dll` in `build/windows-x64/`.

| Directory | Target | What |
| --- | --- | --- |
| [applications/album](applications/album/README.md) | `zui_album` | Photo library |
| [applications/calendar](applications/calendar/README.md) | `zui_calendar` | Month / week agenda |
| [applications/chat](applications/chat/README.md) | `zui_chat` | Local conversations |
| [applications/contacts](applications/contacts/README.md) | `zui_contacts` | Address book |
| [applications/kanban](applications/kanban/README.md) | `zui_kanban` | Nested list board |
| [applications/particle](applications/particle/README.md) | `zui_particle` | Parameterized particle field |
| [applications/reader](applications/reader/README.md) | `zui_reader` | Text-book shelf |
| [applications/share](applications/share/README.md) | `zui_share` | LAN file send / receive |
| [applications/writer](applications/writer/README.md) | `zui_writer` | Notes with fragments |
| [applications/zdb](applications/zdb/README.md) | `zui_zdb` | SQLite browser |
| [applications/zmd](applications/zmd/README.md) | `zui_zmd` | Markdown editor |
| [applications/file-explorer](applications/file-explorer/README.md) | — | Browser-only file mock |

`file-explorer` has no native session. Fixture programs are listed in
[tests/README.md](tests/README.md).

New handlers on a dual-runtime page are bound in `cpp/` and mirrored as no-ops in
`js/bridge.js` (and the matching view script). Native data lives under
`App::user_data_dir()`. Browser storage is localStorage or IndexedDB. The two stores
do not share records.
