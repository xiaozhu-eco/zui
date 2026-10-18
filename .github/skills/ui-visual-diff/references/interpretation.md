# Interpreting the numbers

## Pages

| page   | executable   | html                                       | app first frame                                        |
| ------ | ------------ | ------------------------------------------ | ------------------------------------------------------ |
| hello  | `zui_hello`  | `example/tests/hello/hello.html`           | instant                                                |
| studio | `zui_studio` | `example/tests/studio/studio.html`         | **~30 s** (random 50–100 × 50–100 table + 16 log rows) |
| flow   | `zui_flow`   | `example/tests/flow/flow.html`             | instant                                                |
| style  | `zui_style`  | `example/tests/style/style.html`           | instant                                                |
| text   | `zui_text`   | `example/tests/text/text.html`             | instant                                                |
| motion | `zui_motion` | `example/tests/transition/transition.html` | instant (captured mid-transition)                      |
| checks | `zui_checks` | `example/tests/checks/checks.html`         | instant                                                |

`studio` is also the slowest to *diagnose*: the page itself is fine — rendered through
`zui_checks studio.html` it paints immediately. The cost is its own `main.cpp` binding a huge model
into the template, which currently costs ~30 s of CPU in the first frame.

## Noise that is not a defect

| Observation                                                      | Cause                                                                                                  | Action                              |
| ---------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------ | ----------------------------------- |
| Single-pixel halos around every glyph                            | FreeType vs DirectWrite rasterization                                                                  | ignore; keep `-Tol 36`              |
| Text of one line appearing twice in the diff, offset by a few px | our line box is ~22 % shorter than Chrome's `line-height: normal` (1.14em vs 1.46em) — real, see below | treat as a metric defect, not noise |
| Whole content column shifted/wider by ~4 px                      | scrollbar width: zui reserves 11 px, Chrome 15 px                                                      | not a bug per se; account for it    |
| `motion` tiles differ                                            | `transform: translateY()` is parsed as X, plus `opacity: .2` start state                               | see below                           |
| `checks` / `text` right-hand strip                               | element scrollbar: Chrome shows one, we do not (`Overflow` never filled)                               | see below                           |

## Measured baseline

Two seeds, before and after the 2026-09-23 render fixes (app client area 1100x720 vs headless Edge
1100x720).

