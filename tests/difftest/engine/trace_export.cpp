#include "trace_export.h"

#include "input/interaction.h"
#include "layout/box.h"
#include "layout/box_role.h"
#include "layout/cache/box_cache.h"
#include "layout/cache/inline_content.h"
#include "layout/cache/layout_result.h"
#include "layout/engine.h"
#include "layout/layout_style.h"
#include "layout/paint_map.h"
#include "layout/cache/logical_axes.h"
#include "layout/scrollbar_h.h"
#include "layout/scrollbar_v.h"
#include "paint/painter_internal.h"

#include "css/values.h"
#include "html/document.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace zdt {
namespace {

// `Range.getClientRects()` for one code point is the caret span on the 1/64 px grid:
// floor(start) to ceil(end). Rounding each advance on its own is 1/64 or 2/64 short wherever
// the pen's fraction crosses a grid line. Checked against Edge on `inline-text`'s paragraph.
float caret_span(float start, float end) {
    const float down =
        static_cast<float>(std::floor(static_cast<double>(start) * 64.0 + 1e-9) / 64.0);
    const float up =
        static_cast<float>(std::ceil(static_cast<double>(end) * 64.0 - 1e-9) / 64.0);
    return up - down;
}

bool style_differs(const zui::html::Node& node, const char* key, const char* absent) {
    const std::string* value = node.style(key);
    return value != nullptr && *value != absent;
}

/// The CSS initial font size, which a relative length resolves against above the root element.
constexpr float kInitialFontPx = 16.f;

/// A length in the shape `getComputedStyle` writes it (`22.5px`).
std::string px_string(float value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%gpx", static_cast<double>(value));
    return buffer;
}

bool ends_with_ci(std::string_view value, std::string_view suffix) {
    if (value.size() < suffix.size()) {
        return false;
    }
    const std::size_t start = value.size() - suffix.size();
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(value[start + i])) != suffix[i]) {
            return false;
        }
    }
    return true;
}

bool has_alpha(std::string_view value) {
    for (unsigned char c : value) {
        if (std::isalpha(c) != 0) {
            return true;
        }
    }
    return false;
}

/// A length the exporter resolves itself, in px: `px` is itself, `em` and a bare number are
/// relative to `base`, `rem` to `root`, `%` to `base`. A unit the exporter does not model leaves
/// the base, which is the inherited answer.
float relative_px(const std::string& value, float base, float root) {
    const float amount = std::strtof(value.c_str(), nullptr);
    if (ends_with_ci(value, "px")) {
        return amount;
    }
    if (ends_with_ci(value, "rem")) {
        return amount * root;
    }
    if (ends_with_ci(value, "%")) {
        return base * amount / 100.f;
    }
    if (ends_with_ci(value, "em")) {
        return amount * base;
    }
    return has_alpha(value) ? base : amount * base;
}

/// `width` / `height` answer the used size in px for a box that has one, in the box the author's
/// `box-sizing` resolves it to: the **content** box for `content-box` and the **border** box for
/// `border-box` (measured on `build/scratch/used-size.html`: a 200px `border-box` box with 10px
/// padding and a 5px border reports 200, the same box with `content-box` reports 200 as well and
/// its rect is 230). An inline non-replaced box has no used size and keeps the specified value: a
/// `<strong>` inside a block reports `auto` where the block reports `800px`.
std::string reported_size(const zui::layout::Box* box, const char* prop, const std::string& value) {
    if (box == nullptr || box->style_ptr == nullptr) {
        return value;
    }
    const zui::layout::LayoutStyle& style = box->style();
    if (style.outer == zui::layout::DisplayOuter::Inline && !box->atomic_inline) {
        return value;
    }
    const bool inline_axis = prop[0] == 'w';
    if (style.border_box) {
        return inline_axis ? px_string(box->geo.width) : px_string(box->geo.height);
    }
    return inline_axis ? px_string(box->geo.content_width) : px_string(box->geo.content_height);
}

/// `min-width` / `min-height`: the initial `auto` has a used value of 0 except on a flex or grid
/// item, where it stays `auto` (CSS Sizing 4 section 5.1; measured the same way). An absolutely
/// positioned child of a flex container is **not** an item - it does not participate in the flex
/// layout (CSS Flexbox 1 section 4) - and Chrome answers `0px` for it; `build/computed_probe.py`
/// measured that directly (a `position: absolute` child of `display: flex` reports `0px` while its
/// in-flow sibling reports `auto`). It resolves even where there is no box - Chrome reports `0px`
/// for a `display: none` element - and a boxless element is never an item.
std::string reported_min_size(const zui::html::Node& node, const zui::layout::Box* box,
                              const std::string& value) {
    if (value != "auto") {
        return value;
    }
    const std::string* position = node.style("position");
    const bool out_of_flow =
        position != nullptr && (*position == "absolute" || *position == "fixed");
    if (box != nullptr && !out_of_flow && (box->is_flex_item || box->is_grid_item)) {
        return "auto";
    }
    return "0px";
}

/// `font-weight` is a number in the computed style. With a box the interned style carries the
/// used weight; without one - an element inside a `display: none` subtree - the keyword is enough,
/// because the engine resolves `bold` / `bolder` to 700 and `normal` / `lighter` to 400.
std::string reported_font_weight(const zui::layout::Box* box, const std::string& value) {
    const bool heavy = value == "bold" || value == "bolder";
    const bool light = value == "normal" || value == "lighter";
    if (!heavy && !light) {
        return value;
    }
    if (box != nullptr && box->style_ptr != nullptr) {
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%d", box->style().font_weight);
        return buffer;
    }
    return heavy ? "700" : "400";
}

