// The Engine's own surface: the user-agent stylesheet and sheet load / theme replacement.
// The selector facts and the cascade entry points live in sibling files.
// The Engine's own surface: the user-agent stylesheet, sheet load / theme replacement, the
// selector facts and closure bound derived from the loaded sheets, and the cascade entry point
// that drives `cascade_walk.cpp`. The cascade phases themselves live in the sibling files:
// selector_match, rule_cascade, computed_style, style_diff, generated_content, selection_style.

#include "css/engine/engine.h"
#include "css/engine/cascade_internal.h"
#include "css/engine/http_fetch.h"
#include "css/engine/nesting.h"
#include "css/engine/strip_keyframes.h"
#include "css/shorthand/shorthand.h"
#include "css/values.h"

#include "log/log.h"
#include "perf/perf_budget.h"
#include "text/font_chain.h"
#include "trace/trace.h"

#include <lexbor/css/at_rule.h>
#include <lexbor/css/css.h>
#include <lexbor/css/selectors/pseudo_const.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace zui {
namespace css {

// The cascade phases live in sibling translation units (selector matching, rule cascade,
// computed style, generated content, selection, the tree walk); their internal API is declared
// in cascade_internal.h.
using namespace detail;

namespace {

constexpr const char kUaCss[] =
    // Chromium / HTML rendering: margin:8px is on body only. Putting it on html as well
    // doubled the horizontal inset (vertical margins collapse with body).
    // Edge on this host reports the standard font name for the initial family, not the
    // generic keyword (`getComputedStyle(documentElement).fontFamily` is "Noto Sans SC").
    "html { display: block; color: #000000; font-size: 16px; font-family: \"Noto Sans SC\"; }\n"
    "body { display: block; margin: 8px; color: #000000; font-size: 16px; "
    "font-family: \"Noto Sans SC\"; }\n"
    // Chromium's html.css gives html/body no width or height: both are content-sized block
    // boxes. Forcing `height: 100%` made every document exactly viewport-tall, which hid the
    // viewport scrollbar (the root never overflowed) and put a 600px box under every page in
    // the differential traces.
    // `link` is its own rule. In the middle of this list the selector parser drops
    // every type that follows it, so meta, title and template stayed display:inline.
    "head, script, style, meta, title, template { display: none; }\n"
    "link { display: none; }\n"
    // `noscript` gets no rule of its own: with scripting enabled the parser keeps its content as
    // one raw-text child and the browser renders neither, so the element is an ordinary empty
    // inline box (measured against headless Chrome, `build/noscript_probe.html`). The box tree
    // skips that child (see BoxTree::build), which is what leaves the element at 0x0.
    // Chromium html.css / HTML Rendering: common flow sections + headings.
    "div, header, footer, aside, main, section, article, nav, search, hgroup, "
    "p, h1, h2, h3, h4, h5, h6, address, blockquote, figure, figcaption, "
    "center, form, details, summary { display: block; }\n"
    // HTML: a closed <details> shows only its summary. The click default toggles `open`
    // (EVENT.md section 7). The second rule wins over the universal hide so the summary stays.
    "details:not([open]) > * { display: none; }\n"
    "details:not([open]) > summary { display: block; }\n"
    // HTML's suggested default stylesheet. Author margins and font sizes win; a bare
    // paragraph was missing the 1em block margins Chrome lays out.
    "p { margin: 1em 0; }\n"
    "h1 { font-size: 2em; margin: 0.67em 0; font-weight: bold; }\n"
    "h2 { font-size: 1.5em; margin: 0.83em 0; font-weight: bold; }\n"
    "h3 { font-size: 1.17em; margin: 1em 0; font-weight: bold; }\n"
    "h4 { font-size: 1em; margin: 1.33em 0; font-weight: bold; }\n"
    "h5 { font-size: 0.83em; margin: 1.67em 0; font-weight: bold; }\n"
    "h6 { font-size: 0.67em; margin: 2.33em 0; font-weight: bold; }\n"
    // Chromium html.css nested-section h1 shrink (:-webkit-any → :is). Depth 1..5 maps to
    // h2..h6 sizes; deeper nesting keeps the h6 size.
    ":is(article, aside, nav, section) h1 { font-size: 1.5em; margin: 0.83em 0; }\n"
    ":is(article, aside, nav, section) :is(article, aside, nav, section) h1 { "
    "font-size: 1.17em; margin: 1em 0; }\n"
    ":is(article, aside, nav, section) :is(article, aside, nav, section) "
    ":is(article, aside, nav, section) h1 { font-size: 1em; margin: 1.33em 0; }\n"
    ":is(article, aside, nav, section) :is(article, aside, nav, section) "
    ":is(article, aside, nav, section) :is(article, aside, nav, section) h1 { "
    "font-size: 0.83em; margin: 1.67em 0; }\n"
    ":is(article, aside, nav, section) :is(article, aside, nav, section) "
    ":is(article, aside, nav, section) :is(article, aside, nav, section) "
    ":is(article, aside, nav, section) h1 { font-size: 0.67em; margin: 2.33em 0; }\n"
    // Chromium html.css: blockquote / figure indent; address is italic; center uses text-align.
    "blockquote, figure { margin-block: 1em; margin-inline: 40px; }\n"
    "address { font-style: italic; }\n"
    "center { text-align: center; }\n"
    // Chromium html.css: pre keeps whitespace; hr is a 1px inset rule with auto inline margins.
    "pre, xmp, plaintext, listing { display: block; font-family: monospace; "
    "white-space: pre; margin-block: 1em; margin-inline: 0; }\n"
    "hr { display: block; overflow: hidden; margin-block: 0.5em; margin-inline: auto; "
    "border-style: inset; border-width: 1px; color: gray; }\n"
    // Chromium/HTML Rendering dialog: closed is display:none; open is absolute + centered
    // with left/right:0 + margin:auto (physical; inset-inline not mapped yet).
    // :modal / ::backdrop: declarative `open` sets Node::modal (top-layer stand-in until
    // show()/showModal host split). Canvas/CanvasText → white/black.
    "dialog { display: none; position: absolute; left: 0; right: 0; "
    "width: fit-content; height: fit-content; margin: auto; border: solid; "
    "padding: 1em; background-color: #ffffff; color: #000000; }\n"
    "dialog[open] { display: block; }\n"
    "dialog:modal { position: fixed; overflow: auto; }\n"
    "dialog::backdrop { background-color: rgba(0, 0, 0, 0.1); }\n"
    // Chromium html.css ruby. `layout_ruby` stacks ruby-text above the base (simple ruby).
    // `rt` 50% matches Blink/WebKit ruby-text sizing (approx -webkit-ruby-text).
    "ruby { display: ruby; }\n"
    "rt { display: ruby-text; font-size: 50%; }\n"
    "rp { display: none; }\n"
    // Chromium html.css inline phrase elements.
    "strong, b { font-weight: bolder; }\n"
    "i, cite, em, var, dfn { font-style: italic; }\n"
    "tt, code, kbd, samp { font-family: monospace; }\n"
    "u, ins { text-decoration: underline; }\n"
    "s, strike, del { text-decoration: line-through; }\n"
    // Phrase tags stay inline even when only text-decoration is set (no display longhand).
    // Do not list ruby/rt here — that would override display:ruby / ruby-text above.
    "span, a, label, u, s, strike, del, ins, b, i, small, sub, sup, "
    "cite, var, dfn, tt, kbd, samp, abbr, acronym, bdi, bdo, mark, q, output, wbr { "
    "display: inline; }\n"
    // Chromium html.css residual phrase / bidi / highlight (cold-tag batch).
    // `smaller` ≈ next absolute size below medium (13.3333px / 0.833333em); keyword not resolved yet.
    "small { font-size: 0.833333em; }\n"
    "sub { vertical-align: sub; font-size: 0.833333em; }\n"
    "sup { vertical-align: super; font-size: 0.833333em; }\n"
    "abbr[title], acronym[title] { text-decoration: dotted underline; }\n"
    "mark { background-color: #ffff00; color: #000000; }\n"
    "q { display: inline; }\n"
    "bdi, output { unicode-bidi: isolate; }\n"
    "bdo, bdo[dir] { unicode-bidi: isolate-override; }\n"
    "wbr { line-break: anywhere; }\n"
    // Form controls do not inherit the page font: Chromium's html.css gives them
    // `font: 400 13.3333px Arial` and its own borders, and the two box-sizing values
    // differ per control, which changes their measured size.
    "input, textarea, select, button { display: inline-block; margin: 0; "
    "font: 400 13.3333px arial; color: inherit; }\n"
    // Headless Edge paints a push button as 2px outset of black. Blink's very-dark
    // inset/outset path turns that into the 168/84 bevel. A flat stand-in color
    // leaves a 200px edge along every unstyled button.
    "button { box-sizing: border-box; text-align: center; border: 2px outset #000000; "
    "padding: 1px 6px; }\n"
    // CSS UI / Chromium html.css: interactive controls advertise a system cursor (HOST PR-8).
    "a, button, input[type=\"button\"], input[type=\"submit\"], input[type=\"reset\"], "
    "summary { cursor: pointer; }\n"
    "input:not([type]), input[type=\"text\"], input[type=\"search\"], input[type=\"password\"], "
    "input[type=\"email\"], input[type=\"url\"], input[type=\"tel\"], input[type=\"number\"], "
    "textarea { cursor: text; }\n"
    // No width or height here. `input[type=checkbox]` outranks an author class, so
    // `width: 1px` on a visually hidden control never applied. The 13px widget size
    // is the used value of `auto`, applied in layout.
    // The padding and border are stated here, not inherited from the generic `input` rule below:
    // the widget draws its own box, and Chromium's computed style for a checkbox or radio is
    // `padding: 0` / `border-width: 0` (measured with `build/form_ua_probe.html`). Leaving them to
    // the generic rule made every radio 1px/2px of padding and a 2px border wider than Chrome -
    // the calendar's colour swatches were 6px too wide and the whole row shifted.
    "input[type=\"checkbox\"], input[type=\"radio\"] { "
    "margin: 3px 3px 3px 4px; box-sizing: border-box; padding: 0; border: 0; }\n"
    // Chromium-like defaults: range/progress are replaced-looking widgets with a
    // preferred size when width/height are auto (applied in layout, not here).
    "input[type=\"range\"] { margin: 2px 0; padding: 0; border: none; "
    "box-sizing: border-box; background-color: transparent; cursor: default; }\n"
    // File control: button + filename chrome; preferred size applied in layout when auto.
    // Chromium's html.css gives the file control NO border and NO padding - the visible button
    // and filename live in the shadow widget, and the host element's own box is content-box
    // (measured with build/uainputprobe.py: border 0 / padding 0 / box-sizing content-box, and
    // the widget's used size comes from its intrinsic width). Keeping a UA border here made a
    // visually hidden `width: 1px; height: 1px` file input 2x2 instead of 1x1, which pushed the
    // root box 2px past the viewport and turned on BOTH viewport scrollbars on every page that
    // hides one (example-album, -reader, -zmd, reader-states).
    "input[type=\"file\"] { box-sizing: content-box; padding: 0; "
    "border: 0; background-color: #ffffff; cursor: default; "
    // Author `overflow: hidden` on a visually hidden file control still computes to
    // `clip` in Edge, so the UA value wins.
    "overflow: clip !important; white-space: nowrap; text-overflow: ellipsis; "
    "align-items: baseline; }\n"
    "progress { display: inline-block; box-sizing: border-box; "
    "vertical-align: -0.2em; border: 1px solid #767676; background-color: #efefef; "
    "padding: 0; margin: 0; }\n"
    // Chromium html.css AppearanceBase-off meter preferred size (5em×1em); no UA border.
    "meter { display: inline-block; box-sizing: border-box; width: 5em; height: 1em; "
    "vertical-align: -0.2em; padding: 0; margin: 0; }\n"
    "img { display: inline-block; }\n"
    "br { display: inline; }\n"
    // Chromium html.css (AppearanceBase off): text fields are content-box with
    // padding-block 1px / padding-inline 2px; select and search are border-box.
    // Textarea keeps content-box and a 2px padding on every side.
    "input, textarea { box-sizing: content-box; text-align: start; "
    "border: 2px solid #3d4654; }\n"
    "input { padding: 1px 2px; }\n"
    // Chromium html.css, appearance not auto: text fields are `2px inset` of
    // light-dark(#767676, #858585). Dark/Light of #767676 is the 33/202 sunken
    // edge. Checkbox, radio, and file keep the borders set on their own rules.
    // Chromium reports `overflow: clip` on a text field (album `#album-name`), including one
    // that is `display: none`.
    // Chromium html.css: these fields are `white-space: pre`. `normal` would collapse the
    // repeated spaces a caret has to walk, and `nowrap` collapses them too. `pre` keeps each
    // space and does not wrap a single-line field. overflow stays `clip`.
    "input:not([type]), input[type=\"text\"], input[type=\"search\"], input[type=\"password\"], "
    "input[type=\"email\"], input[type=\"url\"], input[type=\"tel\"], input[type=\"number\"] { "
    "border: 2px inset #767676; overflow: clip; white-space: pre; }\n"
    // Chromium html.css: a textarea keeps its line breaks. Without it `white-space: normal`
    // folds the "\n" the Enter default inserts into a space (EVENT.md section 7).
    "textarea { padding: 2px; white-space: pre-wrap; overflow-wrap: break-word; overflow: auto; }\n"
    // Closed <select> paints the selected label via node->text; options stay out of flow until
    // Interaction sets the `open` attribute (HOST PR-15). Open options are position:fixed under
    // the control so a dialog's overflow:auto does not clip the list.
    // Closed <select> matches Edge's computed style: static, overflow clip, centered,
    // `white-space: pre`. The open list is `position: fixed` on the options, so the control
    // itself does not need to be the containing block. min-width/min-height stay the initial
    // `auto` (0 when the control is `display: none`).
    "select { box-sizing: border-box; text-align: start; "
    "border: 1px solid #767676; padding: 1px 22px 1px 4px; "
    "cursor: default; overflow: clip; align-items: center; white-space: pre; }\n"
    "select[open] { z-index: 10000; }\n"
    // HTML suggested rendering section 15.5.16: both are `display: block`. The light-DOM list
    // inside a closed `<select>` is not rendered at all (the control shows a shadow listbox), so
    // the engine keeps it out of flow in the box tree instead (`box_tree/tree.cpp`).
    // Chromium html.css: a closed control paints through its internal listbox, so an option's
    // computed style carries the picker's own box - padding 0/2px/1px, a 24px minimum on both
    // axes, a centred line with a 0.5em gap, `white-space: nowrap` and `line-height: normal` (the
    // form-control `font` reset reaches the option through the select). A listbox option
    // (`select[size]`) has no picker box: a 16px minimum, no gap, no centring. Measured with
    // `build/form_ua_probe.html` against the pinned Edge.
    "option { display: block; padding: 0 2px 1px; white-space: nowrap; line-height: normal; "
    "min-width: 24px; min-height: 24px; align-items: center; "
    "row-gap: 0.5em; column-gap: 0.5em; }\n"
    "select[size] > option { min-width: 0; min-height: 16px; align-items: normal; "
    "row-gap: normal; column-gap: normal; }\n"
    "optgroup { font-weight: bolder; display: block; }\n"
    "select[open] > optgroup { display: block; }\n"
    "select[open] > option { display: block; position: fixed; box-sizing: border-box; "
    "margin: 0; padding: 2px 8px; border: 1px solid #767676; background-color: #ffffff; "
    "color: #000000; white-space: nowrap; cursor: default; z-index: 10001; }\n"
    "select[open] > option:hover, select[open] > option[selected] { "
    "background-color: #0078d7; color: #ffffff; }\n"
    "input[type=\"search\"] { box-sizing: border-box; }\n"
    // Static datalist has no picker UI yet — HTML rendering hides it (Chromium popover path
    // deferred).
    "datalist { display: none; }\n"
    // Chromium html.css. Cells inherit, so a tall row centers a short cell's line.
    // `table > tr` covers a row that was not wrapped in a row group.
    "thead, tbody, tfoot { vertical-align: middle; }\n"
    "table > tr { vertical-align: middle; }\n"
    // HTML's suggested rendering: a row and a cell take the row group's alignment through
    // `inherit`, so a `td` in a `tbody` is `middle` and a page that sets `vertical-align: top` on
    // the group carries it down. `vertical-align` itself does not inherit (CSS 2.1), which is why
    // this is stated per table part instead of being added to the inherited set.
    "tr, td, th { vertical-align: inherit; }\n"
    // Chromium's html.css: cells carry a 1px padding unless the page says otherwise. Without it a
    // declared cell width came out one border-width narrower than the browser's box.
    "td, th { padding: 1px; }\n"
    // HTML's suggested default. A header cell centers in its column; the column is often
    // wider than the heading because of the cells below it.
    "th { text-align: center; }\n"
    // HTML Living Standard §Rendering Lists (and Chromium html.css). Logical box edges are
    // stored as specified, then `resolve_logical_box_edges` writes the matching physical
    // longhands so layout and getComputedStyle-style dumps see padding-left/right/… .
    // Nested `:is(…)` rules zero block margins and step disc→circle→square. `counter-reset:
    // list-item` is dump fidelity; list-item counter scopes are driven by container tags in
    // cascade_node (author `counter-*` is still out). `text-align: match-parent` on `li` is
    // omitted until that keyword is consumed.
    "dir, dd, dl, dt, menu, ol, ul { display: block; }\n"
    "li { display: list-item; }\n"
    "dir, dl, menu, ol, ul { margin-block: 1em; }\n"
    ":is(dir, dl, menu, ol, ul) :is(dir, dl, menu, ol, ul) { margin-block: 0; }\n"
    "dd { margin-inline-start: 40px; }\n"
    "dir, menu, ol, ul { padding-inline-start: 40px; }\n"
    "ol, ul, menu { counter-reset: list-item; }\n"
    "ol { list-style-type: decimal; }\n"
    "dir, menu, ul { list-style-type: disc; }\n"
    ":is(dir, menu, ol, ul) :is(dir, menu, ul) { list-style-type: circle; }\n"
    ":is(dir, menu, ol, ul) :is(dir, menu, ol, ul) :is(dir, menu, ul) { "
    "list-style-type: square; }\n"
    // Chromium html.css cold tags / form residual (B1 UA table long tail).
    // fieldset/legend: groove + ThreeDFace ≈ #c0c0c0; min-inline-size:min-content kept.
    "fieldset { display: block; margin-inline: 2px; border: groove 2px #c0c0c0; "
    "padding-block: 0.35em 0.625em; padding-inline: 0.75em; min-inline-size: min-content; }\n"
    "legend { display: block; padding-inline: 2px; }\n"
    // Replaced / embedded defaults. -webkit-fill-available → width:100% for marquee.
    // Without an explicit display, tag fallback would make these block; Chromium leaves them inline.
    // HTML suggested rendering section 15.5.13: a marquee is a scroll container (its baseline is
    // then the bottom margin edge, and the line it sits on is one strut descent taller).
    "marquee { display: inline-block; width: 100%; overflow: hidden !important; }\n"
    "iframe, embed, object, video, canvas, audio { display: inline; }\n"
    "iframe { border: 2px inset; }\n"
    "iframe, embed, object { overflow: clip; }\n"
    "video { object-fit: contain; }\n"
    "video, canvas, img { overflow: clip; }\n"
    "audio { width: 300px; height: 54px; }\n"
    "audio:not([controls]) { display: none !important; }\n"
    // Table parts Chromium states explicitly (tag fallback already maps table/col*).
    "table { display: table; box-sizing: border-box; border-spacing: 2px; "
    "border-collapse: separate; text-indent: initial; }\n"
    // HTML suggested rendering section 15.3.8 states every part's display. Without them a `<tr>`
    // computed to the initial `inline` while the layout still built a row, so the computed-style
    // dump disagreed with the browser on the very elements the table fixed up.
    "thead { display: table-header-group; }\n"
    "tbody { display: table-row-group; }\n"
    "tfoot { display: table-footer-group; }\n"
    "tr { display: table-row; }\n"
    "td, th { display: table-cell; }\n"
    "col { display: table-column; }\n"
    "colgroup { display: table-column-group; }\n"
    "caption { display: table-caption; text-align: center; }\n"
    // Hidden / contents stand-ins.
    "area, param, noframes { display: none; }\n"
    "map { display: inline; }\n"
    "slot { display: contents; }\n"
    // Cascade 5 §6.1 probe id: UA !important must beat author normal / author !important.
    // Keep the selector id-private so other pages are unaffected.
    "#ua-important-probe { width: 120px !important; background-color: #7cb7ff !important; }\n"
    "#ua-important-vs-author-important { width: 120px !important; "
    "background-color: #7cb7ff !important; }\n";

// The properties CSS inherits and the engine consumes. `writing-mode` was missing, so a page
// that set it on a container laid its children out horizontally: every child resolved its own
// computed style to the initial `horizontal-tb`, its block axis became the physical vertical
// one, and block-axis margins stopped applying. The rest of the list is the standard inherited
// text set; layout reads the ones LAYOUT.md section 2 interns and leaves the others to the
// computed-style dump.
























}  // namespace

Engine::~Engine() { clear_sheets(); }

Engine::Engine()
    : memo_(std::make_unique<detail::CascadeMemo>()),
      ancestor_facts_(std::make_unique<detail::NeighborFacts>()),
      sibling_facts_(std::make_unique<detail::NeighborFacts>()) {}

void Engine::forget_subtree(const html::Node* dead) {
    if (memo_ != nullptr) {
        memo_->forget(dead);
    }
}

void Engine::clear_sheets() {
    for (lxb_css_stylesheet* sst : sheets_) {
        if (sst != nullptr) {
            lxb_css_stylesheet_destroy(sst, true);
        }
    }
    sheets_.clear();
    sources_.clear();
    parsed_sources_.clear();
    detail::clear_decl_expansion();
    // The targets are views into the destroyed sheets.
    hover_targets_.clear();
    active_targets_.clear();
    has_anchors_.clear();
    theme_sheet_ = nullptr;
}

bool Engine::load_sheets(html::Document& doc, std::string& error) {
    trace::Zone zone("css.parse", trace::Category::Parse);
    clear_sheets();
    lxb_css_parser_t* parser = lxb_css_parser_create();
    if (parser == nullptr || lxb_css_parser_init(parser, nullptr) != LXB_STATUS_OK) {
        error = "CSS parser init failed";
        if (parser != nullptr) {
            lxb_css_parser_destroy(parser, true);
        }
        return false;
    }
    (void)lxb_css_parser_selectors_init(parser);

    // `first` is the stylesheet. `second` is the buffer lexbor's token offsets address
    // (`@keyframes` removed). Callers keep the original text separately for Animations.
    auto parse_one = [&](const lxb_char_t* data, size_t len) -> std::pair<lxb_css_stylesheet*, std::string> {
        const std::string_view view(reinterpret_cast<const char*>(data), len);
        // Lexbor keeps a nested style rule's selector as written, so `&` would match the
        // wrong elements. Flatten nesting first. Lexbor also has no @keyframes type; the
        // original text stays in sources_ for Animations. Prelude offsets are into `parsed`.
        std::string parsed = css_expand_nesting(view);
        if (parsed.find("@keyframes") != std::string::npos) {
            parsed = css_without_keyframes(parsed);
        }
        const lxb_char_t* parse_data = reinterpret_cast<const lxb_char_t*>(parsed.data());
        size_t parse_len = parsed.size();
        lxb_css_stylesheet_t* sst = lxb_css_stylesheet_create(nullptr);
        if (sst == nullptr) {
            return {nullptr, {}};
        }
        if (lxb_css_stylesheet_parse(sst, parser, parse_data, parse_len) != LXB_STATUS_OK) {
            ZUI_WARN("zui::css: stylesheet parse failed ({} bytes skipped)", len);
            lxb_css_stylesheet_destroy(sst, true);
            return {nullptr, {}};
        }
        perf::note_css_sheet();
        return {sst, std::move(parsed)};
    };
    auto keep = [&](std::pair<lxb_css_stylesheet*, std::string> parsed, std::string original) {
        if (parsed.first == nullptr) {
            return;
        }
        sheets_.push_back(parsed.first);
        sources_.push_back(std::move(original));
        parsed_sources_.push_back(std::move(parsed.second));
    };

    keep(parse_one(reinterpret_cast<const lxb_char_t*>(kUaCss), sizeof(kUaCss) - 1),
         std::string(kUaCss, sizeof(kUaCss) - 1));

    namespace fs = std::filesystem;
    const fs::path base = fs::path(doc.source_path).parent_path();
    for (const html::Document::StylesheetSource& sheet : doc.stylesheets) {
        if (sheet.href.empty()) {
            // `<style>` element text: already the CSS, so it only needs parsing.
            // Relative `url()` resolves against the document, same as a browser.
            text::register_font_faces(sheet.text, base);
            keep(parse_one(reinterpret_cast<const lxb_char_t*>(sheet.text.data()),
                           sheet.text.size()),
                 sheet.text);
            continue;
        }
        if (sheet.href.find("://") != std::string::npos) {
            // Remote author sheets: fetch+cache http(s); fail soft on network / unsupported scheme.
            std::string bytes;
            if (!fetch_url(sheet.href, bytes)) {
                continue;
            }
            // Argument evaluation order is unspecified. Moving `bytes` in the same call as
            // `parse_one` can parse an already-empty string (MSVC evaluates right to left).
            text::register_font_faces(bytes, {});
            auto parsed =
                parse_one(reinterpret_cast<const lxb_char_t*>(bytes.data()), bytes.size());
            keep(std::move(parsed), std::move(bytes));
            continue;
        }
        const fs::path file =
            (fs::path(sheet.href).is_absolute() ? fs::path(sheet.href) : base / sheet.href);
        std::ifstream in(file, std::ios::binary);
        if (!in) {
            ZUI_WARN("zui::css: missing stylesheet {}", file.string());
            continue;
        }
        std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        text::register_font_faces(bytes, file.parent_path());
        auto parsed = parse_one(reinterpret_cast<const lxb_char_t*>(bytes.data()), bytes.size());
        keep(std::move(parsed), std::move(bytes));
    }

    lxb_css_parser_destroy(parser, true);
    refresh_selector_facts();
    ZUI_DEBUG("zui::css: {} sheet(s) accepted", sheets_.size());
    error.clear();
    return true;
}

bool Engine::add_sheet_file(std::string_view path, std::string& error) {
    std::ifstream in(std::string(path), std::ios::binary);
    if (!in) {
        error = "cannot open stylesheet";
        return false;
    }
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    text::register_font_faces(bytes, std::filesystem::path(std::string(path)).parent_path());
    return add_sheet_text(bytes, error);
}

bool Engine::add_sheet_text(std::string_view css, std::string& error) {
    std::string bytes(css);
    std::string stripped = css_expand_nesting(bytes);
    if (stripped.find("@keyframes") != std::string::npos) {
        stripped = css_without_keyframes(stripped);
    }
    const std::string* parsed = &stripped;
    lxb_css_parser_t* parser = lxb_css_parser_create();
    if (parser == nullptr || lxb_css_parser_init(parser, nullptr) != LXB_STATUS_OK) {
        error = "CSS parser init failed";
        if (parser != nullptr) {
            lxb_css_parser_destroy(parser, true);
        }
        return false;
    }
    (void)lxb_css_parser_selectors_init(parser);
    lxb_css_stylesheet_t* sst = lxb_css_stylesheet_create(nullptr);
    bool ok = sst != nullptr &&
              lxb_css_stylesheet_parse(sst, parser,
                                       reinterpret_cast<const lxb_char_t*>(parsed->data()),
                                       parsed->size()) == LXB_STATUS_OK;
    lxb_css_parser_destroy(parser, true);
    if (!ok) {
        if (sst != nullptr) {
            lxb_css_stylesheet_destroy(sst, true);
        }
        error = "stylesheet parse failed";
        return false;
    }
    sheets_.push_back(sst);
    sources_.push_back(std::move(bytes));
    parsed_sources_.push_back(*parsed);
    perf::note_css_sheet();
    refresh_selector_facts();
    error.clear();
    return true;
}

bool Engine::set_theme_file(std::string_view path, std::string& error) {
    if (theme_sheet_ != nullptr) {
        for (size_t i = 0; i < sheets_.size(); ++i) {
            if (sheets_[i] == theme_sheet_) {
                sheets_.erase(sheets_.begin() + static_cast<std::ptrdiff_t>(i));
                if (i < sources_.size()) {
                    sources_.erase(sources_.begin() + static_cast<std::ptrdiff_t>(i));
                }
                if (i < parsed_sources_.size()) {
                    parsed_sources_.erase(parsed_sources_.begin() + static_cast<std::ptrdiff_t>(i));
                }
                break;
            }
        }
        lxb_css_stylesheet_destroy(theme_sheet_, true);
        theme_sheet_ = nullptr;
    }
    if (path.empty()) {
        refresh_selector_facts();
        error.clear();
        return true;
    }
    std::string add_error;
    if (!add_sheet_file(path, add_error)) {
        error = add_error;
        return false;
    }
    theme_sheet_ = sheets_.back();
    error.clear();
    return true;
}

}  // namespace css
}  // namespace zui
