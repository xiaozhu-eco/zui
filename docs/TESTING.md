# Testing

How zui proves a page matches Chrome, and what test code may call.

Align the **transfer function**, not the time trajectory. Scroll and animation, as
pixels, are a pure function of (scroll offset, animation time, input). Sample those
parameters with the same pipeline used for a static page. Frame scheduling, smooth-scroll
physics, and vsync are behavior assertions.

A mismatch against Chrome is a geometry bug unless this page already classified it as a
strategy difference with evidence. Strategy differences are written into [LAYOUT.md](./LAYOUT.md)
and a `DIVERGENCE.md` next to the case. An unevidenced "we meant to differ" is a fail.

Numeric thresholds and the case matrix live in `tests/difftest/` (`passline.json`,
`matrix.json`, `env.json`). This page is the mechanism.

## 1. Layers

Fail upward: an upper layer that already matches is not blamed for a lower-layer miss.

| Layer | What | Primitive | Tolerance |
| --- | --- | --- | --- |
| L0 | Environment | flag set, probe strings, embedded fonts, browser version | — |
| L1 | Style | computed-style dump | exact |
| L2 | Geometry | position relative to containing block, line boxes, structure fields | length 1/64 px; structure exact |
| L3 | State sequence | clamp, hit set, order. Geometry still counted once at L2 | — |
| L4 | Pixels | partitioned fallback: large paint errors, content drawn into the scrollbar gutter. Not scrollbar skin, not glyph AA noise, not corner coverage | per zone |
| L5 | Behavior | readback after the same raw input | offset 1/64 px; else exact |
| L6 | Architecture self-check | engine asserts, no Chrome | exact |
| L7 | External yardstick | WPT reftest subset, fuzz | per case |

L1 before L2: bad specified values poison every later layer. L2 before L4: a wrong
relative position is layout; pixels catch paint the geometry cannot see. Line-box count,
break count, and scroll height are structure fields: a difference of one is a geometry
fail. Length tolerance stays 1/64 px.

The kernel comparison binary is `zui_difftest`. It does not compile `source/app/app.cpp`,
does not `#include <zui/app.h>`, and does not construct `App` / `IWindow` / `IPlatform`.

## 2. Support

"Matches the browser" is a claim about this matrix. Cases outside it are not built into
the required set.

| Scope | Level |
| --- | --- |
| Normal flow, margin collapse, floats, BFC, clearance | match |
| Inline: break, baseline, `vertical-align`, line boxes (Latin / CJK) | match |
| Flex, grid | match |
| Replaced sizing, `object-fit` | match |
| Absolute / fixed / sticky | match |
| Scroll: `overflow`, `scrollbar-gutter`, anchoring, two-pass bars | match (event count and timing are behavior) |
| Animation interpolation of transform / opacity / length / color, frozen at t | match |
| Container queries | match (two-round fuse) |
| `font-display` under a virtual clock; fallback only along a declared chain | match |
| Ligatures, kerning, OpenType features on an embedded face | match |
| `::marker` / `list-item`; `::selection` (self-check) | match |
| Complex script, bidi, vertical writing, synthetic bold as a product claim | behavior assertion |
| `scroll-behavior: smooth` / snap *during* the animation | behavior (start, end, snap point) |
| `scroll` / `scrollend` counts and timing | behavior |
| Tables, multicol, fragmentation, form internals as a product claim | unsupported until a case is admitted |

## 3. Driver

One JSON case, two ends:

```json
{
  "steps": [
    { "op": "resize", "w": 800, "h": 600 },
    { "op": "scroll", "target": "window", "x": 0, "y": 320 },
    { "op": "freeze", "t": 150 },
    { "op": "trace" },
    { "op": "capture" }
  ],
  "expect": "golden/"
}
```

| Op | Chrome | Engine |
| --- | --- | --- |
| `load` | CDP navigate; wait `document.fonts.ready` | load; drain resources |
| `resize` | `Emulation.setDeviceMetricsOverride` | test viewport |
| `eval` | `Runtime.evaluate` | shared subset only |
| `scroll` | `scrollTo({behavior:'instant'})` | same |
| `freeze` | `getAnimations()` `currentTime` | same |
| `advance` | virtual time | virtual frame clock |
| `dispatch` | CDP input | `source/input/Interaction` (no OS poll) |
| `trace` | `DOMSnapshot.captureSnapshot` | layout export ([LAYOUT.md](./LAYOUT.md) §8) |
| `capture` | screenshot | `IRenderer::read_frame`. Default OpenGL; `ZUI_PROBE_RENDERER=vk` for Vulkan |