/// `getComputedStyle().display` reports the **blockified** value (CSS Display 3 section 2.7):
/// Chrome answers `block` for an absolutely positioned `inline-block` and `flex` for an
/// `inline-flex` item (measured with `build/dispprobe.py`). The cascade keeps the specified value
/// on purpose - the static position of an out-of-flow inline box is taken from the line cursor it
/// would have sat on, so layout must still see `inline-block` - and the transformation is applied
/// here, at the boundary that mirrors the browser, the same way an animated property reads its
/// used value from `Node::used` instead of `computed`.
std::string blockified_display(const zui::html::Node& node, const std::string& display) {
    const std::string* position = node.style("position");
    const std::string* float_value = node.style("float");
    const bool out_of_flow =
        position != nullptr && (*position == "absolute" || *position == "fixed");
    const bool floating = float_value != nullptr && *float_value != "none";
    bool item = false;
    if (!out_of_flow && !floating) {
        // The container is the nearest ancestor that generates a box: `display: contents`
        // flattens away, so the item belongs to the container above it.
        for (const zui::html::Node* container = node.parent; container != nullptr;
             container = container->parent) {
            const std::string* value = container->style("display");
            if (value != nullptr && *value == "contents") {
                continue;
            }
            item = value != nullptr && (*value == "flex" || *value == "inline-flex" ||
                                        *value == "grid" || *value == "inline-grid");
            break;
        }
    }
    if (!out_of_flow && !floating && !item) {
        return display;
    }
    if (display == "inline" || display == "inline-block") {
        return "block";
    }
    if (display == "inline-flex") {
        return "flex";
    }
    if (display == "inline-grid") {
        return "grid";
    }
    if (display == "inline-table") {
        return "table";
    }
    return display;
}

// Same predicate the browser collector uses. A display:contents element still has a
// computed position, and Chrome names that element as the containing block even though
// it generates no box. The layout box that actually positions the child can be the
// ancestor above it; the trace has to name the element Chrome names.
bool makes_containing_block(const zui::html::Node& node, bool fixed) {
    const bool paints =
        style_differs(node, "transform", "none") || style_differs(node, "filter", "none") ||
        style_differs(node, "backdrop-filter", "none") || style_differs(node, "contain", "none");
    const std::string* container = node.style("container-type");
    const bool query = container != nullptr && *container != "normal";
    if (fixed) {
        return paints || query;
    }
    const std::string* position = node.style("position");
    const bool positioned = position != nullptr && *position != "static";
    return positioned || paints || query;
}

bool is_space_char(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

bool whitespace_only(const std::string& text) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (is_space_char(static_cast<char>(c))) {
            continue;
        }
        // U+3000 IDEOGRAPHIC SPACE collapses like whitespace but is not ASCII.
        if (c == 0xE3 && i + 2 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0x80 &&
            static_cast<unsigned char>(text[i + 2]) == 0x80) {
            i += 2;
            continue;
        }
        return false;
    }
    return true;
}

// The loader collapses an element whose content is only text into `node->text` and creates no
// `#text` child (see html/document.cpp, the `only_text` branch), while a mixed-content element
// gets real `#text` children and an empty `node->text`. Both ends must see the same text, so
// every check below has to look at both places. The browser has a text node child in the first
// case too, which is why collector.js only has to look at child nodes.
bool has_non_whitespace_text(const zui::html::Node* node) {
    if (node == nullptr) {
        return false;
    }
    if (node->tag == "#text") {
        return !whitespace_only(node->text);
    }
    // A <template>'s contents live in its document fragment, not in childNodes.
    // Chrome's text-owner walk never sees them; descending here made `#album-picks`
    // look like it owned the "{{name}}" chip and dropped its line list.
    if (node->tag == "template") {
        return false;
    }
    if (!whitespace_only(node->text)) {
        return true;
    }
    for (const auto& child : node->children) {
        if (has_non_whitespace_text(child.get())) {
            return true;
        }
    }
    return false;
}

/// A text owner is an element whose whole visible text comes from its own text and which has at
/// least one non-whitespace character of it. Only for those do the engine's line summary and the
/// browser's Range rects describe the same string (TESTPLAN section 6).
bool is_text_owner(const zui::html::Node* node) {
    // An input's value is not a child text node. Chrome's Range list for the control is
    // empty, so the line summary stays absent. A textarea's value is a child text node in
    // the engine's tree, but Chrome still reports no Range list for the control itself.
    if (node->tag == "input" || node->tag == "textarea") {
        return false;
    }
    bool direct_text = !whitespace_only(node->text);
    for (const auto& child : node->children) {
        if (child->tag == "#text") {
            if (!whitespace_only(child->text)) {
                direct_text = true;
            }
            continue;
        }
        if (has_non_whitespace_text(child.get())) {
            return false;
        }
    }
    return direct_text;
}

Json rect_array(float a, float b, float c, float d) {
    return Json::array({quantize(a), quantize(b), quantize(c), quantize(d)});
}

Json pair_array(float a, float b) { return Json::array({quantize(a), quantize(b)}); }

/// Node -> principal box, for both the trace walk and the diagnostics walk. They must agree
/// on it or a diagnostics entry would name a different element than the trace entry.
void index_element_boxes(const zui::layout::Box* box,
                         std::unordered_map<const zui::html::Node*, const zui::layout::Box*>& out) {
    if (box == nullptr) {
        return;
    }
    if (box->role == zui::layout::BoxRole::Element && box->node != nullptr) {
        out.emplace(box->node, box);
    }
    for (const zui::layout::Box* child : box->children) {
        index_element_boxes(child, out);
    }
}

struct Collector {
    zui::html::Document& doc;
    zui::layout::LayoutEngine& layout;
    const TraceOptions& options;
    std::unordered_map<const zui::html::Node*, const zui::layout::Box*> boxes;
    std::unordered_map<const zui::html::Node*, std::string> paths;
    Json elements = Json::array();
    std::size_t text_owners = 0;
    std::size_t line_boxes = 0;
    // The root element's scroll container is the viewport, so its `scroll_size` floor needs the
    // viewport size (TESTPLAN section 9's mapping rules).
    float viewport_w = 0;
    float viewport_h = 0;

    Collector(zui::html::Document& d, zui::layout::LayoutEngine& l, const TraceOptions& o)
        : doc(d),
          layout(l),
          options(o) {}

    void index_boxes(const zui::layout::Box* box) { index_element_boxes(box, boxes); }

    /// The root element's font size, which `rem` resolves against. The layout has already resolved
    /// it on the root box, so it does not need the walk below.
    float root_font_size() const {
        const zui::layout::Box* root = layout.root();
        if (root != nullptr && root->style_ptr != nullptr) {
            return root->style().font_size;
        }
        return kInitialFontPx;
    }

    /// The element's **used** font size. A box has it resolved by the layout. An element the
    /// layout never reached - a subtree under `display: none`, which the engine still interns
    /// boxes for, or a `<template>`'s content - resolves the computed `font-size` up the tree
    /// instead, where the parent's used size is the base and the CSS initial 16px sits above the
    /// root. `reported_line_height` needs this: `getComputedStyle` answers a used line-height for
    /// an element the layout skipped.
    float resolved_font_size(const zui::html::Node& node) const {
        const auto found = boxes.find(&node);
        if (found != boxes.end() && found->second->style_ptr != nullptr) {
            return found->second->style().font_size;
        }
        const float inherited =
            node.parent != nullptr ? resolved_font_size(*node.parent) : kInitialFontPx;
        const std::string* value = node.style("font-size");
        return value != nullptr ? relative_px(*value, inherited, root_font_size()) : inherited;
    }

