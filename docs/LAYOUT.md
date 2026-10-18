# Layout

The layout model. Changing the cache, adding a formatting context, or changing
invalidation follows this page. Layout types stay out of `include/`.

Authors write constraints. Layout produces geometry: how large each box is, where it
sits in its containing block, and which lines the text broke into.

Style is a finished input. Layout reads it and does not write computed style. Percentage
and flex pixels are used values and live on geometry; the computed map still holds
`"50%"`.

Radius, `clip-path`, and `mask` are paint shapes. Layout produces rectangular boxes and
line boxes.

Canvas pixels are computed from the root: subtract scroll along the path, multiply
transforms, add containing-block offset and sticky. Scrolling one pixel updates the
scrollport and sticky and does not relayout.

Geometry belongs to boxes. `display: none` has no box. `display: contents` has no box of
its own. Anonymous boxes have no element. Text nodes become `BoxRole::Text`.

## 1. Kernel

```text
Layout(box, key) → local geometry
```

The key is every local input of this question. A child's text change does not change the
parent's key; the parent result is dropped by dirty flags.

This function does not rewrite parent pointers. Containing blocks are geometry computed
while building the key. Absolutely positioned boxes stay under their tree parent.
Formatting context is a kind on the box. Stacking is a later walk.

```cpp
struct LayoutKey {
    std::shared_ptr<const LayoutStyle> style;  // pointer equality
    ContentVersion content;
    ConstraintSpace space;
};

class LayoutContext {
    const LayoutResult* layout(Box& box, const ConstraintSpace& space);
    const LayoutResult* ask(Box& child, const ConstraintSpace& space);
};
```

`layout` is the entry for the root and for container-query re-entry. `ask` is the only
question an algorithm asks about a child: the same `run`, then the key and contribution
are appended to the current stack frame.

The key does not contain a font version. The algorithm that calls `shape` sets
`depends_on_font` on the box. Font load walks the tree for that flag and marks Layout.

Cascade produces computed style. After the box exists, outer/inner display derive an
immutable interned `LayoutStyle`. Boxes, keys, and live `used_requests` hold a reference.
Key comparison is pointer equality. The intern table has no lock: style and layout run
on the frame thread.

`LayoutStyle` interns every computed value that moves geometry, line breaking, or
containing-block membership. Color, background, radius, shadow, `z-index`,
`pointer-events`, and `opacity` stay out: they are paint or stacking. The transform
*matrix* stays out; crossing `none` ↔ non-`none` still changes the interned pointer,
which is how containing-block bits move.

Two derived booleans participate in intern:

- `cb_for_absolute` — non-static `position`, or `transform` / `filter` /
  `backdrop-filter` / `perspective`, or `contain` with `layout` or `paint`, or
  `will-change` listing one of those
- `cb_for_fixed` — the same set without `position: relative`

A box marked Layout must not return an old cache slot. Slots are dropped when the box is
dirtied. During one `compute`, the cache only grows: `ask` pointers stay valid until that
`compute` returns. Eviction happens before or after the whole-frame layout.

Each box pins the last `Final` result used for paint, so a frame that skips layout still
has line boxes. `LayoutResult` is reference-counted.

Layout is single-threaded on the frame thread. `LayoutContext` is not handed to other
threads.

## 2. Constraints

The parent solves its own unknowns first and writes them into `ConstraintSpace`. A
child's key does not hold the parent's track list, flex lines, or column widths.

`available` is the space left for this child. `percentage` is the containing-block size
used to resolve percentages. For in-flow children the containing block is the parent's
**content box** (CSS 2.1 §10.1), so padding and a reserved scrollbar gutter shrink both.
For absolutely positioned children the containing block is the padding box, so the two
can differ.

`stretch_block` is a parent intent: the parent has a definite block size and this box
must fill it (grid `align-self: stretch`, flex cross-axis stretch). A block with
`height: auto` does not grow just because `available` is definite.

The element's own min/max live on `LayoutStyle`. `FitContent` is a `Request::Kind`, not
another min/max pair.

`writing_mode` and `direction` on the space are the containing block's axes. The child's
own writing mode is on `LayoutStyle`. `available` is interpreted on the space's axes.

Incoming margin strut does not enter the key. Collapse offsets are a parent placement
decision. `static_position` participates in equality only for out-of-flow shrink-to-fit.
Relative offsets and scroll offsets stay out of the key.

`icb` (initial containing block) enters every key only when some interned style on the
document actually reads it (`calc()` / `min()` / `max()` / `clamp()` on width or height).
That switch is document-wide, set-only until `LayoutEngine::reset()`. Scroll is not
resize: scroll offset stays out of the key.

`kMeasureSlotLimit = 8` (`source/layout/cache/box_cache.h`). One layout of a box may
store min-content, max-content, fit-content, flex-base, hypothetical, and final. Extra
constraints evict the oldest slot, never the pinned `Final`.

Structs live in `source/layout/`. This page is the model; the headers are the fields.

## 3. Result and placement

A child returns local geometry and contributions. Its coordinate in the parent is held
by the parent and does not enter the child's cache.

`LayoutResult` holds border-box size, margin strut, overflow ink / scrollable, floats,
line boxes (`InlineContent`), a break token, and `used_requests` (the questions this
compute asked). `converged` is false when a float or ask-fuse still moved on the last
round; the frame still paints that geometry, and tests fail the frame.

`Placement` holds `cb_offset` (from the containing-block padding-box origin) and
`sticky_offset`. Fragments, if any, each have their own offset and block size. Hit
testing, paint, and comparison tests walk that list; the reported rect is the union.