| page   | before the fixes | after the fixes | dominant hot cells                               |
| ------ | ---------------- | --------------- | ------------------------------------------------ |
| motion | 2.16 %           | **1.63 %**      | label text; tiles sit at `opacity: .2`           |
| text   | 5.16 %           | **4.91 %**      | per-line text offsets; y580-599 band             |
| studio | 2.16 %           | 5.30 %          | table bands (studio's own model, see below)      |
| style  | 17.84 %          | **6.81 %**      | bands y700-719 (sections pushed down)            |
| flow   | 21.28 %          | **9.74 %**      | band y620-639 (block heights accumulate)         |
| hello  | 19.87 %          | 21.12 %         | y300-319 band (`.cards` stacked instead of grid) |
| checks | 14.89 %          | 26.07 %         | the `.mask` panel: see the note under the table  |

The four pages that improved did so because line boxes, glyph advances and form controls now match
the browser. The two that got *worse* are both honest: with the geometry fixed, a remaining missing
feature shows through as a solid block of differing pixels.

- `hello`: every text line is now the right height, so the only shift left is the stacked `.cards`
  grid. The page is taller than the browser's, which moves more rows past each other below it.
- `checks`: `.mask` is `position: fixed; inset: 0; transform: translateY(110%)`. Its translation is
  now read correctly as vertical, so the panel moves 110 % of its *own* height — and our fixed box
  is only as tall as its content, not the viewport, so it lands inside the window instead of below
  it. Before the fix the panel was pushed off to the right by mistake, which happened to hide both
  this and the missing element clipping. Fix the fixed-position size (or the clipping) and this page
  drops below its old number.

Numbers are only comparable between runs of *this* script: a different browser (the VS Code
integrated browser instead of headless Edge) or a different scrollbar width moves every page by
several points. Re-seed after an intended change; never mix two methods in one baseline.

`hello` box-by-box at 1100x720 (browser DOM vs our boxes), re-measured after the fixes:

| element                | Chrome                     | zui                     | Why                                                               |
| ---------------------- | -------------------------- | ----------------------- | ----------------------------------------------------------------- |
| `.topbar` / `.sidebar` | 0,0,1100,52 / 0,52,232,668 | identical               | ok                                                                |
| `#nav_home`            | 16,68,200,39               | 16,68,200,39            | ok                                                                |
| `#heading`             | 256,76,**805,40**          | 256,76,**809,40**       | height fixed; +4 px width = our 11 px scrollbar vs Chrome's 15 px |
| `.card`                | 256,185,**260.3,72**       | 256,**159**,**809,72**  | height fixed; width/position = grid tracks missing (stacked)      |
| `#name`                | 256,285,**212,39**         | 256,403,**212,39**      | size now exact; y follows the exaggerated `.cards` above it       |
| `#save`                | 478,285,**70.4,39**        | 478,403,**70.38,39**    | ok (text 30.38 vs 30.383)                                         |
| `#inc`                 | 556.4,285,**93,39**        | 556.38,403,**92.97,39** | ok                                                                |
| `.feed-item`           | 805,**39** tall            | 809,**39** tall         | ok                                                                |
| `.feed`                | 805,**390** tall           | 809,**390** tall        | ok                                                                |

## Box comparison (the second command)

`box-compare.ps1` builds the table above mechanically:

- `zui_dump <page> --brief --size 1100x720` prints `box|tag|id|class|x|y|w|h|content_w|content_h`,
  one line per box in document order.
- `assets/box-probe.html` loads the page in a 1100x720 iframe in headless Edge (needs
  `--allow-file-access-from-files`) and prints `dom|tag|id|class|x|y|w|h` for every element whose
  computed `display`/`visibility` is laid out.
- Join key: `#id` when the element has an id, else `tag.class`, else `tag`; duplicates get `~1`,
  `~2`, … in document order, so the Nth `.card` meets the Nth `.card`.
- `dx`/`dy` are top-left differences, `dw`/`dh` size differences, all border-box. Our
  `content_w`/`content_h` are written to `<page>.app.txt` but not compared: the browser's nearest
  equivalent (`clientWidth`) includes padding, so the two are not the same quantity.
- Our `anon` and `#text` containers are skipped, because the browser has no element for them. Text
  geometry therefore only shows up as a line-box height difference on the element that holds it.

Full lists: `build/box-compare/<page>.{app,dom,diff}.txt`. Measured with `-Tol 1` on the same day as
the pixel baseline, app client 1100x720 vs headless Edge 1100x720:

| page   | matched | differing | only-browser | only-zui |
| ------ | ------- | --------- | ------------ | -------- |
| motion | 34      | 14        | 0            | 0        |
| hello  | 12      | 28        | 0            | 0        |
| text   | 8       | 18        | 0            | 0        |
| checks | 11      | 35        | 0            | 0        |
| style  | 54      | 142       | 4            | 0        |
| flow   | 27      | 199       | 0            | 0        |
| studio | 11      | 23        | **6615**     | 0        |

Measured before the fixes, for comparison: motion 26, hello 9, text 5, checks 5, style 4, flow 4.

`studio` is the exception: its html uses `<template>` blocks that the page's own script expands into
~3900 generated `div`s, so the browser lays out a table while `zui_ui_probe` / `zui_dump` see the static markup.
Every generated box lands in `only-browser`. Compare that page with the picture pass, not this one.

How to read a delta, in the order that pays off fastest:

| Pattern                                                        | Cause                                                                         |
| -------------------------------------------------------------- | ----------------------------------------------------------------------------- |
| `dw = +4` on every box in one column (was `+15`)               | scrollbar gutter: we reserve 11 px, Chrome 15 px (constant, not a layout bug) |
| `dh = -4` (32 vs 36, 35 vs 39, 15 vs 19) on text-bearing boxes | line-box metric — **fixed**, expect `dh = 0` now                              |
| siblings share one `y` in the browser but stack in ours        | container algorithm missing — grid tracks (open)                              |
| `dy` growing down a page from one box onward                   | an ancestor is too tall or too short; the rest of the page is chain reaction  |
| a run of boxes shifted by a constant                           | a missing margin/unit above them (UA block margins need `em`, see below)      |
| boxes present in `only-browser`                                | we never created them: `display` handling, grid, generated content            |
| boxes present in `only-zui` besides `anon`/`#text`             | we created a box for something the browser does not lay out (hidden node)     |

## Known causes, by classification

**Implementation defects — fixed on 2026-09-23 (verified against the browser)**

1. Line-box metric: `LayoutContext::font_em` used hhea ascent+descent and rounded nothing, while
   the Windows text stack sizes a font box from `OS/2.usWinAscent/usWinDescent`, rounded to whole
   pixels. For Noto Sans SC that is 1.16/0.29 em instead of 1.00/0.20 em — every line was 4-9 px
   short. Fixed in `text/font_chain.cpp::face_box` + `layout_context.cpp`.
2. `sans-serif` resolved to Arial, but Chromium on this machine resolves it to Noto Sans SC; font
   families were also resolved against absolute paths and the *layout* engine ignored
   `font-family` entirely (one hardcoded `sans-serif` chain for the whole tree, while paint used
   per-family chains). Fixed: per-directory file resolution, a priority list per generic family,
   and `FreeTypeTextMetrics::chain_for`.
3. Variable fonts opened at their own default instance: Noto Sans SC declares `Weight` with
   default **100 (Thin)**, so unstyled text measured ~4 % narrow. Fixed: instance at 400
   (`font_chain.cpp::apply_weight`). A consequence worth knowing: we have no bold face yet, so
   `font-weight: 700` still measures like 400.
4. Glyph advances were hinted and rounded to whole pixels; Chromium positions glyphs with subpixel
   advances (`linearHoriAdvance`). Font sizes were also rounded to whole pixels, which breaks the
   UA's 13.3333 px form-control size. Both fixed in `font_chain.cpp` (size stored in 1/64 px, and
   the atlas keys on the same 1/64 px value).