    /// `getComputedStyle().line-height` answers the **used** length: a unitless line-height reports
    /// `22.5px` at 15px and `normal` stays `normal` (measured on `build/scratch/computed-rules.html`
    /// with `build/rectprobe.py`). A number, a percentage and `em` are relative to the element's
    /// own font size, `rem` to the root's - none of which needs a box, so an element inside a
    /// `display: none` subtree still reports `22.5px`.
    std::string reported_line_height(const zui::html::Node& node, const std::string& value) const {
        if (value.empty() || value == "normal") {
            return value;
        }
        // A menulist whose line-height only arrived through `font: inherit` reports the
        // keyword `normal` (album / kanban / contacts selects). The stored value still
        // equals the parent's, and layout keeps that used length — the control is 36px
        // tall, which is one 18px line plus the 8px padding and 1px border. An author
        // longhand that differs from the parent still reports its own used length.
        if (node.tag == "select" && node.parent != nullptr) {
            const std::string* parent_lh = node.parent->style("line-height");
            if (parent_lh != nullptr && *parent_lh == value) {
                return "normal";
            }
        }
        return px_string(relative_px(value, resolved_font_size(node), root_font_size()));
    }

    /// `letter-spacing` / `word-spacing` answer a px length, with `normal` staying itself. A
    /// relative length resolves against the element's own font size, which is what the layout
    /// does: `0.04em` at 12px reports `0.48px` (measured on the file-explorer page).
    std::string reported_spacing(const zui::html::Node& node, const std::string& value) const {
        if (value.empty() || value == "normal") {
            return value;
        }
        return px_string(relative_px(value, resolved_font_size(node), root_font_size()));
    }

    /// `margin-*` / `padding-*` / `*-gap` answer the **used** length once the specified value is a
    /// relative
    /// or viewport unit: `margin-top: 1em` at 15px reports `17.55px`, `padding-bottom: 30vh`
    /// reports `180px` (measured with `build/computed_probe.py` against the pinned Edge). A px
    /// length, a unitless number (`0`), a percentage and a keyword stay as they are: a
    /// percentage's used value needs the containing block's inline size, which this exporter does
    /// not carry - measured, Chrome answers `80px` for `margin-top: 10%` at an 800px containing
    /// block, so that one is a known gap rather than a wrong number.
    std::string reported_length(const zui::html::Node& node, const std::string& value,
                                float viewport_w, float viewport_h) const {
        if (value.empty() || value == "auto" || value == "normal" || value == "none" ||
            ends_with_ci(value, "px") || value.find('%') != std::string::npos ||
            !has_alpha(value)) {
            return value;
        }
        const zui::css::ResolvedLength resolved = zui::css::resolve_length(
            value, resolved_font_size(node), root_font_size(), viewport_w, viewport_h);
        return resolved.valid && !resolved.is_percent ? px_string(resolved.px) : value;
    }