A replaced box whose decode has not finished measures as 300×150. Decode increments
`ContentVersion`; the next `measure_replaced` sees the intrinsic size.

`contain: size` sizes the contained axis as if empty, then lays children into that size.
`content-visibility: hidden` skips children and uses `contain-intrinsic-size`.
`visibility: hidden` does not change geometry.

`content-visibility: auto` skips using a band 1.5× the viewport. The frame driver
classifies from last frame's world rects before layout; block flow classifies a never-
placed child from this round's block offset. Both write `Box::contents_skipped`, which
enters the key.

Parents doing flex, grid, inline-block, or cell alignment read `BaselineSet` only.
`vertical-align` stays inside the inline algorithm that produces those line boxes.

## 4. Algorithms

Flex, grid, block, inline, and replaced elements share the question protocol. They do
not share formulas.

```text
find(box, key) → hit
else
  measure → solve → place     // at most two rounds per child per segment
  layout_out_of_flow
  store(box, key, result)
```

`run` is a private template on `LayoutContext`. Algorithms cannot call `find` / `store`.
`place` does not call `ask`. Out-of-flow descendants are collected after `compute` and
asked while this layer's used-request stack is still current; collection stops at a
descendant whose `cb_for_absolute` / `cb_for_fixed` is true.

The same child, same segment, asked a third time with a key that still moved, sets
`converged` false and keeps the second round. A designed new segment (`reset_ask_fuse`)
resets that count. Different `Request::Kind` values in one segment (min-content /
max-content / final) are not repeats.

Parents in block layout hold a mutable float accumulation. Each `ask` receives an
immutable snapshot. Appending a float allocates a new snapshot.

`LayoutContext` also exposes `shape` (shape a script run; no line breaking, no available
width) and `measure_replaced`.

Directories:

```
source/layout/
  engine.cpp            one frame
  box_tree/             build, sync_children, relower
  cache/                LayoutKey, ask / run, BoxCache
  flow/  flex/  grid/  table/  multicol/
  world_placement.cpp   world coords
  hit_tester.cpp        hit test
```

## 5. Invalidation

`mark_needs_layout` walks `parent` and `cache.clear()`. The next frame starts from the
root with the ICB. Uncleared subtrees hit on their own `ConstraintSpace`.

`Box.style_stamp` is `Node.computed.get()`. `refresh_style` is pushed by the cascade
layout list, `sync_children`, rebuild, and box creation. The compute path does not pull
style. Paint-only cascade may change the interned pointer; layout follows `StyleDiff`
and does not treat a stamp change as a layout miss.

`css::Engine::cascade` returns `CascadeResult`: `reattach`, `layout`, `paint`.

| Diff | Typical properties | Box tree |
| --- | --- | --- |
| Paint | `color`, `background-color`, `border-color` | unchanged |
| Layout | size, margin, flex/grid, font-size, overflow | `mark_needs_layout` |
| Reattach | `display`, `position` | `sync_children` on the parent |

`set_model` queues the host only (`pending_attach`). Equal rows keep their nodes. Kept
rows refill `geo.runs` after placement when the box still has size, empty runs, and the
text lives on `Node::text`. See [EVENT.md](./EVENT.md).

## 6. Text quantities

On an embedded face, these layout quantities are the ones later stages consume. FreeType
and DirectWrite coverage bitmaps are paint.

| Quantity | Produced by | Consumed by |
| --- | --- | --- |
| Face ascent, descent, x-height, average width | `FontChain` (OS/2 win metrics, else hhea) | line-box strut, `vertical-align: middle`, `ch` / `ex` |
| Per-cluster advance + kerning | `layout::inline_text::shape` | line breaking, min/max-content, flex base, nowrap scroll width |
| Whole-run advance, ceiled to 1/64 px | `text::ceil_advance` | line width, inline-block width, selection rect |
| Line-box count, break offsets | `source/layout/flow/lines.cpp` | block height, later siblings |
| Line height, half-leading, first-line top | same; strut is `used_line_height` | flow after the paragraph, `BaselineSet` |
| Pen | `paint/text_runs.cpp`: line origin + ascent | glyphs, underline, ellipsis, `::selection` |
| Caret / hit offset | `layout/caret`: `EditMapping` of `range_edit_text` onto LineBox runs | caret bar, `text_offset_at`, pointer Range, `::selection` slice |

That mapping is the only caret and hit geometry. Glyphs that still live on a nested
inline's LineBoxes map through those lines, rebased into the owner's border box.

`letter-spacing` is applied in inline measurement, together with kerning and
`text-align: justify` stretch. Paint prefers those advances.

Synthetic bold (`font-weight: 700` with no bold face) adds `5/256 em` to each glyph
advance and emboldens the outline before upload.

Ink does not reflow. While line boxes are still wrong, pixel diffs count the same error
again.

## 7. Scrollbar thickness

Thickness is a platform metric injected into `LayoutContext` / `LayoutEngine`
(`PlatformMetrics`). Comparison tests pin **15px** (classic Chrome scrollbar at device
scale 1 on Windows). Win32 `SM_CXVSCROLL` on the same machine reports 17; the engine
targets Chrome pixels. Skin of the bar is paint, outside this model.

## 8. Observation

After layout, the engine can export JSON: border boxes, overflow rects, `BaselineSet`,
line-box summaries, `Placement.cb_offset` and `sticky_offset`, lengths quantized to
1/64 px. That is the engine side of comparison-test layer L2. See [TESTING.md](./TESTING.md).
