#include "paint/painter_internal.h"

#include "paint/display_list.h"

#include "perf/perf_budget.h"
#include "css/transition/transition.h"
#include "image/image.h"
#include "layout/clip_path.h"
#include "layout/overflow_clip.h"
#include "layout/paint_map.h"
#include "layout/paint_transform.h"
#include "layout/scrollbar_h.h"
#include "layout/scrollbar_v.h"
#include "layout/stacking.h"

#include "text/font_chain.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace zui {
namespace paint {
namespace painter_detail {
const std::string* computed_slow(const layout::Box& box, const char* key) {
    return box.node == nullptr ? nullptr : box.node->style(key);
}

bool establishes_own_transform(const layout::Box& box) {
    using layout::paint_detail::Aff2;
    using layout::paint_detail::local_transform;
    using layout::paint_detail::xform_of;
    const auto xform = xform_of(box, box.geo.width, box.geo.height);
    const Aff2 local = local_transform(box.geo.width, box.geo.height, xform);
    return std::fabs(local.a - 1.f) > 1e-5f || std::fabs(local.e - 1.f) > 1e-5f ||
           std::fabs(local.b) > 1e-5f || std::fabs(local.d) > 1e-5f;
}

float attr_float(const html::Node* node, const char* key, float fallback) {
    if (node == nullptr) {
        return fallback;
    }
    const auto it = node->attrs.find(key);
    if (it == node->attrs.end() || it->second.empty()) {
        return fallback;
    }
    char* end = nullptr;
    const float v = std::strtof(it->second.c_str(), &end);
    if (end == it->second.c_str()) {
        return fallback;
    }
    return v;
}

float node_float_value(const html::Node* node, float fallback) {
    if (node == nullptr || node->value.empty()) {
        return fallback;
    }
    char* end = nullptr;
    const float v = std::strtof(node->value.c_str(), &end);
    if (end == node->value.c_str()) {
        return fallback;
    }
    return v;
}

bool appearance_native(const layout::Box& box, std::string_view widget_keyword) {
    const std::string* appearance = computed(box, "appearance");
    return appearance == nullptr || *appearance == "auto" || *appearance == "initial" ||
           *appearance == widget_keyword;
}

/// Flow-dump diagnostics. Both helpers are answered from the environment once, so a normal frame
/// never pays for them - the display list is hot.
bool flow_debug_on() {
    static const bool on = std::getenv("ZUI_DEBUG_FLOW_ITEMS") != nullptr;
    return on;
}

/// Flow-dump diagnostics: the element behind a box, which is what the dump names in an item line.
const html::Node* owner_node(const layout::Box& box) {
    if (!flow_debug_on()) {
        return nullptr;
    }
    for (const layout::Box* b = &box; b != nullptr; b = b->parent) {
        if (b->node == nullptr) {
            continue;
        }
        return b->node;
    }
    return nullptr;
}

Color overlay_rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a);

/// Owning DOM node for selection mapping: style_box->node, else the box that holds the run.
const html::Node* run_owning_node(const layout::Box& box, const layout::TextRun& run) {
    if (run.style_box != nullptr && run.style_box->node != nullptr) {
        return run.style_box->node;
    }
    return box.node;
}

bool is_placeholder_node(const html::Node* node) {
    if (node == nullptr || node->parent == nullptr) {
        return false;
    }
    const auto& slot =
        node->parent->pseudo_node[static_cast<int>(html::PseudoElement::Placeholder)];
    return slot.get() == node;
}

void selection_colors(const html::Node* node, Color& fg, Color& bg) {
    if (node == nullptr || node->selection == nullptr || node->selection->flags == 0) {
        fg = kUaHighlightText;
        bg = kUaHighlight;
        return;
    }
    const html::SelectionStyle& s = *node->selection;
    fg = Color{s.fg_r, s.fg_g, s.fg_b, s.fg_a};
    bg = Color{s.bg_r, s.bg_g, s.bg_b, s.bg_a};
}

/// Pixel x of a byte offset inside a mappable run. First knife: one UTF-8 byte ≡ one glyph
/// (ASCII selfcheck). The glyph advances already carry `letter-spacing`: the layout adds it
/// after every character including the last, so adding it here again would push every offset
/// one step past the end of the run.
float run_byte_x(const layout::TextRun& run, std::size_t byte_in_run) {
    // One advance per byte (ASCII, and the ligature caret split that keeps one slot per byte).
    if (run.glyph_advances.size() == run.text.size()) {
        const std::size_t n = std::min(byte_in_run, run.glyph_advances.size());
        float x = 0.f;
        for (std::size_t i = 0; i < n; ++i) {
            x += run.glyph_advances[i];
        }
        return x;
    }
    // One advance per codepoint. Walk UTF-8 so a multi-byte character consumes one advance.
    float x = 0.f;
    std::size_t byte = 0;
    for (float advance : run.glyph_advances) {
        if (byte >= byte_in_run || byte >= run.text.size()) {
            break;
        }
        x += advance;
        ++byte;
        while (byte < run.text.size() &&
               (static_cast<unsigned char>(run.text[byte]) & 0xc0) == 0x80) {
            ++byte;
        }
    }
    return x;
}

/// Parses a computed colour, resolving `currentcolor` against the box's own colour.
Color paint_color(const layout::Box& box, const std::string* v);
Color apply_opacity(Color c, float opacity);
bool parse_css_filter(std::string_view css, FilterEffects& out, Color current);

/// The used background colour of one box, with the same precedence as the background step below:
/// an animated value first, then the computed property, else transparent.
Color used_background(const layout::Box& box) {
    if (box.node != nullptr && (box.node->used.flags & html::VisualUsed::kBg)) {
        return overlay_rgba(box.node->used.bg_r, box.node->used.bg_g, box.node->used.bg_b,
                            box.node->used.bg_a);
    }
    if (const std::string* v = computed(box, "background-color")) {
        return paint_color(box, v);
    }
    return Color::transparent();
}

/// CSS Backgrounds 3 §2.11 solid-color half: root color, else body's. Image/gradient donation is
/// resolved later in `Painter::paint` once the image helpers are in scope.
Color canvas_background(const layout::Box& root) {
    const Color from_root = used_background(root);
    if (!color_is_transparent(from_root)) {
        return from_root;
    }
    for (const layout::Box* child : root.children) {
        if (child != nullptr && child->node != nullptr && child->node->tag == "body") {
            return used_background(*child);
        }
    }
    return Color::transparent();
}

/// Fullscreen dim under modal top-layer dialogs (HTML `::backdrop`). No layout box: fill the
/// viewport from the cascaded `Node::backdrop` map, document order, before the box tree.
void paint_modal_backdrops(const layout::Box& box, DisplayList& out, const Rect& vp) {
    if (box.node != nullptr && box.node->modal && box.node->backdrop != nullptr) {
        const auto it = box.node->backdrop->find("background-color");
        if (it != box.node->backdrop->end() && !it->second.empty()) {
            const Color dim = parse_color(it->second);
            if (!color_is_transparent(dim)) {
                out.fill_rect(vp, dim, 0.f);
            }
        }
    }
    for (const layout::Box* child : box.children) {
        if (child != nullptr) {
            paint_modal_backdrops(*child, out, vp);
        }
    }
}

bool display_none(const layout::Box& box) {
    return box.style().outer == layout::DisplayOuter::None;
}

/// CSS Visibility: `hidden` skips painting this box but still visits descendants, so a child with
/// `visibility: visible` can paint (PIPELINE.md).
bool visibility_hidden(const layout::Box& box) {
    const std::string* v = computed(box, "visibility");
    return v != nullptr && *v == "hidden";
}

Color apply_opacity(Color c, float opacity) {
    if (opacity >= 0.999f) {
        return c;
    }
    const float a = static_cast<float>(c.a) * std::max(0.0f, std::min(1.0f, opacity));
    c.a = static_cast<std::uint8_t>(a + 0.5f);
    return c;
}

float node_opacity(const layout::Box& box) {
    if (box.node != nullptr && (box.node->used.flags & html::VisualUsed::kOpacity)) {
        return box.node->used.opacity;
    }
    if (const std::string* v = computed(box, "opacity")) {
        return css::parse_opacity(*v);
    }
    return 1.0f;
}

Color overlay_rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    return Color{r, g, b, a};
}