    Json style_object(const zui::html::Node& node, const zui::layout::Box* box,
                      std::vector<std::pair<std::string, std::string>>& entries) const {
        Json out = Json::object();
        for (const std::string& prop : options.style_props) {
            std::string value;
            // An animated property reports its *used* value: TESTPLAN section 5 compares the
            // interpolation itself (transform matrix, opacity, lengths), and the browser's
            // getComputedStyle returns the animated value, not the specified one. The engine
            // keeps specified values in `computed` and the frame's animated overlay in
            // `Node::used`, so the trace reads the overlay for exactly the properties it covers.
            const zui::html::VisualUsed& used = node.used;
            if (prop == "opacity" && (used.flags & zui::html::VisualUsed::kOpacity) != 0) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(used.opacity));
                value = buf;
            } else if (const std::string* computed = node.style(prop.c_str())) {
                value = *computed;
            } else if (const auto it = options.initial.find(prop); it != options.initial.end()) {
                value = it->second;
            }
            // The rules below make the reported value the one `getComputedStyle` answers, whatever
            // the value's source was - a property the UA sheet never sets falls back to the initial
            // table and still needs them (an inline-level item of a grid container reports `block`;
            // the initial `min-width: auto` reports `0px` for a block).
            if (prop == "display") {
                value = blockified_display(node, value);
            } else if (prop == "line-height") {
                value = reported_line_height(node, value);
            } else if (prop == "letter-spacing" || prop == "word-spacing") {
                value = reported_spacing(node, value);
            } else if (prop == "width" || prop == "height") {
                value = reported_size(box, prop.c_str(), value);
                // A `display: none` box is not laid out, but Chrome still resolves
                // `min(240px, 46vw)` against the viewport (`#album-name` reports 240px).
                if (value.find("min(") == 0 || value.find("max(") == 0 ||
                    value.find("clamp(") == 0 || value.find("calc(") == 0) {
                    const zui::css::ResolvedLength resolved = zui::css::resolve_length(
                        value, resolved_font_size(node), root_font_size(), viewport_w, viewport_h);
                    if (resolved.valid && !resolved.is_percent) {
                        value = px_string(resolved.px);
                    }
                }
            } else if ((prop == "margin-left" || prop == "margin-right" || prop == "margin-top" ||
                        prop == "margin-bottom") &&
                       value == "auto" && box != nullptr &&
                       (box->is_flex_item || box->is_grid_item)) {
                // A flex/grid item's auto margin is a used length. A block that did not absorb
                // free space keeps the keyword.
                const zui::layout::PhysicalEdges& margin = box->geo.margin;
                const float used = prop == "margin-left"    ? margin.left
                                   : prop == "margin-right" ? margin.right
                                   : prop == "margin-top"   ? margin.top
                                                            : margin.bottom;
                value = px_string(used);
            } else if (prop == "min-width" || prop == "min-height" || prop == "max-width" ||
                       prop == "max-height") {
                // The `min-`/`max-` limits report a used length once the specified value is a
                // relative unit: `min-height: 1.2em` reports `15.6px` and `max-width: 16em` reports
                // `256px` (measured with build/computed_probe.py). `none` and a percentage stay.
                value = reported_min_size(node, box, value);
                // `min-height: 1.2em` reports the used px (`15.6px` on the album error line), and
                // so does a math function whose terms all resolved to lengths: zdb's `.topbar`
                // keeps `calc(56px + env(safe-area-inset-top))` in the declaration, whose computed
                // value is `56px` once the env substitutes 0px. The "already a plain px" test is
                // the suffix one, not "contains px anywhere", or the calc text slips through
                // (`reported_length` below uses the same test).
                if (value != "auto" && value != "none" && !ends_with_ci(value, "px") &&
                    value.find('%') == std::string::npos) {
                    const zui::css::ResolvedLength resolved = zui::css::resolve_length(
                        value, resolved_font_size(node), root_font_size(), viewport_w, viewport_h);
                    if (resolved.valid && !resolved.is_percent) {
                        value = px_string(resolved.px);
                    }
                }
            } else if (prop == "font-weight") {
                value = reported_font_weight(box, value);
            } else if (prop == "font-size") {
                // Chrome answers the used size: `1.17em` at a 15px parent reports `17.55px`
                // (measured with build/computed_probe.py), keywords included.
                value = px_string(resolved_font_size(node));
            } else if (prop == "margin-top" || prop == "margin-right" ||
                       prop == "margin-bottom" || prop == "margin-left" || prop == "padding-top" ||
                       prop == "padding-right" || prop == "padding-bottom" ||
                       prop == "padding-left" || prop == "row-gap" || prop == "column-gap") {
                value = reported_length(node, value, viewport_w, viewport_h);
            }
            value = normalize_style_value(value);
            entries.emplace_back(prop, value);
            out[prop] = value;
        }
        return out;
    }

    void lines_object(const zui::html::Node& node, const zui::layout::Box* box, Json& element) {
        if (!options.include_lines || box == nullptr || box->style_ptr == nullptr) {
            return;
        }
        if (!is_text_owner(&node)) {
            return;
        }
        if (box->style().writing_mode != zui::layout::WritingMode::HorizontalTb) {
            return;
        }
        // A block that owns its text keeps the line boxes on itself. A flex or grid container keeps
        // them one level down instead: its text is wrapped in an anonymous item (CSS Flexbox
        // section 4), and the anonymous box has no element of its own, so the browser reports those
        // lines as the element's. Follow the anonymous (and inline) boxes to find them.
        const zui::layout::Box* owner = box;
        const zui::layout::LayoutResult* result =
            box->cache != nullptr ? box->cache->pinned_final() : nullptr;
        if (result == nullptr || result->lines == nullptr || result->lines->lines.empty()) {
            for (const zui::layout::Box* child : box->children) {
                if (child == nullptr || child->cache == nullptr) {
                    continue;
                }
                const zui::layout::LayoutResult* child_result = child->cache->pinned_final();
                if (child_result == nullptr || child_result->lines == nullptr ||
                    child_result->lines->lines.empty()) {
                    continue;
                }
                owner = child;
                result = child_result;
                break;
            }
        }
        if (result == nullptr || result->lines == nullptr || result->lines->lines.empty()) {
            return;
        }
        // Chrome's Range list covers the element's own DOM text. Generated boxes
        // (::marker / ::before / ::after) sit in the same IFC but must not create extra line
        // entries, shift first_line_offset, or contribute to breaks / advances.
        const auto run_is_generated = [](const zui::layout::ShapedRun& run) {
            return run.style_box != nullptr &&
                   run.style_box->generated != zui::html::PseudoElement::Count;
        };
        const auto line_has_own_text = [&](const zui::layout::LineBox& line) {
            for (const zui::layout::ShapedRun& run : line.runs) {
                if (!run.text.empty() && !run_is_generated(run)) {
                    return true;
                }
            }
            return false;
        };
        bool painted_text = false;
        for (const zui::layout::LineBox& line : result->lines->lines) {
            if (line_has_own_text(line)) {
                painted_text = true;
                break;
            }
        }
        // An empty input still has a strut line. Chrome's Range list is empty when the
        // control has no text, so the trace omits lines too.
        if (!painted_text) {
            return;
        }
        // A box the layout split across fragmentainers reports each line where *its* fragment put
        // it, not where the unsplit strip would have: a browser's Range rects follow the
        // fragments, so the second column's lines start at the second column's top.
        const auto line_top = [&owner](const zui::layout::LineBox& line, float glyph_top) {
            // `strip` is the line's place in the content flow. The fragment's block offset uses
            // that same origin. World coordinates start at the border edge, so the border is added
            // when the line is turned into a page position.
            const float strip = owner->geo.padding.top + line.block_offset;
            const float border_top = owner->geo.border.top;
            if (!owner->geo.fragments.empty()) {
                const zui::layout::FragmentBox* holder = nullptr;
                for (const zui::layout::FragmentBox& fragment : owner->geo.fragments) {
                    // The last fragment that starts at or above the line holds it: a line that
                    // begins exactly where a fragment does belongs to that fragment, not to the one
                    // that ends there.
                    if (strip + 0.5f >= fragment.block_offset) {
                        holder = &fragment;
                    }
                }
                if (holder != nullptr) {
                    return holder->world_y + border_top + (strip - holder->block_offset) +
                           glyph_top;
                }
            }
            return owner->geo.world_y + border_top + strip + glyph_top;
        };
        Json tops = Json::array();
        Json heights = Json::array();
        Json breaks = Json::array();
        Json advances = Json::array();
        Json baselines = Json::array();
        bool baseline_defined = true;
        // The browser numbers every line start in the element's own text, collapsed whitespace
        // included; `LineBox::source_chars` is what the line breaker counted while the line was
        // open (generated markers/pseudos contribute 0 after the layout filter), so it is the
        // number to accumulate here (LAYOUT section 5).
        std::size_t painted = 0;
        std::size_t kept_lines = 0;
        for (const zui::layout::LineBox& line : result->lines->lines) {
            if (!line_has_own_text(line)) {
                continue;
            }
            ++kept_lines;
            // The browser's counterpart is a Range rect: a *text fragment*, i.e. the content area
            // of the glyphs. Prefer the first own-text glyph so a leading ::marker run does not
            // shift the reported top.
            float glyph_top = 0.f;
            bool have_glyph = false;
            for (const zui::layout::ShapedRun& run : line.runs) {
                if (run_is_generated(run) || run.glyphs.empty()) {
                    continue;
                }
                glyph_top = run.glyphs.front().block_offset;
                have_glyph = true;
                break;
            }
            if (!have_glyph) {
                for (const zui::layout::ShapedRun& run : line.runs) {
                    if (!run.glyphs.empty()) {
                        glyph_top = run.glyphs.front().block_offset;
                        have_glyph = true;
                        break;
                    }
                }
            }
            const float top = quantize(line_top(line, have_glyph ? glyph_top : 0.f));
            const float height = quantize(line.block_size);
            if (line.break_rect) {
                tops.push_back(top);
                heights.push_back(height);
                if (options.include_breaks) {
                    breaks.push_back(painted);
                }
                if (options.include_advances) {
                    advances.push_back(Json::array());
                }
                if (std::isnan(line.baseline)) {
                    baseline_defined = false;
                    baselines.push_back(nullptr);
                } else {
                    baselines.push_back(quantize(line.baseline));
                }
            }
            tops.push_back(top);
            heights.push_back(height);
            if (options.include_breaks) {
                breaks.push_back(painted);
            }
            if (options.include_advances) {
                Json line_advances = Json::array();
                // Viewport x of the line start. The caret fraction is what the browser rect snaps.
                float pen = element["border_box"][0].get<float>() + box->geo.border.left +
                            box->geo.padding.left + line.inline_start;
                for (std::size_t r = 0; r < line.runs.size(); ++r) {
                    const zui::layout::ShapedRun& run = line.runs[r];
                    if (run_is_generated(run)) {
                        continue;
                    }
                    const bool is_last_run = r + 1 == line.runs.size();
                    for (std::size_t i = 0; i < run.glyphs.size(); ++i) {
                        const zui::layout::GlyphInline& glyph = run.glyphs[i];
                        // A hung trailing space has a zero-width rect in the browser. The layout
                        // already excludes it from the line's `hanging_width`.
                        const bool hung =
                            glyph.collapsible && is_last_run && i + 1 == run.glyphs.size();
                        if (hung) {
                            line_advances.push_back(0);
                            continue;
                        }
                        const float start = pen;
                        pen += glyph.advance;
                        line_advances.push_back(caret_span(start, pen));
                    }
                }
                advances.push_back(std::move(line_advances));
            }
            if (std::isnan(line.baseline)) {
                baseline_defined = false;
                baselines.push_back(nullptr);
            } else {
                baselines.push_back(quantize(line.baseline));
            }
            painted += line.source_chars;
        }
        // Chrome builds the line list from a Range that spans every direct text node.
        // An element sitting between two of them — the empty <select> between "相册"
        // and the newline after it — is another client rect, even though it is not a
        // line box of the text. The break offset stays 0: no source character lands
        // on that rect.
        int first_text = -1;
        int last_text = -1;
        const int child_count = static_cast<int>(node.children.size());
        for (int i = 0; i < child_count; ++i) {
            if (node.children[static_cast<std::size_t>(i)]->tag == "#text") {
                if (first_text < 0) {
                    first_text = i;
                }
                last_text = i;
            }
        }
        if (first_text >= 0 && (last_text > first_text || node.trailing_whitespace)) {
            const int range_end = last_text > first_text ? last_text : child_count;
            for (int i = first_text + 1; i < range_end; ++i) {
                const zui::html::Node* child = node.children[static_cast<std::size_t>(i)].get();
                if (child == nullptr || child->tag == "#text" || child->tag == "template") {
                    continue;
                }
                const auto child_box = boxes.find(child);
                if (child_box == boxes.end() || child_box->second == nullptr) {
                    continue;
                }
                const zui::layout::Geometry& geo = child_box->second->geo;
                const float top = quantize(geo.fragments.empty() && !geo.union_across ? geo.aabb_y
                                                                                      : geo.union_y);
                const float height =
                    quantize(geo.fragments.empty() && !geo.union_across ? geo.aabb_h : geo.union_h);
                if (!tops.empty() && tops.back().get<float>() == top) {
                    continue;
                }
                tops.push_back(top);
                heights.push_back(height);
                if (options.include_breaks) {
                    breaks.push_back(0);
                }
                if (options.include_advances) {
                    advances.push_back(Json::array());
                }
                // The element's rect is not a text line. Baseline numbers are not compared;
                // `baseline_defined` stays whatever the text lines already decided.
                baselines.push_back(0);
            }
        }
        const float border_top =
            element["border_box"][1].get<float>() + box->geo.border.top + box->geo.padding.top;
        const float first_top = tops.empty() ? border_top : tops[0].get<float>();
        Json lines = Json::object();
        lines["count"] = tops.size();
        lines["line_count"] = tops.size();
        lines["tops"] = std::move(tops);
        lines["heights"] = std::move(heights);
        lines["first_line_offset"] = quantize(first_top - border_top);
        lines["baselines"] = std::move(baselines);
        lines["baseline_defined"] = baseline_defined;
        if (!options.include_breaks) {
            breaks = Json::array();
        }
        if (!options.include_advances) {
            advances = Json::array();
        }
        lines["breaks"] = breaks;
        lines["line_breaks"] = breaks;
        lines["advances"] = std::move(advances);
        lines["cluster_advances"] = lines["advances"];
        element["lines"] = std::move(lines);
        ++text_owners;
        line_boxes += kept_lines;
    }

    void walk(const zui::html::Node& node, const std::string& path) {
        paths[&node] = path;
        Json element = Json::object();
        element["path"] = path;
        element["tag"] = node.tag;
        element["id"] = node.id;
        element["classes"] = node.class_name;

        const auto found = boxes.find(&node);
        const zui::layout::Box* box = found != boxes.end() ? found->second : nullptr;
        element["has_box"] = box != nullptr;
        if (box != nullptr) {
            // An element the layout split across fragmentainers reports the union of its fragments,
            // which is what a browser's `getBoundingClientRect` returns for it; a box in one piece
            // reports its own border box.
            if (!box->geo.fragments.empty() || box->geo.union_across) {
                element["border_box"] = rect_array(box->geo.union_x, box->geo.union_y,
                                                   box->geo.union_w, box->geo.union_h);
            } else {
                // Transformed getBoundingClientRect is the AABB of the border box after
                // `transform` (paint_map fills aabb_*). Untransformed boxes keep aabb == world.
                float origin_x = box->geo.aabb_x;
                float origin_y = box->geo.aabb_y;
                float box_w = box->geo.aabb_w;
                float box_h = box->geo.aabb_h;
                if (box == layout.root()) {
                    origin_x -= node.scroll_x;
                    origin_y -= node.scroll_y;
                }
                element["border_box"] = rect_array(origin_x, origin_y, box_w, box_h);
            }
            // Scrollable extent in the browser's sense. Two mapping rules (TESTPLAN section 9):
            //
            // * `scrollWidth`/`scrollHeight` are the padding box grown to the scrollable
            //   overflow. The layout fills that as `geo.scroll_*` (LAYOUT section 4); the
            //   content box is smaller when a scrollbar gutter was reserved, so it is not used
            //   as the floor;
            // * the root element's scroll container is the viewport, so its extent is at least
            //   the viewport: `documentElement.scrollHeight` is 600 for a 200px document.
            float scroll_w = box->geo.scroll_width;
            float scroll_h = box->geo.scroll_height;
            if (box == layout.root()) {
                // The root element's scroll container is the viewport. A vertical bar takes
                // its thickness off the client width. Content that only filled the unreduced
                // viewport does not then overflow horizontally, so scrollWidth becomes that
                // client width (measured: key-scroll, 785 not 800). A page that really is
                // wider than the viewport keeps the horizontal bar and loses 15px of height
                // (measured: 920 wide, documentElement.scrollHeight 585).
                const float bar = layout.scrollbar_thickness();
                const bool tall = scroll_h > viewport_h + 0.5f;
                const bool wide = scroll_w > viewport_w + 0.5f;
                if (tall && !wide) {
                    scroll_w = std::max(0.0f, viewport_w - bar);
                }
                const bool wide_now = scroll_w > viewport_w + 0.5f;
                const float view_floor_h = std::max(0.0f, viewport_h - (wide_now ? bar : 0.0f));
                const float view_floor_w = std::max(0.0f, viewport_w - (tall ? bar : 0.0f));
                scroll_h = std::max(scroll_h, view_floor_h);
                scroll_w = std::max(scroll_w, view_floor_w);
            }
            element["scroll_size"] = pair_array(scroll_w, scroll_h);
            element["scroll_offset"] = pair_array(node.scroll_x, node.scroll_y);

            const float origin_x = element["border_box"][0].get<float>();
            const float origin_y = element["border_box"][1].get<float>();
            float client_w = std::max(0.0f, box->geo.width - box->geo.border.horizontal());
            float client_h = std::max(0.0f, box->geo.height - box->geo.border.vertical());
            if (box == layout.root()) {
                zui::input::document_client_size(
                    viewport_w, viewport_h, scroll_w, scroll_h, layout.scrollbar_thickness(),
                    box->geo.border.horizontal(), box->geo.border.vertical(), client_w, client_h);
            } else if (box->style_ptr != nullptr) {
                const zui::layout::ScrollbarV vertical =
                    zui::layout::vertical_scrollbar(*box, layout.scrollbar_thickness());
                const zui::layout::ScrollbarH horizontal =
                    zui::layout::horizontal_scrollbar(*box, layout.scrollbar_thickness());
                if (vertical.visible) {
                    client_w = std::max(0.0f, client_w - vertical.track_w);
                }
                if (horizontal.visible) {
                    client_h = std::max(0.0f, client_h - horizontal.track_h);
                }
                // `scrollbar-gutter: stable` shrinks clientWidth even when the bar is not
                // painted. A visible bar already removed one thickness; `both-edges` still
                // removes the opposite gutter.
                const float thickness = zui::layout::scrollbar_used_thickness(
                    box->style(), layout.scrollbar_thickness());
                const bool horizontal_wm = zui::layout::is_horizontal_wm(box->style().writing_mode);
                const zui::layout::OverflowMode block_overflow =
                    horizontal_wm ? box->style().overflow_y : box->style().overflow_x;
                const bool block_bar_visible = horizontal_wm ? vertical.visible : horizontal.visible;
                if (block_overflow != zui::layout::OverflowMode::Visible && thickness > 0.f &&
                    zui::layout::scrollbar_gutter_forces_reserve(box->style())) {
                    float extra = box->style().scrollbar_gutter == zui::layout::ScrollbarGutter::BothEdges
                                      ? thickness * 2.f
                                      : thickness;
                    if (block_bar_visible) {
                        extra = std::max(0.f, extra - thickness);
                    }
                    if (horizontal_wm) {
                        client_w = std::max(0.0f, client_w - extra);
                    } else {
                        client_h = std::max(0.0f, client_h - extra);
                    }
                }
            }
            // CSSOM: clientWidth, clientHeight, scrollWidth and scrollHeight are 0 for a
            // non-replaced inline box. The border box above is still the line's rect.
            // `display:ruby` is laid out as an atomic unit but Chrome still reports client/scroll
            // 0 (same as a pure inline), so treat it like non-replaced inline here.
            //
            // Pinned by case `ruby-layout` (engine-recorded). This is a property of the box kind,
            // not of the exporter: if a later cut makes the engine report the ruby box's own
            // client/scroll rectangle, drop this and re-record the case from Chrome.
            //
            // An abspos, fixed, or floated box is blockified. Chrome reports its client box and
            // the scrollable overflow of the text it laid out (contacts span.sr is 1x1 with a
            // 30x23 scroll). An in-flow ruby stays zero.
            const std::string* display_for_client =
                box->node != nullptr ? box->node->style("display") : nullptr;
            const bool ruby_container =
                display_for_client != nullptr && *display_for_client == "ruby";
            const bool inline_non_replaced =
                box->style_ptr != nullptr &&
                box->style().outer == zui::layout::DisplayOuter::Inline && !box->is_replaced &&
                (!box->atomic_inline || ruby_container) && !box->style().position_absolute &&
                !box->style().position_fixed && !box->style().floating;
            if (inline_non_replaced) {
                client_w = 0.f;
                client_h = 0.f;
                scroll_w = 0.f;
                scroll_h = 0.f;
                element["scroll_size"] = pair_array(0, 0);
            }
            // Collapsed table-column / column-group: border box is 0×0, but Chrome keeps
            // clientHeight and scrollHeight at the track's block size.
            if (box->style_ptr != nullptr && box->style().visibility_collapse &&
                box->style().table_column) {
                client_w = 0.f;
                client_h = box->geo.content_height;
                scroll_w = 0.f;
                scroll_h = box->geo.content_height;
                element["scroll_size"] = pair_array(scroll_w, scroll_h);
            }
            // Chrome's clientWidth and scrollWidth for a checkbox or radio equal the
            // border box. A CSS border does not shrink those CSSOM sizes.
            float client_x = origin_x + box->geo.border.left;
            float client_y = origin_y + box->geo.border.top;
            if (zui::html::is_checkbox(&node) || zui::html::is_radio(&node)) {
                scroll_w = box->geo.width;
                scroll_h = box->geo.height;
                client_w = box->geo.width;
                client_h = box->geo.height;
                client_x = origin_x;
                client_y = origin_y;
                element["scroll_size"] = pair_array(scroll_w, scroll_h);
            }
            // `border-collapse: collapse` keeps a collapsed line's half outside the CSOM box for
            // every table part but the cell: Chrome reports the table's, row's and row group's
            // clientWidth/clientHeight as their border box (probe `build/collapseprobe.py`, where
            // the table's clientWidth is its border box while a cell's is its box minus its two
            // halves). The engine keeps the halves in `geo.border` for painting, so the reported
            // box is widened here - the same kind of parity stand-in as the checkbox rule above.
            const bool collapsed_part = box->style_ptr != nullptr && box->style().border_collapse &&
                                        box->style().table_part != zui::layout::TablePart::None &&
                                        box->style().table_part != zui::layout::TablePart::Cell;
            if (collapsed_part) {
                client_w = box->geo.width;
                client_h = box->geo.height;
                client_x = origin_x;
                client_y = origin_y;
                scroll_w = std::max(scroll_w, client_w);
                scroll_h = std::max(scroll_h, client_h);
                element["scroll_size"] = pair_array(scroll_w, scroll_h);
            }
            element["client_box"] = rect_array(client_x, client_y, client_w, client_h);
            Json client_rects = Json::array();
            if (!box->geo.fragments.empty()) {
                for (const zui::layout::FragmentBox& fragment : box->geo.fragments) {
                    client_rects.push_back(rect_array(fragment.world_x, fragment.world_y,
                                                      fragment.width, fragment.height));
                }
            } else {
                client_rects.push_back(element["border_box"]);
            }
            element["client_rects"] = std::move(client_rects);
            Json scroll_rects = Json::array();
            if (scroll_w > client_w + 0.5f || scroll_h > client_h + 0.5f) {
                scroll_rects.push_back(rect_array(0, 0, scroll_w, scroll_h));
            }
            element["scroll_rects"] = std::move(scroll_rects);
            const bool fragmented = !box->geo.fragments.empty() || box->geo.union_across;
            element["fragmented"] = fragmented;
            const bool out_of_flow = box->style_ptr != nullptr && (box->style().position_absolute ||
                                                                   box->style().position_fixed);
            element["out_of_flow"] = out_of_flow;
            if (out_of_flow) {
                const bool fixed = box->style().position_fixed;
                const zui::html::Node* cb_node = nullptr;
                for (const zui::html::Node* ancestor = node.parent; ancestor != nullptr;
                     ancestor = ancestor->parent) {
                    if (makes_containing_block(*ancestor, fixed)) {
                        cb_node = ancestor;
                        break;
                    }
                }
                // No ancestor qualifies: Chrome writes null (the initial containing block).
                // The parent element qualifying is omitted, which the differ reads as mode parent.
                if (cb_node == nullptr) {
                    element["containing_block"] = nullptr;
                } else if (cb_node != node.parent) {
                    const auto found_cb = paths.find(cb_node);
                    element["containing_block"] =
                        found_cb != paths.end() ? Json(found_cb->second) : Json(nullptr);
                }
            }
        } else {
            element["border_box"] = rect_array(0, 0, 0, 0);
            element["client_box"] = rect_array(0, 0, 0, 0);
            element["client_rects"] = Json::array();
            element["scroll_rects"] = Json::array();
            element["scroll_size"] = pair_array(0, 0);
            element["scroll_offset"] = pair_array(0, 0);
            element["fragmented"] = false;
            const std::string* display = node.style("display");
            const bool own_none = display != nullptr && *display == "none";
            const bool own_contents = display != nullptr && *display == "contents";
            // `link` does not pick up `display: none` from the UA sheet, so it must not be
            // reported as a box just because it sits under head. The other names are the
            // metadata elements Chrome also leaves without a box.
            const bool metadata = node.tag == "link" || node.tag == "meta" || node.tag == "title" ||
                                  node.tag == "script" || node.tag == "style" ||
                                  node.tag == "head" || node.tag == "template";
            // Chrome reports the element's own display: an element whose display is not none or
            // contents has `has_box` even when nothing was laid out for it - a descendant of
            // display:none, or a subtree this engine's stand-ins skipped (a closed select's list).
            // Its rect stays zero and out-of-flow follows its position, so a box the browser does
            // have and the engine does not is still reported as a geometry difference.
            if (!own_none && !own_contents && !metadata) {
                element["has_box"] = true;
                const std::string* position = node.style("position");
                element["out_of_flow"] =
                    position != nullptr && (*position == "absolute" || *position == "fixed");
            } else {
                element["out_of_flow"] = false;
            }
        }

        const std::string* transform = node.style("transform");
        // Chrome answers the *used* transform: an element with no box (`display: none`, which
        // covers `[hidden]`, or `contents`) reports `none` however the declaration reads
        // (measured: `build/transform_probe.html`).
        const bool boxed = element.contains("has_box") && element["has_box"].get<bool>();
        element["transformed"] = boxed && transform != nullptr && *transform != "none";

        // Used corner radii in px, CSS order x then y: tl, tl_y, tr, tr_y, br, br_y, bl, bl_y.
        // Not an L2 field. The pixel diff uses it to keep rounded-corner coverage out of the
        // edge and component totals. A square box omits the key.
        if (box != nullptr && box->style_ptr != nullptr && box->style().border_radius > 0.f) {
            const zui::paint::CornerRadii radii = zui::paint::painter_detail::resolve_border_radii(
                box->style(), box->geo.width, box->geo.height);
            if (radii.max_component() > 0.f) {
                Json corners = Json::array();
                corners.push_back(radii.tl);
                corners.push_back(radii.tl_y);
                corners.push_back(radii.tr);
                corners.push_back(radii.tr_y);
                corners.push_back(radii.br);
                corners.push_back(radii.br_y);
                corners.push_back(radii.bl);
                corners.push_back(radii.bl_y);
                element["corner_radii"] = std::move(corners);
            }
        }

        std::vector<std::pair<std::string, std::string>> entries;
        element["style"] = style_object(node, box, entries);
        element["style_digest"] = style_digest(entries);

        lines_object(node, box, element);
        elements.push_back(std::move(element));

        // Template contents live in the fragment, not as document children. Chrome's snapshot
        // does not list them, and layout does not box them (the template is display:none).
        if (node.tag != "template") {
            const std::string* display = node.style("display");
            const bool own_none = display != nullptr && *display == "none";
            std::size_t index = 0;
            for (const auto& child : node.children) {
                if (child->tag.empty() || child->tag == "#text") {
                    continue;
                }
                walk(*child, path + "/" + std::to_string(index));
                ++index;
            }
        }
    }
};

}  // namespace

