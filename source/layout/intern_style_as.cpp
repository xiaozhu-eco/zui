#include "layout/intern_style.h"
#include "layout/intern_style_internal.h"
#include "layout/intern_detail.h"
#include "layout/style_literal.h"
#include "layout/intern_grid.h"
#include "layout/style_table.h"
#include "layout/cache/key_policy.h"
#include "css/values.h"
#include "text/font_chain.h"
#include "log/log.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <unordered_map>
#include <vector>

namespace zui {
namespace layout {

using namespace intern_detail;

namespace {

/// Everything `intern_style_as` reads besides the computed style map itself.
///
/// The map is the identity, not a copy: the cascade merges nodes that no selector can tell apart
/// into one shared `Computed` (`css::Engine`'s share cache), so two rows of a list hold the *same*
/// `StyleMap` pointer and interning the second row is the same work as the first. `style` is held,
/// not borrowed, so the address cannot be reused by a later map while the entry lives.
///
/// The rest is what the map does not carry: the resolved language (an HTML attribute, inherited
/// down the tree), the `colspan` / `rowspan` / `span` attributes the table code reads, the
/// viewport, and the two font sizes a length can resolve against.
struct InternMemoKey {
    std::shared_ptr<const StyleMap> style;
    std::string lang;
    int colspan = 1;
    int rowspan = 1;
    int span = 1;
    BoxRole role = BoxRole::Element;
    DisplayOuter outer = DisplayOuter::Block;
    DisplayInner inner = DisplayInner::Flow;
    TablePart part = TablePart::None;
    float viewport_w = 0.f;
    float viewport_h = 0.f;
    float font_px = 16.f;
    float root_px = 16.f;