bool ident_at(std::string_view s, size_t i, std::string_view ident) {
    if (i + ident.size() > s.size() || s.compare(i, ident.size(), ident) != 0) {
        return false;
    }
    const auto ident_char = [](unsigned char c) {
        return std::isalnum(c) || c == '-' || c == '_';
    };
    if (i > 0 && ident_char(static_cast<unsigned char>(s[i - 1]))) {
        return false;
    }
    const size_t end = i + ident.size();
    return end == s.size() || !ident_char(static_cast<unsigned char>(s[end]));
}

std::string replace_ident(std::string_view s, std::string_view ident, std::string_view with) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if (ident_at(s, i, ident)) {
            out.append(with);
            i += ident.size();
        } else {
            out.push_back(s[i]);
            ++i;
        }
    }
    return out;
}

bool css_ident_char(unsigned char c) {
    return std::isalnum(c) != 0 || c == '-' || c == '_';
}

/// `open` is the '(' of a `light-dark(` call that starts at `i`. Whitespace may sit before
/// the parenthesis. A longer ident (`light-darker`) is not a call.
bool light_dark_call_at(std::string_view s, std::size_t i, std::size_t& open) {
    constexpr char kName[] = "light-dark";
    constexpr std::size_t n = sizeof(kName) - 1;
    if (i + n > s.size()) {
        return false;
    }
    if (i > 0 && css_ident_char(static_cast<unsigned char>(s[i - 1]))) {
        return false;
    }
    for (std::size_t k = 0; k < n; ++k) {
        const unsigned char c = static_cast<unsigned char>(s[i + k]);
        if (std::tolower(c) != static_cast<unsigned char>(kName[k])) {
            return false;
        }
    }
    std::size_t j = i + n;
    if (j < s.size() && css_ident_char(static_cast<unsigned char>(s[j]))) {
        return false;
    }
    while (j < s.size() && std::isspace(static_cast<unsigned char>(s[j]))) {
        ++j;
    }
    if (j >= s.size() || s[j] != '(') {
        return false;
    }
    open = j;
    return true;
}

