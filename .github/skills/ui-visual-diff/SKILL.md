---
name: ui-visual-diff
description: 'Same-scale visual self-test for zui pages. Use after changing layout, cascade, text metrics, paint, or hit testing, or when the rendered page "looks different from Chrome". Renders each example page in our own app AND in a real browser at the same client size (1100x720), pixel-diffs the two images, reports a per-page difference percentage plus the hottest regions, and compares against a stored baseline to catch UI regressions. A second command dumps every box our layout engine produced and every browser element rect for the same page, joins them by id / tag+class, and prints the per-box dx/dy/dw/dh delta. Triggers: 界面自测, 截图对照, 盒子对照, 和 Chrome 对比, 布局回归, 渲染差异, visual regression, pixel diff, screenshot compare, box compare, geometry diff.'
argument-hint: '[-Page hello,checks | -All] [-UpdateBaseline] [-Tol 1.5] [-ScrollId id -ScrollTops 0,0.5,1] [-FullPage] | box-compare.ps1 -Page hello [-AfterBind] [-WaitMs 2000] [-AppDumpPath path]'
---

# Same-scale visual self-test

Two commands, both at the same 1100x720 client size, both meant to be run after a change:

- **[visual-diff.ps1](./scripts/visual-diff.ps1)** renders the page in our own app *and* in headless
  Edge/Chrome, then diffs the two pictures pixel by pixel. It says *that* a page differs, and where.
  With `-UpdateBaseline` the percentages become a regression check.
- **[box-compare.ps1](./scripts/box-compare.ps1)** dumps every box our layout engine produced and
  every element rect the browser produced for the same page, joins them by `#id` / `tag.class`, and
  prints `dx dy dw dh` per box. It says *which box* is wrong and by how much.

Use the picture pass first (cheap, catches paint/metrics too), then the box pass to localise the
boxes behind the hot cells.

## When to use

- After touching `source/layout/**`, `source/css/**`, `source/paint/**`, `source/text/**`, or the
  fonts: does the picture still match Chrome at the same size?
- A page "looks off" and you need to know whether the deviation is layout, paint, or metrics.
- Before/after a fix, to prove the difference percentage went down.

## Prerequisites

- Windows shell with `System.Drawing` (Windows PowerShell 5.1 is fine) and Edge or Chrome installed.
- A built tree: `build/windows-x64/zui_<page>.exe` plus the example assets copied next to it.
  Build first (`cmake --build build/windows-x64`) — the example executables load the HTML you pass
  them, but the CSS resolution follows the file, so passing the source path is what the script does.
- `build/windows-x64/zui_ui_probe.exe` for the box / scroll harness (preferred): 
  `cmake --build build/windows-x64 --target zui_ui_probe`. Same static layout path as `zui_dump`
  (links internal `source/`), plus scroll via `html::Node::scroll_x` / `scroll_y` and
  `LayoutEngine::sync_world()` — **not** the public App API, and nothing new under `include/`.
  CLI: `zui_ui_probe <page.html> [--size 1100x720] [--brief] [--list-scrollers]
  [--scroll-id id] [--scroll-top v] [--scroll-left v] [--scroll-fraction]
  [--scroll-tops a,b,c] [--out path] [--wait-ms n] [--dom]`.
  `--brief` prints `box|tag|id|class|x|y|w|h|content_w|content_h` (what `box-compare.ps1` parses).
  `--scroll-tops` emits `scroll|id|y|max|index` then a brief frame per value for visual-diff splits.
  `zui_dump` remains available as a fallback; prefer the probe going forward.
- No `vcvars64.bat` needed: the Windows preset is clang-cl, which locates the MSVC toolchain and the
  Windows SDK itself. (A build directory created with the old cl.exe preset must be recreated once:
  `cmake --preset windows-x64 --fresh`.)

## Procedure

1. Run [visual-diff.ps1](./scripts/visual-diff.ps1):

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .github/skills/ui-visual-diff/scripts/visual-diff.ps1 -Page hello
   powershell -NoProfile -ExecutionPolicy Bypass -File .github/skills/ui-visual-diff/scripts/visual-diff.ps1 -Page hello,checks
   powershell -NoProfile -ExecutionPolicy Bypass -File .github/skills/ui-visual-diff/scripts/visual-diff.ps1 -All
   ```

   Page names are comma-separated after `-Page` (`powershell -File` passes them as one string).

   One page per run is fastest to read; `-All` takes ~1 minute because `studio` needs ~30 s for its
   first frame (its model binds a 50–100 × 50–100 table). Add a page to the `$pages` table in the
   script when a new example appears.

   Multi-scroll / full-page (opt-in; default remains a single 1100x720 viewport shot):

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .github/skills/ui-visual-diff/scripts/visual-diff.ps1 -Page hello -ScrollId content -ScrollTops 0,0.5,1
   powershell -NoProfile -ExecutionPolicy Bypass -File .github/skills/ui-visual-diff/scripts/visual-diff.ps1 -Page flow -FullPage
   ```

   Artifacts: `<page>.{app,web}.sN.png`, `<page>.diff.sN.png`; full-page stitch `<page>.{app,web,diff}.full.png`;
   app geometry frames `<page>.app.sN.boxes.txt` from `zui_ui_probe`.
   Browser scrolling uses `assets/scroll-capture.html`. App **PNG** has no public scrollTop API —
   multi-scroll PNG may stay at s0 (noted in the row); `-FullPage` PNG uses best-effort Win32
   PageDown. **Geometry alignment** always prefers `zui_ui_probe` scroll steps (internal Node scroll).

