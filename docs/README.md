# docs

This folder is for people who work on zui: contributors, reviewers, and coding agents.

It holds two kinds of page:

1. **Mechanism** — how the engine works today, written against `source/` and `include/zui`.
2. **Convention** — what a change may add, where it lives, and how it is proven.

A page that describes yesterday's types, a PR plan, a capability gap, or a dated test run
does not belong here. Application walkthroughs live next to the programs; the index is
[example/README.md](../example/README.md).

When code and a page disagree, the page is wrong until the same change rewrites it.

Source comments may still name retired slice labels (`PERF_REFACTOR` capability letters,
`RFC_MODEL` waves). Those labels identify landed code. They are not pages.

## Index

| Page | Kind | Read it when |
| --- | --- | --- |
| [CONTRACT.md](./CONTRACT.md) | Convention | Changing `include/zui`, examples, or the author surface |
| [ARCHITECTURE.md](./ARCHITECTURE.md) | Mechanism | Adding a module, a backend, a plugin, or a frame-loop step |
| [MARKUP.md](./MARKUP.md) | Mechanism | Touching `source/html` or `source/css` |
| [LAYOUT.md](./LAYOUT.md) | Mechanism | Touching `source/layout` or the box tree |
| [EVENT.md](./EVENT.md) | Mechanism | Touching `event.h`, `bind`, input, or `set_model` |
| [PERF.md](./PERF.md) | Convention | Hot-path work in `css/`, `layout/`, `paint/`, `flush_style` |
| [TESTING.md](./TESTING.md) | Convention | Adding tests, probes, or Chrome comparison |

[AGENTS.md](../AGENTS.md) is the short form of the conventions. Keep its A-tier list identical
to [CONTRACT.md](./CONTRACT.md) §3.