std::size_t matching_paren(std::string_view s, std::size_t open) {
    int depth = 0;
    for (std::size_t i = open; i < s.size(); ++i) {
        if (s[i] == '(') {
            ++depth;
        } else if (s[i] == ')') {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    return std::string_view::npos;
}

/// Two `<color>` arguments. A comma inside nested parentheses stays in its argument.
bool split_two_color_args(std::string_view inner, std::string_view& a, std::string_view& b) {
    int depth = 0;
    std::size_t comma = std::string_view::npos;
    for (std::size_t i = 0; i < inner.size(); ++i) {
        const char c = inner[i];
        if (c == '(') {
            ++depth;
        } else if (c == ')' && depth > 0) {
            --depth;
        } else if (c == ',' && depth == 0) {
            if (comma != std::string_view::npos) {
                return false;
            }
            comma = i;
        }
    }
    if (comma == std::string_view::npos) {
        return false;
    }
    a = trim_sv_local(inner.substr(0, comma));
    b = trim_sv_local(inner.substr(comma + 1));
    return !a.empty() && !b.empty();
}

/// Replace every `light-dark()` call with the chosen argument. Nested calls resolve first.
/// A string with no well-formed call is returned unchanged.
std::string substitute_light_dark(bool dark, std::string_view in) {
    std::string out;
    out.reserve(in.size());
    bool replaced = false;
    for (std::size_t i = 0; i < in.size();) {
        std::size_t open = 0;
        if (!light_dark_call_at(in, i, open)) {
            out.push_back(in[i]);
            ++i;
            continue;
        }
        const std::size_t close = matching_paren(in, open);
        if (close == std::string_view::npos) {
            out.append(in.substr(i));
            break;
        }
        std::string_view light;
        std::string_view dark_arg;
        const std::string_view inner = in.substr(open + 1, close - open - 1);
        if (!split_two_color_args(inner, light, dark_arg)) {
            out.append(in.substr(i, close + 1 - i));
            i = close + 1;
            continue;
        }
        out += substitute_light_dark(dark, dark ? dark_arg : light);
        replaced = true;
        i = close + 1;
    }
    if (!replaced) {
        return std::string(in);
    }
    return out;
}

/// CSS Color Adjustment used color scheme, for `light-dark()`. The preferred scheme on this
/// path is light (the engine default): it wins when `light` is listed, otherwise a list that
/// names `dark` is dark. `only` does not change the pick. `normal` and a missing property stay
/// light. Called only when the color string contains `light-dark`.
bool color_scheme_is_dark(const layout::Box& box) {
    const std::string* raw = computed(box, "color-scheme");
    if (raw == nullptr) {
        return false;
    }
    bool saw_light = false;
    bool saw_dark = false;
    std::string_view s(*raw);
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
            ++i;
        }
        if (i >= s.size()) {
            break;
        }
        const std::size_t start = i;
        while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) {
            ++i;
        }
        const std::string word = lower_ascii(s.substr(start, i - start));
        if (word == "only" || word == "normal") {
            continue;
        }
        if (word == "light") {
            saw_light = true;
        } else if (word == "dark") {
            saw_dark = true;
        }
    }
    if (saw_light) {
        return false;
    }
    return saw_dark;
}