5. UA form-control defaults were wrong: Chrome gives `button, input, select, textarea`
   `font: 400 13.3333px Arial`, a 2 px border, `content-box` on inputs but `border-box` on buttons,
   and no inheritance from the page font. Fixed in `css/engine.cpp` (kUaCss).
6. Scrollbar gutter: only the normal-flow path reserved it, and only for a definite `height`, so a
   flex-stretched `overflow: auto` pane (`.content`) kept a 15 px-too-wide content box. Fixed in
   `flex/flex.cpp` by running the item passes twice when the content overflows a scroll container
   with a definite block size (second pass gets `content_inline - kScrollbar`). This needed
   `AskFuse::reset` / `LayoutContext::reset_ask_fuse`, because the fuse refuses the third ask for one
   child even when the size differs.
7. `paint_map.cpp::xform_of`: the string fallback read `translateY(32px)` as a horizontal shift and
   stopped at the first function. Fixed with a used-value parser that keeps the axis per function;
   `geo.x/geo.y` no longer add `tx/ty` a second time on top of `to_world`.

The old forms of 1, 3, 4, 5 and 7 all showed up as `dh`/`dw` in the box table; after the fixes the
remaining `dw` is the 4 px scrollbar difference and the remaining `dy` is the grid.

**Implementation defects — still open**

8. `studio` first frame: `LayoutContext::find()` recurses `result_stale()` on every cache hit →
   O(n²) in the box count (≈10 000 cells), ~30 s of CPU before the first paint.
9. `position: fixed` with `inset: 0` is not sized to the viewport: `checks`' `.mask` comes out as
   tall as its content, so its correct `translateY(110%)` lands inside the window instead of below
   it (see the `checks` note above).