float quantize(float value) {
    return static_cast<float>(std::round(static_cast<double>(value) * 64.0) / 64.0);
}

std::string normalize_style_value(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    bool pending_space = false;
    for (char c : value) {
        if (is_space_char(c)) {
            pending_space = !out.empty();
            continue;
        }
        if (pending_space) {
            out.push_back(' ');
            pending_space = false;
        }
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    // Rule 2 from props.json: a zero length collapses to `0`, so `0px` / `0.0px` / `-0px` agree.
    if (out.size() > 2 && out.compare(out.size() - 2, 2, "px") == 0) {
        const std::string number = out.substr(0, out.size() - 2);
        char* end = nullptr;
        const double parsed = std::strtod(number.c_str(), &end);
        if (end != nullptr && *end == '\0' && parsed == 0.0) {
            return "0";
        }
    }
    return out;
}

std::string style_digest(const std::vector<std::pair<std::string, std::string>>& entries) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const auto& entry : entries) {
        for (char c : entry.first) {
            hash = (hash ^ static_cast<unsigned char>(c)) * 1099511628211ull;
        }
        hash = (hash ^ '=') * 1099511628211ull;
        for (char c : entry.second) {
            hash = (hash ^ static_cast<unsigned char>(c)) * 1099511628211ull;
        }
        hash = (hash ^ ';') * 1099511628211ull;
    }
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(hash));
    return buf;
}

