# Performance

Implementation-side contract, next to [CONTRACT.md](./CONTRACT.md). That page says what
an application may write. This page says how much that write may cost.

Until a change rewrites this header, [AGENTS.md](../AGENTS.md) uses §3 as the hot-path
review bar. A change to `source/{css,layout,paint,app}/` that violates an invariant
records, in the same commit, which invariant, why it cannot hold, and when it returns —
and a gate case pins the current behavior.

**Work is proportional to the dirty set plus the closure CSS requires.** The closure may
be the whole document (`:root:has(...)`, inherited font-size, counters). Whole-document
recompute is then correct. The invariant is: do not walk past the closure, and keep the
closure as small as the selectors allow.

## 1. Work units, not milliseconds

Debug and Release differ by more than 10× on the same click. `present` is vsync-blocked
in Release and is not in Debug. Budgets are work units that a Debug build can count with
tracing off. Milliseconds remain in traces for ranking.

## 2. Cost model

### 2.1 Dirty units

| Unit | Meaning | Typical source |
| --- | --- | --- |
| `Node(n)` | n's computed style and its own box | `set(text/value/checked/disabled/src/href)`; a class that affects only n |
| `Subtree(n)` | n and descendants | a class that inherits or changes `display` |
| `Children(host)` | host's child *set* | `set_model(host)`, `load_fragment`, `unload_fragment` |
| `Box(b)` | one box's geometry | layout |
| `Clip(r)` | a paint region | paint |

`set(id,"visible",v)` is `Node(n)` whose diff lands on `Children(parent)`: it writes
`hidden`, cascade reports `Reattach`, `sync_children` runs on the parent.

### 2.2 Work units

Two groups: structure (the tree changed) and walk (visited, unchanged).

| Unit | Counts | Notes |
| --- | --- | --- |
| `style.intern` | `intern_style` calls (create and `refresh_style`) | Not "how many boxes were born" |
| `box.created` / `box.destroyed` | `BoxTree::make` / `forget_subtree` | |
| `cascade.visited` | elements that participated in matching | Includes share-cache hits |
| `cascade.recomputed` | elements whose style pointer was replaced | Not `cache_misses` (those are distinct ShareKeys in *this* walk) |
| `laid.boxes` | distinct boxes that produced a result this frame | Deduped. This is the layout gate. |
| `laid.stores` | `store` events | Same box, different `ConstraintSpace`, each counts. Not a gate. |
| `relower` / `sync_children` | those calls | |
| `world.walk` / `hit.boxes` / `paint.walk` | boxes visited | |
| `damage.items` | display items in the dirty rects | |
| `image.decodes` / `input.events` / `text.shapes` | cache misses / fired handlers / uncached shapes | |
| `html.nodes` / `css.sheets` | built at `load` | Not a frame |

`paint.items` is retained-listing *stock*. An idle frame that reuses the list still has
`paint.items > 0`. It is not a "this frame painted" counter.

`laid.boxes` is deduped for the whole frame and does not multiply by cascade rounds.
`style.intern` and `box.created` do multiply by rounds. `laid.stores` has no box-count
upper bound: flex and grid ask several `ConstraintSpace`s per item. Slot cap is
`kMeasureSlotLimit = 8`.

### 2.3 Why not milliseconds

See §1.

### 2.4 Closure

`closure(D)` is every node whose match or output can change because of D:

1. **Self** — every node in D.
2. **Down** — descendants, when the change can inherit, restyle via a descendant
   selector, `:has()`, `nth-*`, `display`, custom properties, or `container-type`.
   Inherited restyle of the whole subtree is correct.
3. **Sideways / up** — the other side of a combinator (`+` / `~` following siblings,
   ancestor chain of a descendant selector, `:root:has(...)`) and those nodes' subtrees.
4. **Generated content and counters** — changing one list item's `display` changes later
   `list-item` ordinals and `content: counter(...)` text with no selector change.
5. **Collapsed-border tables** — a change on any box in the table re-interns every box
   in the table, then re-resolves shared borders. Upper bound is the boxes in the table.

Hover dirty set: the old ancestor chain and the new ancestor chain, each node a `Node`.
Selectors 4 §6.2 matches `:hover` on the element *and its ancestors* (so
`.card:hover .title` works). That is not `Subtree(html)`. Descendants join only when
closure says they must.

Focus dirty set: the one focused node. Focus flags do not walk ancestors.

Until a precise closure analysis covers every combinator, author counters, and
`:has()`, the I3 upper bound is the whole document.

### 2.5 DirtySet

One declaration per frame: dirty-unit kind + root + source. Cleared at frame end. Used
for budgets. Box-tree invalidation still walks `mark_needs_layout`; DirtySet does not
replace that.

| Group | Source | Maps to |
| --- | --- | --- |
| A-tier write | `set` / `set_model` / `add_class` / `load_fragment` / `set_theme` | §4 |
| Engine | hover / active / focus | hover: each node on the chain; active / focus: one `Node` |
| Engine | transition sample | `Node`; `Box` when `needs_layout` |
| Engine | caret | `Clip(caret rect)` |
| Engine | media query / window size | whole document, the cascade round already running |
| Engine | container-query second round | at most 2 rounds, counted in §3 |
| Engine | image intrinsic size arriving next frame | `Box` of that box |

## 3. Invariants

Numbered once for the repository. Code comments, commits, and gate cases cite these
names.