    bool operator==(const InternMemoKey& other) const {
        return style == other.style && lang == other.lang && colspan == other.colspan &&
               rowspan == other.rowspan && span == other.span && role == other.role &&
               outer == other.outer && inner == other.inner && part == other.part &&
               viewport_w == other.viewport_w && viewport_h == other.viewport_h &&
               font_px == other.font_px && root_px == other.root_px;
    }
};

struct InternMemoKeyHash {
    std::size_t operator()(const InternMemoKey& key) const {
        std::size_t hash = std::hash<const void*>{}(key.style.get());
        const auto mix = [&hash](std::size_t value) {
            hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
        };
        mix(std::hash<std::string>{}(key.lang));
        mix(static_cast<std::size_t>(key.colspan));
        mix(static_cast<std::size_t>(key.rowspan));
        mix(static_cast<std::size_t>(key.span));
        mix(static_cast<std::size_t>(key.role));
        mix(static_cast<std::size_t>(key.outer));
        mix(static_cast<std::size_t>(key.inner));
        mix(static_cast<std::size_t>(key.part));
        mix(std::hash<float>{}(key.viewport_w));
        mix(std::hash<float>{}(key.viewport_h));
        mix(std::hash<float>{}(key.font_px));
        mix(std::hash<float>{}(key.root_px));
        return hash;
    }
};

/// A memoized intern: the style, and whether that intern resolved a viewport unit (the hit path
/// does not re-read any text, so `Box::depends_on_viewport` has to come from here).
///
/// The style is held weakly on purpose. `StyleTable` interns by content and hands out the entries
/// its callers keep alive - `LayoutBox.TreeAndIntern` asserts the table drops a style once the last
/// holder releases it, and an animation's style churn has to be reclaimed. A strong reference here
/// would make this memo, not the boxes, the owner.
struct InternMemoValue {
    std::weak_ptr<const LayoutStyle> style;
    bool viewport_unit = false;
};

/// Bounded because a key holds its `StyleMap` alive: the entries are worth keeping for the layout
/// passes of one page, not for every variant a long session ever interned.
constexpr std::size_t kInternMemoLimit = 8192;

std::unordered_map<InternMemoKey, InternMemoValue, InternMemoKeyHash>& intern_memo() {
    // Deliberately leaked. The entries hold `shared_ptr`s to a `StyleMap` and to a `LayoutStyle`,
    // and releasing those from a static destructor - after the rest of the process has been torn
    // down - crashed the test binaries (0xC0000005 in the exit path, with every assertion already
    // passed). A process-wide cache is worth keeping until the process ends anyway.
    static auto* memo = new std::unordered_map<InternMemoKey, InternMemoValue, InternMemoKeyHash>();
    return *memo;
}

/// The key for this intern, or false when the node cannot be memoized. A node the cascade never
/// gave a computed style has nothing to key on, and interning it is rare enough not to matter.
bool intern_memo_key(const html::Node* node, BoxRole role, DisplayOuter outer, DisplayInner inner,
                     TablePart part, Viewport viewport, InternMemoKey& key) {
    if (node == nullptr || node->computed == nullptr) {
        return false;
    }
    key.style = node->computed;
    key.lang = lang_of(node);
    if (node->tag == "td" || node->tag == "th") {
        if (const auto it = node->attrs.find("colspan"); it != node->attrs.end()) {
            key.colspan = std::max(1, std::atoi(it->second.c_str()));
        }
        if (const auto it = node->attrs.find("rowspan"); it != node->attrs.end()) {
            key.rowspan = std::max(1, std::atoi(it->second.c_str()));
        }
    }
    if (part == TablePart::Column || part == TablePart::ColumnGroup) {
        if (const auto it = node->attrs.find("span"); it != node->attrs.end()) {
            key.span = std::max(1, std::atoi(it->second.c_str()));
        }
    }
    key.role = role;
    key.outer = outer;
    key.inner = inner;
    key.part = part;
    key.viewport_w = viewport.width;
    key.viewport_h = viewport.height;
    key.font_px = font_size_px(node);
    key.root_px = root_font_size_px(node);
    return true;
}

}  // namespace

struct InternPhaseDiag {
    double key_ms = 0;
    double find_ms = 0;
    double font_ms = 0;
    double body_ms = 0;
    double table_ms = 0;
    int hits = 0;
    int misses = 0;
};
InternPhaseDiag g_intern_phase;

void reset_intern_phase() { g_intern_phase = {}; }

void trace_intern_phase() {
    ZUI_TRACE("[intern-phase] key={:.1f} find={:.1f} font={:.1f} body={:.1f} table={:.1f}"
              " hits={} misses={}",
              g_intern_phase.key_ms, g_intern_phase.find_ms, g_intern_phase.font_ms,
              g_intern_phase.body_ms, g_intern_phase.table_ms, g_intern_phase.hits,
              g_intern_phase.misses);
}

namespace {

struct InternFallback {
    InternSession* previous = nullptr;
    bool owned = false;
    /// Outlives `intern_style`. `intern_into` reads `intern_used_viewport_unit` after
    /// `intern_style` returns, so a stack session would already be gone.
    static InternSession& scratch() {
        static InternSession session;
        return session;
    }
    InternFallback() {
        previous = active_intern_slot();
        owned = previous == nullptr;
        if (owned) {
            active_intern_slot() = &scratch();
        }
    }
    ~InternFallback() {
        if (owned) {
            active_intern_slot() = &scratch();
        }
    }
};

}  // namespace

std::shared_ptr<const LayoutStyle> intern_style_as(const html::Node* node, BoxRole role,
                                                   DisplayOuter outer, DisplayInner inner,
                                                   TablePart part, Viewport viewport) {
    InternFallback fallback;
    g_length_viewport = viewport;
    g_intern_used_viewport_unit = false;
    InternMemoKey memo_key;
    const bool memoizable = intern_memo_key(node, role, outer, inner, part, viewport, memo_key);
    if (memoizable) {
        auto& memo = intern_memo();
        const auto hit = memo.find(memo_key);
        if (hit != memo.end()) {
            if (std::shared_ptr<const LayoutStyle> held = hit->second.style.lock()) {
                if (hit->second.viewport_unit) {
                    note_viewport_unit_used();
                }
                ++g_intern_phase.hits;
                return held;
            }
            // The style this entry named is gone (the intern table dropped it when its last holder
            // released it). The key is still good, so the entry is only stale; re-intern below.
            memo.erase(hit);
        }
    }
    // One pass over the computed map. The longhand reads below index it without a call.
    style_literal_detail::SlotGuard slot_guard(node);
    // Only a miss builds a `LayoutStyle`, and only that path resolves lengths, so the font bases
    // are computed here rather than before the lookup: on a hit the value computed up there was
    // thrown away, and the chain it built was the whole remaining cost of the call.
    // Opening a FreeType face is the cold-intern cost. Skip it when no computed value uses
    // ch/ex/lh; the default bases are the "not measured" sentinels resolve_length already ignores.
    if (node != nullptr && node->computed != nullptr && style_needs_font_rel(*node->computed)) {
        g_font_rel = font_rel_of(node);
    } else {
        g_font_rel = {};
    }
    LayoutStyle style;
    style.outer = outer;
    style.inner = inner;
    style.table_part = part;
    // Anonymous wrappers for a list item's marker reuse that element's node; they still need the
    // list-style-position bit so outside markers are not mistaken for in-flow ones.
    if (node != nullptr) {
        if (const std::string* list_pos = style_literal(node, "list-style-position")) {
            style.list_style_outside = *list_pos != "inside";
        }
    }
    if (role == BoxRole::Element && node != nullptr) {
        const std::string* pos = style_literal(node, "position");
        const std::string position = pos != nullptr ? *pos : "static";
        style.position_relative = position == "relative";
        style.position_absolute = position == "absolute";
        style.position_fixed = position == "fixed";
        style.position_sticky = position == "sticky";
        const std::string* floated = style_literal(node, "float");
        style.floating = floated != nullptr && *floated != "none";
        style.float_right = floated != nullptr && *floated == "right";
        if (const std::string* clear = style_literal(node, "clear")) {
            if (*clear == "left") {
                style.clear_side = ClearSide::Left;
            } else if (*clear == "right") {
                style.clear_side = ClearSide::Right;
            } else if (*clear == "both") {
                style.clear_side = ClearSide::Both;
            }
        }
        apply_length(style.width_auto, style.width_percent, style.width_px,
                     parse_length(node, style_literal(node, "width"), true));
        apply_length(style.height_auto, style.height_percent, style.height_px,
                     parse_length(node, style_literal(node, "height"), true));
        // `parse_length` resolves everything the style system can resolve on its own, including a
        // `calc()` in pixel terms. What it cannot resolve - a math function with a percentage in
        // it - is left to layout, and that path is the only reader of `ConstraintSpace::icb`.
        if (is_math_length(style_literal(node, "width")) || is_math_length(style_literal(node, "height"))) {
            note_icb_length();
        }
        style.width_intrinsic = intrinsic_of(style_literal(node, "width"));
        style.height_intrinsic = intrinsic_of(style_literal(node, "height"));
        apply_margin(
            style.margin_top, style.margin_top_percent, style.margin_top_auto,
            parse_length_kept(style, LengthProperty::MarginTop, node, style_literal(node, "margin-top"),
                              false));
        apply_margin(
            style.margin_right, style.margin_right_percent, style.margin_right_auto,
            parse_length_kept(style, LengthProperty::MarginRight, node,
                              style_literal(node, "margin-right"), false));
        apply_margin(
            style.margin_bottom, style.margin_bottom_percent, style.margin_bottom_auto,
            parse_length_kept(style, LengthProperty::MarginBottom, node,
                              style_literal(node, "margin-bottom"), false));
        apply_margin(
            style.margin_left, style.margin_left_percent, style.margin_left_auto,
            parse_length_kept(style, LengthProperty::MarginLeft, node, style_literal(node, "margin-left"),
                              false));
        apply_length(style.min_width_auto, style.min_width_percent, style.min_width_px,
                     parse_length_kept(style, LengthProperty::MinWidth, node,
                                       style_literal(node, "min-width"), true));
        apply_length(style.min_height_auto, style.min_height_percent, style.min_height_px,
                     parse_length_kept(style, LengthProperty::MinHeight, node,
                                       style_literal(node, "min-height"), true));
        apply_max(style.max_width_none, style.max_width_percent, style.max_width_px, style,
                  LengthProperty::MaxWidth, node, style_literal(node, "max-width"));
        apply_max(style.max_height_none, style.max_height_percent, style.max_height_px, style,
                  LengthProperty::MaxHeight, node, style_literal(node, "max-height"));
        if (const std::string* display = style_literal(node, "display")) {
            style.flow_root = *display == "flow-root";
        }
        if (const std::string* sizing = style_literal(node, "box-sizing")) {
            style.border_box = *sizing == "border-box";
        }
        read_inherited_inline(style, node);
        read_vertical_align(style, node);
        read_gutter(style, node);
        read_scrollbar_width(style, node);
        read_flex(style, node);
        read_grid(style, node);
        if (const std::string* query = style_literal(node, "container-type")) {
            if (query->find("inline-size") != std::string::npos) {
                style.container_type = ContainerType::InlineSize;
            } else if (*query == "size") {
                style.container_type = ContainerType::Size;
            }
        }
        apply_edge(
            style.padding_top, style.padding_top_percent,
            parse_length_kept(style, LengthProperty::PaddingTop, node, style_literal(node, "padding-top"),
                              false));
        apply_edge(
            style.padding_right, style.padding_right_percent,
            parse_length_kept(style, LengthProperty::PaddingRight, node,
                              style_literal(node, "padding-right"), false));
        apply_edge(
            style.padding_bottom, style.padding_bottom_percent,
            parse_length_kept(style, LengthProperty::PaddingBottom, node,
                              style_literal(node, "padding-bottom"), false));
        apply_edge(
            style.padding_left, style.padding_left_percent,
            parse_length_kept(style, LengthProperty::PaddingLeft, node,
                              style_literal(node, "padding-left"), false));
        const ParsedLength top =
            parse_length_kept(style, LengthProperty::Top, node, style_literal(node, "top"), true);
        const ParsedLength right =
            parse_length_kept(style, LengthProperty::Right, node, style_literal(node, "right"), true);
        const ParsedLength bottom =
            parse_length_kept(style, LengthProperty::Bottom, node, style_literal(node, "bottom"), true);
        const ParsedLength left =
            parse_length_kept(style, LengthProperty::Left, node, style_literal(node, "left"), true);
        style.top_auto = !top.valid || top.is_auto;
        style.top_percent = top.valid && top.is_percent && !top.is_auto;
        style.top_px = top.valid && !top.is_auto ? top.value : 0;
        style.right_auto = !right.valid || right.is_auto;
        style.right_percent = right.valid && right.is_percent && !right.is_auto;
        style.right_px = right.valid && !right.is_auto ? right.value : 0;
        style.bottom_auto = !bottom.valid || bottom.is_auto;
        style.bottom_percent = bottom.valid && bottom.is_percent && !bottom.is_auto;
        style.bottom_px = bottom.valid && !bottom.is_auto ? bottom.value : 0;
        style.left_auto = !left.valid || left.is_auto;
        style.left_percent = left.valid && left.is_percent && !left.is_auto;
        style.left_px = left.valid && !left.is_auto ? left.value : 0;
        auto border_line_style = [](const std::string* v) {
            if (v == nullptr || v->empty() || *v == "none") {
                return BorderLineStyle::None;
            }
            if (*v == "hidden") {
                return BorderLineStyle::Hidden;
            }
            if (*v == "dotted") {
                return BorderLineStyle::Dotted;
            }
            if (*v == "dashed") {
                return BorderLineStyle::Dashed;
            }
            if (*v == "solid") {
                return BorderLineStyle::Solid;
            }
            if (*v == "double") {
                return BorderLineStyle::Double;
            }
            if (*v == "groove") {
                return BorderLineStyle::Groove;
            }
            if (*v == "ridge") {
                return BorderLineStyle::Ridge;
            }
            if (*v == "inset") {
                return BorderLineStyle::Inset;
            }
            if (*v == "outset") {
                return BorderLineStyle::Outset;
            }
            return BorderLineStyle::None;
        };
        style.border_top_style = border_line_style(style_literal(node, "border-top-style"));
        style.border_right_style = border_line_style(style_literal(node, "border-right-style"));
        style.border_bottom_style = border_line_style(style_literal(node, "border-bottom-style"));
        style.border_left_style = border_line_style(style_literal(node, "border-left-style"));
        // CSS 2.1 §8.5.3: none/hidden → used border width 0 (also feeds collapse conflict).
        auto used_border_width = [&](const std::string* width, BorderLineStyle line) {
            if (line == BorderLineStyle::None || line == BorderLineStyle::Hidden) {
                return 0.f;
            }
            return px_of(node, width, 0);
        };
        style.border_top =
            used_border_width(style_literal(node, "border-top-width"), style.border_top_style);
        style.border_right =
            used_border_width(style_literal(node, "border-right-width"), style.border_right_style);
        style.border_bottom =
            used_border_width(style_literal(node, "border-bottom-width"), style.border_bottom_style);
        style.border_left =
            used_border_width(style_literal(node, "border-left-width"), style.border_left_style);
        // CSS UI 4 section 2: the outline width defaults to `medium` (3px at the initial font size),
        // but a `none` outline never paints. `outline-offset` may be negative.
        style.outline_style = [&] {
            if (node == nullptr) {
                return BorderLineStyle::None;
            }
            const std::string* v = style_literal(node, "outline-style");
            if (v == nullptr || v->empty()) {
                return BorderLineStyle::None;
            }
            if (*v == "dotted") {
                return BorderLineStyle::Dotted;
            }
            if (*v == "dashed") {
                return BorderLineStyle::Dashed;
            }
            if (*v == "solid") {
                return BorderLineStyle::Solid;
            }
            if (*v == "double") {
                return BorderLineStyle::Double;
            }
            return BorderLineStyle::None;
        }();
        if (style.outline_style != BorderLineStyle::None) {
            const std::string* width = node != nullptr ? style_literal(node, "outline-width") : nullptr;
            if (width == nullptr || width->empty() || *width == "medium") {
                style.outline_width = 3.f;
            } else if (*width == "thin") {
                style.outline_width = 1.f;
            } else if (*width == "thick") {
                style.outline_width = 5.f;
            } else {
                style.outline_width = px_of(node, style_literal(node, "outline-width"), 3.f);
            }
            style.outline_offset = px_of(node, style_literal(node, "outline-offset"), 0.f);
        }
        // CSS Backgrounds 3 section 5: each corner is `<length-percentage>{1,2}`. One value sets
        // both axes; two values are horizontal then vertical. Percentages stay unresolved until
        // paint has the border-box size.
        auto corner = [&](const std::string* raw, float& px, bool& percent, float& py, bool& percent_y) {
            px = 0;
            percent = false;
            py = 0;
            percent_y = false;
            if (raw == nullptr || *raw == "auto") {
                return;
            }
            std::vector<std::string> parts;
            std::string cur;
            for (char c : *raw) {
                if (std::isspace(static_cast<unsigned char>(c))) {
                    if (!cur.empty()) {
                        parts.push_back(cur);
                        cur.clear();
                    }
                    continue;
                }
                cur.push_back(c);
            }
            if (!cur.empty()) {
                parts.push_back(cur);
            }
            if (parts.empty()) {
                return;
            }
            const float fs = font_size_px(node);
            const float rfs = root_font_size_px(node);
            const css::ResolvedLength x = resolve_with_viewport(parts[0], fs, rfs);
            if (!x.valid) {
                return;
            }
            px = x.px;
            percent = x.is_percent;
            if (parts.size() >= 2) {
                const css::ResolvedLength y = resolve_with_viewport(parts[1], fs, rfs);
                if (y.valid) {
                    py = y.px;
                    percent_y = y.is_percent;
                    return;
                }
            }
            py = px;
            percent_y = percent;
        };
        corner(style_literal(node, "border-top-left-radius"), style.border_top_left_radius,
               style.border_top_left_radius_percent, style.border_top_left_radius_y,
               style.border_top_left_radius_y_percent);
        corner(style_literal(node, "border-top-right-radius"), style.border_top_right_radius,
               style.border_top_right_radius_percent, style.border_top_right_radius_y,
               style.border_top_right_radius_y_percent);
        corner(style_literal(node, "border-bottom-right-radius"), style.border_bottom_right_radius,
               style.border_bottom_right_radius_percent, style.border_bottom_right_radius_y,
               style.border_bottom_right_radius_y_percent);
        corner(style_literal(node, "border-bottom-left-radius"), style.border_bottom_left_radius,
               style.border_bottom_left_radius_percent, style.border_bottom_left_radius_y,
               style.border_bottom_left_radius_y_percent);
        style.border_radius =
            std::max({style.border_top_left_radius, style.border_top_right_radius,
                      style.border_bottom_right_radius, style.border_bottom_left_radius,
                      style.border_top_left_radius_y, style.border_top_right_radius_y,
                      style.border_bottom_right_radius_y, style.border_bottom_left_radius_y});
        style.overflow_x = overflow_of(style_literal(node, "overflow-x"));
        style.overflow_y = overflow_of(style_literal(node, "overflow-y"));
        copy_font(style, node);
        style.lang = lang_of(node);
        const std::string* contain = style_literal(node, "contain");
        style.contain_paint = contain_paint(node);
        style.contain_layout = has_word(contain, "layout") || has_word(contain, "strict") ||
                               has_word(contain, "content");
        const bool contain_size = has_word(contain, "size") || has_word(contain, "strict");
        style.contain_inline_size = contain_size || has_word(contain, "inline-size");
        style.contain_block_size = contain_size || has_word(contain, "block-size");
        // CSS Containment 3: `container-type` applies layout containment plus the queried axis.
        if (style.container_type == ContainerType::InlineSize) {
            style.contain_layout = true;
            style.contain_inline_size = true;
        } else if (style.container_type == ContainerType::Size) {
            style.contain_layout = true;
            style.contain_inline_size = true;
            style.contain_block_size = true;
        }
        if (const std::string* visibility_mode = style_literal(node, "content-visibility")) {
            style.content_visibility_hidden = *visibility_mode == "hidden";
            style.content_visibility_auto = *visibility_mode == "auto";
        }
        // CSS Containment section 4: `content-visibility: auto` and `hidden` change the *used*
        // value of `contain` - the element still computes `contain: none` - turning on layout,
        // style and paint containment. Only layout and paint land anywhere here: `contain: style`
        // scopes counters and quotes and this engine has neither. `auto` adds size containment
        // only while it skips its contents, and that axis is already carried by
        // `contents_skipped` plus `contain-intrinsic-size`; switching size containment on for a
        // box that is still laid out would lock it to its intrinsic height, so it stays off.
        const bool visibility_contains =
            style.content_visibility_hidden || style.content_visibility_auto;
        style.contain_paint = style.contain_paint || visibility_contains;
        style.contain_layout = style.contain_layout || visibility_contains;
        read_intrinsic(style, node);
        if (const std::string* visibility = style_literal(node, "visibility")) {
            style.visibility_collapse = *visibility == "collapse";
        }
        const std::string display =
            style_literal(node, "display") != nullptr ? *style_literal(node, "display") : "";
        style.table_row = table_row_display(display, node->tag);
        style.table_column = table_column_display(display, node->tag);
        read_table(style, node);
        // CSS Containment sections 3.3 and 3.5: `contain: layout` and `contain: paint` *each*
        // establish a containing block for out-of-flow descendants, so both feed this pair. The
        // two flags above already carry what `content-visibility` turns on.
        auto present = [](const std::string* value) {
            return value != nullptr && *value != "none";
        };
        const bool paints_cb = present(style_literal(node, "transform")) ||
                               present(style_literal(node, "filter")) ||
                               present(style_literal(node, "perspective")) || style.contain_paint ||
                               style.contain_layout || will_change_cb(node);
        style.cb_for_fixed = paints_cb;
        style.cb_for_absolute = paints_cb || position != "static";
    } else {
        copy_font(style, node);
        style.lang = lang_of(node);
        read_inherited_inline(style, node);
    }
    ++g_intern_phase.misses;
    std::shared_ptr<const LayoutStyle> held = StyleTable::process().intern(std::move(style));
    if (memoizable) {
        auto& memo = intern_memo();
        if (memo.size() >= kInternMemoLimit) {
            // A key holds its `StyleMap` alive, so an unbounded memo would keep every style a long
            // session ever interned. Dropping the whole map keeps the frame this runs in.
            memo.clear();
        }
        memo.emplace(std::move(memo_key), InternMemoValue{held, g_intern_used_viewport_unit});
    }
    return held;
}

}  // namespace layout
}  // namespace zui