2. Run the box pass on the same page:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .github/skills/ui-visual-diff/scripts/box-compare.ps1 -Page hello
   powershell -NoProfile -ExecutionPolicy Bypass -File .github/skills/ui-visual-diff/scripts/box-compare.ps1 -All -Tol 1.5 -Top 40
   ```

   It runs `zui_ui_probe <page> --brief --size WxH` (falls back to `zui_dump`), loads the same page inside a 1100x720 iframe in
   headless Edge (`assets/box-probe.html`, which needs `--allow-file-access-from-files`), reads back
   `getBoundingClientRect()` for every element, and joins the two lists. It prints
   `key / browser (x,y,w,h) / zui (x,y,w,h) / dx dy dw dh` sorted by the largest delta, then a
   summary `matched / differing / only-browser / only-zui`, and writes the full lists to
   `build/box-compare/<page>.{app,dom,diff}.txt`. Exit code 1 when anything differs beyond `-Tol`.

   **Static vs after-bind:** default `zui_dump` lays out static markup only. For dynamic pages:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .github/skills/ui-visual-diff/scripts/box-compare.ps1 -Page studio -AfterBind -WaitMs 2000 -AppDumpPath build/box-compare/studio.app.bind.txt
   ```

   `-AppDumpPath` / `-AppDumpCommand` supply a brief dump taken after bind. `--wait-ms N` on the
   probe/dump is the extension point (sleep before layout). Pass `-ScrollId` / `-ScrollTop`
   (optional `-ScrollFraction`) through to `zui_ui_probe` for scrolled geometry compare.


   Read the columns as: **dx/dy** = where the box starts, so a box whose *parent* is wrong shows
   the parent's error; **dw/dh** = that box's own size. A constant `+15` on every box in a column
   means a scrollbar gutter, a constant `-4` on heights means the line-box metric, and three boxes
   that share `y` in the browser but stack in ours mean the container's layout algorithm (e.g. grid)
   is missing. `-Tol 1` is the default; raise it only for pages with sub-pixel content.

3. Read the picture pass: `page / app / web / diff=NN.NN% [hot cells]`.
   Artifacts land in `build/visual-diff/`: `<page>.app.png`, `<page>.web.png`, `<page>.diff.png`
   (magenta = differing pixels, dimmed = our own pixels where the two agree).

4. Look at `<page>.diff.png` to attribute the hot cells before touching code. A *band* across the
   full width means content below it shifted; a *cluster around a control* means that control's box
   or text differs; *scattered* differences inside glyphs are antialiasing only.

5. Compare against the baseline when there is one:

   ```powershell
   ... -All                       # prints baseline -> current and exits 1 on a regression
   ... -All -UpdateBaseline       # after an intended change, adopt the new numbers
   ```

6. Classify every real deviation before fixing it — *missing feature* (algorithm or field not
   implemented, e.g. grid tracks, `Overflow` fields, form-control internals) vs *implementation
   defect* (a quantity computed wrongly, e.g. line-box metrics, scrollbar gutter, `translateY`).
   See [interpretation.md](./references/interpretation.md) for the known noise sources, the
   per-page table, the box delta patterns, and this session's measured baselines.

## Limits

- The picture diff is a *picture* comparison; the box diff is a *geometry* comparison. Neither
  compares glyph runs or paint order. Text runs, baselines, and stacking are only visible in the
  human-readable `zui_ui_probe <page>` / `zui_dump <page>` output.
- The box pass defaults to static markup (`zui_ui_probe`, fallback `zui_dump`). For script/model-bound
  pages use `-AfterBind -WaitMs ...` on the browser side and supply `-AppDumpPath` /
  `-AppDumpCommand` for the app side; without that, `studio` still reports thousands of
  `only-browser` boxes. Prefer the picture pass for those pages when no after-bind dump is available.
- Multi-scroll (`-ScrollId` / `-ScrollTops`) and `-FullPage`: browser PNGs via
  `assets/scroll-capture.html`; app **geometry** via `zui_ui_probe` (`<page>.app.sN.boxes.txt`).
  App **PNG** still has no public scrollTop API (`app-png=s0-only` / PageDown for `-FullPage`).
  Scrollbar width still differs across engines.
- Our engine emits `anon` / `#text` container boxes that the browser has no element for; the box
  pass skips them instead of reporting them as missing. Text geometry therefore only ever shows up
  indirectly, as line-box height differences on the element that holds the text.
- Glyph rasterization differs by definition (FreeType vs DirectWrite); keep the pixel tolerance at 36
  and never chase single pixels.
- Antialiasing, focus/caret state, and animation phase are not layout signals. The `motion` page is
  captured mid-transition in both engines, so its numbers are the least stable.
- Scrollbar width is not comparable across the two sides: headless Edge pages that scroll lose the
  browser's own width while our app reserves 11px, and the VS Code browser reserves 15px. Expect a
  constant offset on that page's content width until our scrollbar width matches the host.