Color paint_color(const layout::Box& box, const std::string* v) {
    if (v == nullptr) {
        return Color{0, 0, 0, 255};
    }
    std::string x = *v;
    for (char& c : x) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    // CSS Color 5 `light-dark()`. Resolve before currentcolor substitution so a chosen
    // argument is what the rest of this function parses. One scheme lookup per color that
    // mentions the function; other colors do not read `color-scheme`.
    if (x.find("light-dark") != std::string::npos) {
        const std::string resolved = substitute_light_dark(color_scheme_is_dark(box), *v);
        if (resolved != *v) {
            return paint_color(box, &resolved);
        }
    }
    if (x == "currentcolor") {
        if (const std::string* c = computed(box, "color")) {
            return parse_color(*c);
        }
        return Color{0, 0, 0, 255};
    }
    // color-mix() and the modern color functions take currentcolor as an argument. The
    // keyword is the element's own color, substituted once so a color that itself mentions
    // currentcolor cannot recurse.
    if (x.find("currentcolor") != std::string::npos) {
        Color cc{0, 0, 0, 255};
        if (const std::string* c = computed(box, "color")) {
            std::string lower = *c;
            for (char& ch : lower) {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            if (lower.find("currentcolor") == std::string::npos) {
                cc = parse_color(*c);
            }
        }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "rgb(%u %u %u / %u)", cc.r, cc.g, cc.b, cc.a);
        return parse_color(replace_ident(x, "currentcolor", buf));
    }
    return parse_color(*v);
}

void parse_scrollbar_colors(const layout::Box& box, Color& thumb, Color& track) {
    // The default is the UA's own classic bar, measured off Chrome at device scale 1: a 15px bar
    // whose track is #fcfcfc and whose thumb is #8b8b8b, with the thumb inset 3px on each side.
    // These were a dark invention (thumb #6b7588 on track #161c27), which made every page with a
    // scroll container differ from the browser by a full-height band in the screenshot comparison.
    // `scrollbar-color` overrides both. Chrome would pick its *dark* bar when the page declares
    // `color-scheme: dark`, which this engine does not parse yet (docs/LAYOUT.md section 9).
    thumb = Color{139, 139, 139, 255};
    track = Color{252, 252, 252, 255};
    const std::string* v = computed(box, "scrollbar-color");
    if (v == nullptr) {
        return;
    }
    std::string_view s = *v;
    auto take = [&](Color& outc) {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
            s.remove_prefix(1);
        }
        if (s.empty()) {
            return false;
        }
        size_t n = 0;
        if (s[0] == '#') {
            n = (s.size() >= 7) ? 7 : (s.size() >= 4 ? 4 : 0);
        } else {
            // A functional colour carries spaces inside its parentheses
            // (`rgba(92, 72, 44, 0.38)`). Reading to the next space stopped at `rgba(92,` and
            // parsed a half colour, so the reader's thin scrollbar came out as pure dark red.
            int depth = 0;
            while (n < s.size()) {
                const char ch = s[n];
                if (ch == '(') {
                    ++depth;
                } else if (ch == ')') {
                    if (depth > 0) {
                        --depth;
                    }
                } else if (depth == 0 && std::isspace(static_cast<unsigned char>(ch))) {
                    break;
                }
                ++n;
            }
        }
        if (n == 0) {
            return false;
        }
        outc = parse_color(s.substr(0, n));
        s.remove_prefix(n);
        return true;
    };
    Color a;
    Color b;
    if (take(a) && take(b)) {
        thumb = a;
        track = b;
    }
}

}  // namespace painter_detail
}  // namespace paint
}  // namespace zui
