# Share

LAN file send and receive. Two machines on the same network discover each other, send,
receive, and resume. This is a full port of the earlier SDL3 + Dear ImGui `xz-share`
stack onto zui: HTML/CSS for the screens, C++ for files and the network.

- Files: directory listing, parent / drive root, recursive share expand, collision
  rename, FNV-1a fingerprint (`cpp/util.*`).
- Network: UDP broadcast / multicast discovery, TCP control, up to 8 parallel 1 MB
  data connections, `.xzpart` + `.xzmap` resume (`cpp/net.*`).
- UI: seven screens plus an incoming-offer layer. Identity and the receive directory
  live in `user_data_dir()/config.txt`.

A flat `ListModel` host with 1000+ rows (`#file-list`) windows to 32 stamped rows.
Progress width is `style="width:{{pct}}%"` on a one-row model. `Session::go()` switches
screen classes on `#app`.

## Run

```bash
cmake --build --preset windows-x64 --target zui_share
build/windows-x64/share/zui_share.exe
```

The window opens at 1100×760. Below 760 px, `@media (max-width: 760px)` switches to the
phone layout. Two processes on the LAN: name → pick files → share → pick device → wait
→ transfer → done. The other side shows the incoming layer.

Browser (same page, sample data):

```bash
cd example/applications/share
python -m http.server 8791
```

## Bench

Filesystem handlers without a window:

```bash
build/windows-x64/zui_app_interact.exe --gtest_filter=AppInteract.Share*
```

Network, same binary:

```bash
zui_share.exe bench recv <recv-dir> <port-file>
zui_share.exe bench send <file>
zui_share.exe bench sendto 127.0.0.1 37211 <file>
zui_share.exe bench gen <file> 256M
zui_share.exe bench check <recv-dir>/bench.bin 256M
zui_share.exe bench partial <recv-dir> <file> bench.bin 0.5
```

## Screens

| Screen | Class |
| --- | --- |
| Name | `.screen-name` |
| Browse (narrow / desktop) | `.screen-browse` |
| Pick device | `.screen-pick` |
| Wait | `.screen-wait` |
| Fail | `.screen-fail` |
| Transfer | `.screen-transfer` |
| Done | `.screen-done` |
| Settings | `.screen-settings` |
| Incoming offer | `.screen-incoming` |

## Native objects

`App::bind` closes over `Session` only.

- `FileBrowser` — cwd, entries, checks. No `zui::App`, so it tests on a temp directory.
- `Config` — identity, receive dir, `config.txt`.
- `net_*` — one process-wide transfer service on background threads. Snapshots
  (`net_peers` / `net_xfer` / `net_incoming`) plus `net_offer` / `net_respond` /
  `net_cancel`. `Session::poll` on a 200 ms interval copies those snapshots onto the
  document on the main thread.
- `Session` — `go(Screen)`, one `render_*` per screen, one `on_*` per HTML bind name.
  `set` / `set_model` / `add_class` only.

```
index.html         screens, templates
css/               tokens, shell, browse, xfer, motion, theme
main.cpp           load + Session; `bench` subcommand
cpp/util.*         Config, DirEnt, ShareFile
cpp/net.*          discovery and transfer
cpp/browser.*      FileBrowser
cpp/rows.*         ListModel subclasses
cpp/session.*      screens, bind, poll
cpp/bench.*        recv / send / sendto / gen / check / partial
js/                browser demo
```

## Packaging and device smoke

### Windows installer (`zui-pack`)

```bash
cmake --build --preset windows-x64 --target zui_pack
zui-pack example/applications/share --root <ZUI_ROOT> --build <ZUI_BUILD>
```

Expected dist artifacts: `portable.zip` and `setup.exe` (installer).

### Android two-device smoke

1. Same LAN: both devices appear in each other's peer list (multicast / discovery permissions).
2. Pick files — send — peer accepts — files land in the receive directory.
3. Reject once — sender reaches Fail — Back returns to Browse.

Confirm Manifest contains `CHANGE_WIFI_MULTICAST_STATE` (e.g. `aapt dump permissions` on the APK).