A "frame" is the `frame` zone. Several cascade/layout rounds may run (flush before
input, container queries ≤ 2, layout after transitions). Upper bounds are
`rounds × |closure|` except `laid.boxes`, which is deduped. Each round is accounted
separately.

A `layout()` may retry for a scrollbar gutter (`bar_retries`) or a still-dirty root
after container queries (`dirty_retries`). Those retries call `cache_.layout` and do
not `mark_needs_layout`, so they do not multiply `style.intern` / `box.created`.
`bar_retries` changes the root `ConstraintSpace`; the conservative I5 bound for that
frame is the whole document (an upper bound, not "every box must compute"). Overflow
walls (`overflow != visible`) that remeasure their subtree (`measure_again`, flex/grid
bar reruns) bound to **that wall's subtree**.

| # | Invariant | Gate |
| --- | --- | --- |
| **I1** | Idle: empty `D_frame` does no work | `cascade.visited`, `style.intern`, `box.created`, `laid.boxes`, `paint.walk`, `hit.boxes`, `world.walk` all 0. Do not use `paint.items`. A caret, a running transition, or a media change means `D_frame` is not empty. |
| **I2** | Idempotent: writing the same value does no work | Second call: cascade / intern / created / laid.boxes all 0. `add_class` / `remove_class` already early-out. |
| **I3** | Cascade stays inside the closure | `Σ cascade.visited ≤ rounds × \|closure(D)\|` (same for `recomputed`). Inherited and counter restyle are inside the closure. |
| **I4** | Relower root is the smallest element whose child *set* changed | `box.created` ≤ boxes in that subtree. `display:none` returns; `contents` recurses to children; a host that already has a box relowers in place. Climbing to an ancestor because the host has no box yet is a violation. |
| **I5** | `laid.boxes ⊆ closure_box(D) ∪ coupled boxes` | Coupled boxes are CSS: flex/grid free space, later in-flow positions, table columns, collapsed-border tables, wall-internal second measure. Coupling stops at the nearest overflow wall. `laid.stores` is not a gate. `bar_retries > 0` raises the bound to the document. |
| **I6** | Paint only the damaged region | `damage.items`; no change ⇒ `paint.walk == 0`. |
| **I7** | Hit-test reads this frame's final world | `hit.boxes == 0` only when pointer, keys, *and* the world version last observed by hit-test are unchanged. Scroll updates world after the pointer pass; a scroll frame hit-tests again. |
| **I8** | Data reads do no work | `get` leaves every counter unchanged. `Document::measure` flushes (CSSOM View) and counts as the current interaction. `frame::Access::measure` does not flush. |
| **I9** | One input event stays within §2.2 bounds | `laid.boxes` does not multiply by rounds; `laid.stores` stays out. The closure may be the whole document. |

## 4. A-tier cost contract

Three bands, because the same method is cheap or structural depending on what CSS does:
**paint-only** / **geometry** / **structure**. Paint-only still re-interns when paint
reads a `LayoutStyle` field (`border-radius`, `border-*-style`).

| Call | Dirty unit | Bound |
| --- | --- | --- |
| `set` paint-only | `Node(id)` | `laid.boxes == 0`; intern ≤ this element's boxes (plus collapsed-table boxes) |
| `set` geometry | `Subtree` or `Box` | intern / laid.boxes inside closure |
| `set("visible")` | `Node`; diff on `Children(parent)` | relower on the parent only (I4) |
| `set` same value | none | I2 |
| `add_class` / `remove_class` | `Node` or `Subtree`, by CSS | already-present / missing class is 0 work |
| `set_model` | `Children(id)` | rebuild root is `id`; created boxes stay in changed rows; equal model is 0 (I2, I4, I5) |
| `load_fragment` | `Children(parent)` | created boxes stay inside the fragment |
| `unload_fragment` | `Children(parent)` | no boxes created |
| `set_theme` | whole document | the one A-tier call allowed a document-wide cascade; re-applying the current theme is 0 (I2) |
| `get` | none | I8 |
| `measure` | flush of pending work | counted on this interaction, not a new dirty source |
| `focus` / `blur` / `scroll_into_view` | one input event | I9 |

## 5. Tracing

Three channels:

| Channel | Product | Question |
| --- | --- | --- |
| `ZUI_PERF=1` | one text line per frame | milliseconds, cache hits, shape calls |
| difftest trace | `zui.difftest.trace/2` JSON | geometry vs Chrome ([TESTING.md](./TESTING.md)) |
| Perfetto | `.pftrace` | which algorithm, which box; query with SQL |

Write path is the Perfetto Tracing SDK (`third_party/perfetto`). Read path is official
`trace_processor`. Aggregate in the engine, not in the prompt.

```powershell
pwsh tools/trace/fetch_trace_processor.ps1
build/windows-x64-release/zui_dump.exe build/perf/grid60.html `
    --size 1100x720 --repeat 6 --trace build/trace/grid60.pftrace
build/tools/trace_processor_shell.exe query -f tools/trace/queries/03_hotspots.sql build/trace/grid60.pftrace
```

An application records with `ZUI_TRACE` / `ZUI_PERF`, or `pwsh tools/trace/run_app.ps1 studio -Perf`.
Work units are mirrored as `work.*` plus `laid.boxes` / `frame.laid` when a recording
window closes ([ARCHITECTURE.md](./ARCHITECTURE.md)).