10. No GPOS kerning: our chain only reads the legacy `kern` table, so a string measures ~0.1 px per
   glyph wider than the browser (`ZUI Studio` 88.60 vs 87.82 at 18 px; the browser's kerning is
   -0.78 px there). Visible in the `dw` column as small positive values on text boxes.
11. Text boxes report `runs` inconsistently (`LayoutText.FallbackAndWrap` sees `wrap runs=0`), and
   the `text_wrap` fixture's 80 px box never wraps although a browser breaks it into four lines.
    Pre-existing; the same fixture fails the same way before and after the render fixes.

**Missing features**

12. Grid tracks (`GridAlgo` is still the `InFlowPass` placeholder) → `hello .cards`, `style` card
    groups, `studio` table columns. This is now the single largest source of `dy` drift.
13. Relative and viewport units: declaration values accept only `px` and `%`
    (`intern_style.cpp::parse_length`), so `em`, `rem`, `vh`, `calc()` and friends are dropped. That
    also blocks the UA block margins (`p { margin: 1em }`, `h1..h6 { margin: 0.67em }`), which is
    why text blocks sit ~13 px closer together than in a browser.
14. `Overflow.ink` / `scrollable` / `clipped_by_paint_contain` are declared but never written, so
    `scrollbar_h.cpp` can only compare `geo.content_width` against itself and never shows element
    scrollbars; element clipping follows from the same missing field.
15. Bold: `font-weight` is not plumbed to the text stack at all (`LayoutStyle` has no field), so
    headings and bold labels measure and paint with the regular instance.
16. Form-control internals (`LAYOUT.md` §14): no UA intrinsic width for `<input>`.
17. Rounded-corner and `clip-path` hit testing; stacking order (`stacking.cpp` is an empty stub).

## Scroll / full-page / after-bind (skill extensions)

- Preferred harness: **`zui_ui_probe`** (`cmake --build build/windows-x64 --target zui_ui_probe`).
  It links the same internal `source/` as `zui_dump`, sets `html::Node::scroll_x` / `scroll_y`, then
  calls `LayoutEngine::sync_world()` so world boxes move. No public App / `include/` scroll API.
- `visual-diff.ps1 -ScrollId <id> -ScrollTops 0,0.5,1` captures multiple browser viewports via
  `assets/scroll-capture.html` (`<page>.web.sN.png`) and writes app geometry frames via the probe
  (`<page>.app.sN.boxes.txt`, plus `<page>.app.scroll-tops.boxes.txt` with `scroll|id|y|max|index`
  headers). Absolute tops: `-ScrollAbsolute`.
- `-FullPage` steps by viewport height, stitches with `StitchVertical` to `<page>.web.full.png`.
- **App PNG gap (still):** example windows have no scripted `scrollTop`. Multi-scroll PNG may stay
  at s0 (`app-png=s0-only`); `-FullPage` PNG uses Win32 `PageDown` strips. Prefer probe geometry
  for scroll alignment; do not require a public App scroll API.
- `box-compare.ps1` prefers **`zui_ui_probe`** (fallback `zui_dump`). Pass `-ScrollId` / `-ScrollTop`
  / `-ScrollFraction` for scrolled geometry. `-AfterBind -WaitMs N` lengthens the browser
  virtual-time wait; app geometry after bind still needs `-AppDumpPath` / `-AppDumpCommand`.
  `--wait-ms` on the probe is a sleep-before-layout hook only.

## Adding a page

Append to `$pages` in `scripts/visual-diff.ps1` with the exe suffix, the html path relative to the
repo root, and `WaitMs` (raise it for anything that animates or binds a large model). Then run
`-Page <name> -UpdateBaseline`.

For the box pass, only the html path matters: `box-compare.ps1` has its own `$pages` table
(html path per name, no executable) because it probes/dumps the page instead of launching the app. Add
the page there too, and skip it if its content is built by the page's own script (that is why
`studio` only exists in the picture comparison).