TraceResult export_trace(zui::html::Document& doc, zui::layout::LayoutEngine& layout,
                         float viewport_w, float viewport_h, const TraceOptions& options) {
    TraceResult out;
    Json trace = Json::object();
    trace["schema"] = "zui.difftest.trace/2";
    trace["engine"] = options.engine;
    trace["engine_version"] = options.engine_version;
    trace["viewport"] = pair_array(viewport_w, viewport_h);

    // LAYOUT.md section 16: this export reads *layout* geometry - border boxes and the two overflow
    // rectangles - not what the frame decided to paint, so it maps every box. A frame culls the
    // subtrees that cannot reach the viewport (section 9 step B) and clears their geometry, which
    // would otherwise report a page whose off-screen boxes have moved to the origin.
    if (layout.root() != nullptr) {
        zui::layout::map_paint_every_box(layout.paint_map_session(), *layout.root());
    }

    if (doc.root != nullptr) {
        trace["scroll"] = pair_array(doc.root->scroll_x, doc.root->scroll_y);
    } else {
        trace["scroll"] = pair_array(0, 0);
    }

    Collector collector(doc, layout, options);
    collector.viewport_w = viewport_w;
    collector.viewport_h = viewport_h;
    collector.index_boxes(layout.root());
    if (doc.root != nullptr) {
        collector.walk(*doc.root, "0");
    }
    out.elements = collector.elements.size();
    out.text_owners = collector.text_owners;
    out.line_boxes = collector.line_boxes;
    trace["elements"] = std::move(collector.elements);
    out.trace = std::move(trace);
    return out;
}