`dispatch` uses one event shape on both ends (`mousePressed`, `mouseReleased`,
`mouseMoved`, `mouseWheel`, keys). Chrome comparison does not run handlers: it advances
DOM to the target state, then compares geometry. Behavior is L5 / L6.

Scrollbar thickness for comparison is 15px, injected from `env.json`
([LAYOUT.md](./LAYOUT.md) §7).

## 4. What test code may call

Tests may:

- Start a program that already paints (`zui_hello`, `zui_checks`, a session that
  `pump_frame`s) and read the frame it drew.
- Compare that frame and a style dump to a browser-recorded golden.
- Call interaction that already exists in `source/input` and `frame::Access`.
- Write probe output as JSON or PNG. Comparison rules live in `tests/difftest/report/`.

Tests may not reimplement cascade, used-style lookup, line boxes, coverage, blending, or
a second paint. When a value is needed, add a probe in `source/` and compile it out of
Release with `ZUI_PROBE`.

`capture` writes this frame's color as PNG from the real renderer after `present`'s
target was drawn. Tests do not rasterize a `DisplayList` onto a second bitmap and treat
that as the accepted picture.

`frame::Access` is the C-tier seam ([ARCHITECTURE.md](./ARCHITECTURE.md) §7). Application
headers do not grow members for it. `Document::measure` flushes; `Access::measure` does
not.

`EngineRunner::frame` in `tests/difftest/engine/` is a driver. New used-value rules stop
being added in `trace_export.cpp`; they move into `source/`.

## 5. Probe macros

Declared in `source/probe/probe.h`. Application headers do not include it.

| Macro | Default | When on |
| --- | --- | --- |
| `ZUI_PROBE` | on in Debug and any non-Release; off in Release. CMake option `ZUI_PROBE` overrides | observation and automation compile into the library |
| `ZUI_PROBE_FRAME` | same | offscreen framebuffer readback on GL and Vulkan |

`ZUI_TRACE_PERFETTO` wraps `source/trace`. `NDEBUG` wraps `source/perf/perf_budget.cpp`.

## 6. How to run

```bash
cmake --preset windows-x64
cmake --build --preset windows-x64
ctest --preset windows-x64 --output-on-failure
```

Kernel GoogleTest binaries cover layout, paint, cascade, host, and interaction.
`zui_difftest` plus `tests/difftest/run.py` is the Chrome comparison. Concern tags and
the five-verdict rollup (`correctness`, `render`, `interaction`, `performance`, `app`)
are produced by `tests/concern_report.py` from `tests/concern_manifest.json`. They do not
replace L0–L7 or `passline.json`.

### Caret / grapheme service targets

Layout-owned caret work (see `docs/superpowers/plans/2026-10-15-caret-layout-service.md`)
adds kernel binaries `zui_grapheme_break` and `zui_caret_service`. PowerShell from the repo root:

```powershell
cmake --build --preset windows-x64 --target zui_grapheme_break --target zui_caret_service --target zui_layout_text --target zui_app_interact

# CTest registers GoogleTest cases by suite name (not the binary name):
ctest --preset windows-x64 -R "GraphemeBreak|CaretService|LayoutText" --output-on-failure

.\build\windows-x64\zui_grapheme_break.exe --gtest_filter=GraphemeBreak.*
.\build\windows-x64\zui_caret_service.exe --gtest_filter=CaretService.*
.\build\windows-x64\zui_app_interact.exe --gtest_filter=CaretService.*:PerfStage3.*:AppInteract.ChatComposerCaretWalksTheWrappedLines
```

`HB_HAVE_ICU` stays `OFF` in `cmake/deps.cmake` (minimal UAX#29 grapheme tables, no ICU).

Manual example checklist (IME candidacy, CE bold/italic inherit/merge, password fail-closed)
lives in that plan under Task 9; record pass/fail in PR notes, do not commit from the checklist alone.

