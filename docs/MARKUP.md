# Markup

Structure language: **HTML5 (WHATWG)**. Style language: **standard CSS**. This page
constrains `source/html` and `source/css`.

The picture contract is [CONTRACT.md](./CONTRACT.md) §1. zui does not run JavaScript, does
not expose a CSSOM API, and does not embed Chromium.

## 1. HTML5

UI documents use WHATWG [HTML](https://html.spec.whatwg.org/) tokenization and tree
construction. pugixml accepts well-formed XML only; `<input>`, `<img>`, omitted end tags,
and browser recovery fail there. UI documents are not parsed with pugixml.

`Document::load` accepts `.html` or `.zui`. Both are HTML5:

```html
<!DOCTYPE html>
<html>
<head>
  <title>ZUI Studio</title>
  <link rel="stylesheet" href="window.css">
</head>
<body>
  <button id="save" class="primary" onclick="on_save">Save</button>
  <input id="title" type="text" placeholder="Short title" oninput="on_title">
</body>
</html>
```

```cpp
app.document().load("window.html");
app.document().bind("on_save", [](zui::Event& e) { (void)e; });
app.document().set("title", "value", "Sprint focus");
```

`onclick` / `onchange` / `oninput` (and every other `on*`) hold a C++ `bind` name. There
is no `<script>`.

The window title comes from `<title>`. Custom controls use hyphenated tags
(`<color-picker id="c">`). An unknown tag is still a `Node`; CSS paints it.

Fragments (dialogs) parse as HTML fragments with `body` as context.

## 2. CSS: syntax, then used values

| Layer | Who | Spec | Rule |
| --- | --- | --- | --- |
| Lexer / syntax / selectors | embedded parser | [CSS Syntax](https://drafts.csswg.org/css-syntax-3/), [Selectors](https://drafts.csswg.org/selectors/) | Follow the spec, including error recovery. Unknown rules become custom; the file is kept. |
| Cascade / computed | `source/css` | [Cascade](https://drafts.csswg.org/css-cascade/) | Author origin, specificity, source order, `!important` |
| Layout / paint | `source/layout`, `source/paint` | Box, Flex, Grid, Position, Backgrounds, … | Implement by module. Unimplemented computed values stay on the node and do not participate. |

Parsing CSS is not layout. The stylesheet names algorithms and constraints (`display:
flex`, `width: 50%`). Used pixels need the viewport, text metrics, sibling intrinsic
sizes, and the containing block. Those do not exist when lexbor finishes the stylesheet.

An inline `style` attribute is a declaration on that node, with the specificity the spec
assigns. Theme switch = extra stylesheet + dirty the tree.

## 3. Parser: lexbor

HTML5 tree construction and CSS Syntax are a browser front end. zui spends its budget on
layout and GPU paint.

**[lexbor](https://github.com/lexbor/lexbor)** (Apache-2.0, C99, no deps) is vendored in
`third_party/lexbor` and wired by `cmake/deps.cmake`.

- HTML: WHATWG tree construction
- CSS: Syntax + Selectors + stylesheet; unknown declarations are custom
- Its Layout module is unused; zui lays out

lexbor types do not appear in `include/`. The lexbor HTML DOM is thrown away after the
zui `Node` tree is built. Matching and cascade run on the zui tree in `source/css`, so
`add_class` and `:hover` restyle one tree.

```
.html / .css bytes
  → lexbor HTML document + lexbor stylesheet (syntax + selector AST)
  → walk DOM, build zui Node tree (id table, onclick names, class)
  → drop lexbor HTML DOM
  → match, cascade, inherit; write Node.computed
  → layout / paint read the zui tree
```

`<style>` element bodies currently do not enter the cascade. Linked stylesheets do.

## 4. Bindings on attributes

| HTML | zui |
| --- | --- |
| `id` | structure-table key; `set` / `add_class` |
| `class` | class list; CSS selection |
| `onclick="on_save"` | behavior-table key; `Document::bind("on_save", …)` |
| `onchange` / `oninput` / every `on*` | same |
| text children, `value`, `placeholder`, `checked` | data fields |
| `<link rel="stylesheet" href>` | stylesheet source |
| `<title>` | window title |

`Document::set("save", "text", "Saved")` writes that element's text content, or an
input's `value`, by tag.

## 5. Directories

Nodes live in `source/html/` (tag, id, class, attributes, interned computed). CSS syntax
and the selector AST live in `source/css/`, with matching and cascade. Geometry is not on
the node. See [ARCHITECTURE.md](./ARCHITECTURE.md).