Json export_diagnostics(zui::html::Document& doc, zui::layout::LayoutEngine& layout,
                        const std::string& step,
                        const std::vector<NonConvergenceNote>& extra_non_converged) {
    Json out = Json::object();
    out["schema"] = "zui.difftest.diagnostics/1";
    out["step_name"] = step;

    const zui::layout::LayoutContext::FrameStats& stats = layout.frame_stats();
    Json frame = Json::object();
    frame["dirty_marks"] = stats.dirty_marks;
    frame["laid_out"] = stats.laid_out;
    frame["cache_hits"] = stats.cache_hits;
    frame["evicted_measure_slots"] = stats.evicted_measure_slots;
    frame["container_requery_rounds"] = stats.container_requery_rounds;
    frame["interned_styles"] = stats.interned_styles;
    out["frame"] = std::move(frame);

    std::unordered_map<const zui::html::Node*, const zui::layout::Box*> boxes;
    index_element_boxes(layout.root(), boxes);
    const std::vector<zui::layout::LayoutContext::NonConvergence>& reasons =
        layout.non_convergence();

    const auto extra_reason =
        [&extra_non_converged](const zui::layout::Box* box) -> const std::string* {
        for (const NonConvergenceNote& note : extra_non_converged) {
            if (note.box == box) {
                return &note.reason;
            }
        }
        return nullptr;
    };
    Json non_converged = Json::array();
    std::size_t element_count = 0;
    const auto reason_for = [&reasons](const zui::layout::Box* box) -> std::string {
        for (const zui::layout::LayoutContext::NonConvergence& entry : reasons) {
            if (entry.box == box) {
                return entry.reason;
            }
        }
        return "converged == false";
    };
    const auto visit = [&](auto&& self, const zui::html::Node& node,
                           const std::string& path) -> void {
        ++element_count;
        const auto found = boxes.find(&node);
        const zui::layout::Box* box = found != boxes.end() ? found->second : nullptr;
        if (box != nullptr && box->cache != nullptr) {
            const zui::layout::LayoutResult* pinned = box->cache->pinned_final();
            const std::string* carried = extra_reason(box);
            if ((pinned != nullptr && !pinned->converged) || carried != nullptr) {
                Json entry = Json::object();
                entry["path"] = path;
                entry["tag"] = node.tag;
                entry["id"] = node.id;
                entry["classes"] = node.class_name;
                entry["reason"] = carried != nullptr ? *carried : reason_for(box);
                non_converged.push_back(std::move(entry));
            }
        }
        std::size_t index = 0;
        for (const auto& child : node.children) {
            if (child->tag.empty() || child->tag == "#text") {
                continue;
            }
            self(self, *child, path + "/" + std::to_string(index));
            ++index;
        }
    };
    if (doc.root != nullptr) {
        visit(visit, *doc.root, "0");
    }

    out["elements"] = element_count;
    out["converged"] = non_converged.empty();
    out["non_converged"] = std::move(non_converged);
    return out;
}

}  // namespace zdt
