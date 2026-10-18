#include "layout/intern_style.h"
#include "layout/style_literal.h"

#include "layout/cache/key_policy.h"

#include "layout/intern_grid.h"

#include "layout/style_table.h"

#include "css/values.h"
#include "text/font_chain.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "layout/intern_detail.h"

namespace zui {
namespace layout {
namespace intern_detail {


/// `calc()` / `min()` / `max()` / `clamp()` as the leading token of a computed value. The same
/// test `normal_flow.cpp` uses before it resolves a width lazily; it is also what decides whether
/// the initial containing block has to stay in the layout key (see `cache/key_policy.h`).
bool is_math_length(const std::string* value) {
    if (value == nullptr) {
        return false;
    }
    return value->rfind("calc(", 0) == 0 || value->rfind("min(", 0) == 0 ||
           value->rfind("max(", 0) == 0 || value->rfind("clamp(", 0) == 0;
}



bool ends_with(const std::string& value, char suffix) {
    return !value.empty() && value.back() == suffix;
}

ParsedLength parse_length(const html::Node* node, const std::string* v, bool missing_is_auto) {
    ParsedLength parsed;
    parsed.is_auto = missing_is_auto;
    if (node == nullptr || v == nullptr) {
        return parsed;
    }
    if (*v == "auto") {
        parsed.valid = true;
        parsed.is_auto = true;
        return parsed;
    }
    const css::ResolvedLength resolved =
        resolve_with_viewport(*v, font_size_px(node), root_font_size_px(node));
    if (!resolved.valid) {
        return parsed;
    }
    parsed.valid = true;
    parsed.is_auto = false;
    parsed.is_percent = resolved.is_percent;
    parsed.value = resolved.px;
    return parsed;
}

/// Whether a declaration is the one case the style system cannot decide: a math function whose
/// expression mentions `%`. `parse_length` has already failed on it, and without the containing
/// block there is nothing more to compute (CSS Values section 10).
bool keeps_basis_text(const std::string& raw) {
    const bool math = raw.rfind("calc(", 0) == 0 || raw.rfind("min(", 0) == 0 ||
                      raw.rfind("max(", 0) == 0 || raw.rfind("clamp(", 0) == 0;
    return math && raw.find('%') != std::string::npos;
}

/// Rewrites the relative units of a kept declaration into px, so that only its `%` is left for the
/// used-value pass. Every other length gets the same treatment while it is interned - `parse_length`
/// resolves `em` / `rem` / `vw` / `ch` / `ex` / `lh` against these very facts - and a resize
/// invalidates the whole tree, so a baked `vw` cannot go stale. A unit the engine cannot resolve
/// (`cqw` without a basis, ...) is left alone: the used-value pass then fails on it and the
/// property keeps its initial value.
std::string bake_relative_units(std::string_view text, float font_size, float root_font_size,
                                float viewport_width, float viewport_height,
                                const css::FontRelativeUnits& font_rel) {
    std::string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (!std::isdigit(static_cast<unsigned char>(c)) && c != '.') {
            out.push_back(c);
            ++i;
            continue;
        }
        const std::size_t number_start = i;
        while (i < text.size() &&
               (std::isdigit(static_cast<unsigned char>(text[i])) || text[i] == '.')) {
            ++i;
        }
        std::size_t unit_end = i;
        while (unit_end < text.size() && std::isalpha(static_cast<unsigned char>(text[unit_end]))) {
            ++unit_end;
        }
        std::string unit;
        for (std::size_t k = i; k < unit_end; ++k) {
            unit.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(text[k]))));
        }
        const float number =
            std::strtof(std::string(text.substr(number_start, i - number_start)).c_str(), nullptr);
        float unit_px = 0.f;
        if (unit == "em") {
            unit_px = font_size;
        } else if (unit == "rem") {
            unit_px = root_font_size;
        } else if (unit == "ch") {
            unit_px = font_rel.ch > 0.f ? font_rel.ch : font_size * 0.5f;
        } else if (unit == "ex") {
            unit_px = font_rel.ex > 0.f ? font_rel.ex : font_size * 0.5f;
        } else if (unit == "lh") {
            unit_px = font_rel.lh > 0.f ? font_rel.lh : font_size;
        } else if (unit == "vw" || unit == "dvw" || unit == "svw" || unit == "lvw" ||
                   unit == "vi") {
            unit_px = viewport_width * 0.01f;
        } else if (unit == "vh" || unit == "dvh" || unit == "svh" || unit == "lvh" ||
                   unit == "vb") {
            unit_px = viewport_height * 0.01f;
        } else if (unit == "vmin") {
            unit_px = std::min(viewport_width, viewport_height) * 0.01f;
        } else if (unit == "vmax") {
            unit_px = std::max(viewport_width, viewport_height) * 0.01f;
        }
        if (unit_px > 0.f) {
            char buffer[40];
            std::snprintf(buffer, sizeof(buffer), "%.4fpx", number * unit_px);
            out.append(buffer);
        } else {
            out.append(text.substr(number_start, unit_end - number_start));
        }
        i = unit_end;
    }
    return out;
}

/// `parse_length`, plus the one case the style system cannot decide. A declaration whose math mixes
/// `%` with lengths keeps its text on @p style, with the relative units baked, and the property
/// holds its initial value until the used-value pass resolves it against the containing block
/// (`used_edge` / `used_main` in `layout/cache/used_length.h`).
ParsedLength parse_length_kept(LayoutStyle& style, LengthProperty prop, const html::Node* node,
                               const std::string* raw, bool missing_is_auto) {
    const ParsedLength parsed = parse_length(node, raw, missing_is_auto);
    if (parsed.valid || node == nullptr || raw == nullptr || !keeps_basis_text(*raw)) {
        return parsed;
    }
    MathLength kept;
    kept.prop = prop;
    kept.text = bake_relative_units(*raw, font_size_px(node), root_font_size_px(node),
                                    g_length_viewport.width, g_length_viewport.height, g_font_rel);
    style.math_lengths.push_back(std::move(kept));
    return parsed;
}

// `min-content` / `max-content` / `fit-content` (CSS Sizing section 5) are not lengths: the used
// size comes from the box's own intrinsic size, so they are recorded next to the length and the
// layout resolves them.
IntrinsicSize intrinsic_of(const std::string* v) {
    if (v == nullptr) {
        return IntrinsicSize::None;
    }
    if (*v == "min-content") {
        return IntrinsicSize::MinContent;
    }
    if (*v == "max-content") {
        return IntrinsicSize::MaxContent;
    }
    if (*v == "fit-content") {
        return IntrinsicSize::FitContent;
    }
    return IntrinsicSize::None;
}

float px_of(const html::Node* node, const std::string* value, float fallback) {
    const ParsedLength parsed = parse_length(node, value, false);
    if (!parsed.valid || parsed.is_auto) {
        return fallback;
    }
    return parsed.value;
}

OverflowMode overflow_of(const std::string* v) {
    if (v == nullptr) {
        return OverflowMode::Visible;
    }
    if (*v == "hidden") {
        return OverflowMode::Hidden;
    }
    if (*v == "clip") {
        // CSS Overflow 3: clips without creating a scroll container.
        return OverflowMode::Clip;
    }
    if (*v == "auto") {
        return OverflowMode::Auto;
    }
    if (*v == "scroll") {
        return OverflowMode::Scroll;
    }
    return OverflowMode::Visible;
}

bool has_word(const std::string* value, const char* word) {
    if (value == nullptr || word == nullptr) {
        return false;
    }
    const std::size_t n = std::strlen(word);
    std::size_t pos = 0;
    while ((pos = value->find(word, pos)) != std::string::npos) {
        const auto boundary = [](unsigned char c) { return !std::isalnum(c) && c != '-'; };
        const bool left = pos == 0 || boundary(static_cast<unsigned char>((*value)[pos - 1]));
        const bool right =
            pos + n >= value->size() || boundary(static_cast<unsigned char>((*value)[pos + n]));
        if (left && right) {
            return true;
        }
        pos += n;
    }
    return false;
}

bool contain_paint(const html::Node* node) {
    const std::string* value = node == nullptr ? nullptr : style_literal(node, "contain");
    return has_word(value, "paint") || has_word(value, "strict") || has_word(value, "content");
}

bool will_change_cb(const html::Node* node) {
    const std::string* value = node == nullptr ? nullptr : style_literal(node, "will-change");
    return has_word(value, "transform") || has_word(value, "filter") ||
           has_word(value, "perspective");
}

bool not_none(const html::Node* node, const char* key) {
    const std::string* value = style_literal_slow(node, key);
    return value != nullptr && *value != "none";
}

int parse_font_weight(const std::string* value) {
    if (value == nullptr || value->empty()) {
        return 400;
    }
    const std::string& v = *value;
    if (v == "normal" || v == "lighter") {
        return 400;
    }
    if (v == "bold" || v == "bolder") {
        return 700;
    }
    char* end = nullptr;
    const long parsed = std::strtol(v.c_str(), &end, 10);
    if (end != v.c_str() && parsed >= 1 && parsed <= 1000) {
        return static_cast<int>(parsed);
    }
    return 400;
}

bool parse_font_italic(const std::string* value) {
    if (value == nullptr) {
        return false;
    }
    return *value == "italic" || *value == "oblique";
}

/// `font-kerning`: the initial `auto` and the explicit `normal` both leave the face's `kern`
/// feature on, so only `none` is recorded.
bool parse_font_kerning_none(const std::string* value) {
    return value != nullptr && *value == "none";
}

void copy_font(LayoutStyle& style, const html::Node* node) {
    // `font_size_px` already resolves `em` / `%` / `rem` against the parent and the root element.
    // Routing the same declaration through `px_of` resolved it a second time with the element's own
    // size as the `em` basis, so `font-size: 0.8em` inside 19px measured at 10.24px instead of
    // 15.2px and the painted glyphs overlapped their advances.
    style.font_size = font_size_px(node);
    if (const std::string* family = node == nullptr ? nullptr : style_literal(node, "font-family")) {
        style.font_family = *family;
    }
    if (style.font_family.empty()) {
        style.font_family = "sans-serif";
    }
    style.font_weight = parse_font_weight(node == nullptr ? nullptr : style_literal(node, "font-weight"));
    style.font_italic = parse_font_italic(node == nullptr ? nullptr : style_literal(node, "font-style"));
    style.font_kerning_none =
        parse_font_kerning_none(node == nullptr ? nullptr : style_literal(node, "font-kerning"));
}

std::unordered_map<const html::Node*, std::string>& lang_cache() {
    // Leaked like the font-size cache: a static map of `std::string` torn down after the rest of
    // the process has been destroyed faulted the test binaries on the way out.
    static auto* cache = new std::unordered_map<const html::Node*, std::string>();
    return *cache;
}

std::string lang_of(const html::Node* node) {
    if (node == nullptr) {
        return {};
    }
    auto& cache = lang_cache();
    if (const auto hit = cache.find(node); hit != cache.end()) {
        return hit->second;
    }
    // `attrs` is keyed by `std::string`. A literal builds a temporary on every ancestor of every
    // intern, including memo hits. One walk fills the chain; later boxes in the same layout hit.
    static const std::string lang_key("lang");
    std::string lang;
    const html::Node* stop = nullptr;
    for (const html::Node* current = node; current != nullptr; current = current->parent) {
        if (const auto hit = cache.find(current); hit != cache.end()) {
            lang = hit->second;
            stop = current;
            break;
        }
        const auto it = current->attrs.find(lang_key);
        if (it != current->attrs.end() && !it->second.empty()) {
            lang = it->second;
            cache.emplace(current, lang);
            stop = current;
            break;
        }
    }
    for (const html::Node* current = node; current != stop; current = current->parent) {
        cache.emplace(current, lang);
    }
    return lang;
}

void clear_lang_cache() { lang_cache().clear(); }

void apply_length(bool& is_auto, bool& is_percent, float& value, const ParsedLength& parsed) {
    is_auto = !parsed.valid || parsed.is_auto;
    is_percent = parsed.valid && parsed.is_percent && !parsed.is_auto;
    value = parsed.valid ? parsed.value : 0;
}

void apply_edge(float& value, bool& is_percent, const ParsedLength& parsed) {
    is_percent = parsed.valid && parsed.is_percent;
    value = parsed.valid ? parsed.value : 0;
}

// An unusable margin is 0, not auto: auto is its own keyword and means the free space.
void apply_margin(float& value, bool& is_percent, bool& is_auto, const ParsedLength& parsed) {
    is_auto = parsed.valid && parsed.is_auto;
    is_percent = parsed.valid && parsed.is_percent && !parsed.is_auto;
    value = parsed.valid ? parsed.value : 0;
}

bool has_unit(const std::string& value) {
    for (unsigned char c : value) {
        if (std::isalpha(c)) {
            return true;
        }
    }
    return false;
}

void read_line_height(LayoutStyle& style, const std::string* value) {
    if (value == nullptr || *value == "normal") {
        return;
    }
    style.line_height_value = std::strtof(value->c_str(), nullptr);
    if (ends_with(*value, '%')) {
        style.line_height_kind = LineHeightKind::Percent;
    } else if (!has_unit(*value)) {
        style.line_height_kind = LineHeightKind::Number;
    } else {
        style.line_height_kind = LineHeightKind::Length;
    }
}

void read_text_align(LayoutStyle& style, const std::string* value) {
    if (value == nullptr) {
        return;
    }
    if (*value == "end") {
        style.text_align = TextAlign::End;
    } else if (*value == "left") {
        style.text_align = TextAlign::Left;
    } else if (*value == "right") {
        style.text_align = TextAlign::Right;
    } else if (*value == "center") {
        style.text_align = TextAlign::Center;
    } else if (*value == "justify") {
        style.text_align = TextAlign::Justify;
    }
}

void read_vertical_align(LayoutStyle& style, const html::Node* node) {
    const std::string* value = node == nullptr ? nullptr : style_literal(node, "vertical-align");
    if (value == nullptr || *value == "baseline") {
        return;
    }
    if (*value == "sub") {
        style.vertical_align = VerticalAlign::Sub;
    } else if (*value == "super") {
        style.vertical_align = VerticalAlign::Super;
    } else if (*value == "top") {
        style.vertical_align = VerticalAlign::Top;
    } else if (*value == "middle") {
        style.vertical_align = VerticalAlign::Middle;
    } else if (*value == "bottom") {
        style.vertical_align = VerticalAlign::Bottom;
    } else if (*value == "text-top") {
        style.vertical_align = VerticalAlign::TextTop;
    } else if (*value == "text-bottom") {
        style.vertical_align = VerticalAlign::TextBottom;
    } else {
        style.vertical_align = VerticalAlign::Length;
        style.vertical_align_percent = ends_with(*value, '%');
        if (style.vertical_align_percent) {
            // CSS 2.1 section 10.8.1: a percentage is against the element's own line-height, which
            // `align_shift` resolves with the font facts - the number is what is stored.
            style.vertical_align_px = std::strtof(value->c_str(), nullptr);
        } else {
            // A length, not a number: reading the digits straight off the text made Chromium's own
            // `meter { vertical-align: -0.2em }` a -0.2 *pixel* shift, so the gauge sat 3px above
            // the browser's box and the whole line box shrank with it (`ua-cold-tags`).
            const css::ResolvedLength resolved =
                resolve_with_viewport(*value, font_size_px(node), root_font_size_px(node));
            style.vertical_align_px = resolved.valid ? resolved.px : 0.f;
        }
    }
}

void read_inherited_inline(LayoutStyle& style, const html::Node* node) {
    if (node == nullptr) {
        return;
    }
    if (const std::string* wm = style_literal(node, "writing-mode")) {
        if (*wm == "vertical-rl") {
            style.writing_mode = WritingMode::VerticalRl;
        } else if (*wm == "vertical-lr") {
            style.writing_mode = WritingMode::VerticalLr;
        } else if (*wm == "sideways-rl") {
            style.writing_mode = WritingMode::SidewaysRl;
        } else if (*wm == "sideways-lr") {
            style.writing_mode = WritingMode::SidewaysLr;
        } else {
            style.writing_mode = WritingMode::HorizontalTb;
        }
    }
    if (const std::string* dir = style_literal(node, "direction")) {
        if (*dir == "rtl") {
            style.direction = Direction::Rtl;
        } else {
            style.direction = Direction::Ltr;
        }
    }
    if (const std::string* white = style_literal(node, "white-space")) {
        if (*white == "nowrap") {
            style.white_space = WhiteSpace::Nowrap;
        } else if (*white == "pre") {
            style.white_space = WhiteSpace::Pre;
        } else if (*white == "pre-wrap") {
            style.white_space = WhiteSpace::PreWrap;
        } else if (*white == "pre-line") {
            style.white_space = WhiteSpace::PreLine;
        } else if (*white == "break-spaces") {
            style.white_space = WhiteSpace::BreakSpaces;
        }
    }
    if (const std::string* wb = style_literal(node, "word-break")) {
        if (*wb == "break-all") {
            style.word_break = WordBreak::BreakAll;
        } else if (*wb == "keep-all") {
            style.word_break = WordBreak::KeepAll;
        } else if (*wb == "break-word") {
            // Legacy alias of overflow-wrap: anywhere (CSS Text 3).
            style.overflow_wrap = OverflowWrap::Anywhere;
        }
    }
    if (const std::string* ow = style_literal(node, "overflow-wrap")) {
        if (*ow == "break-word") {
            style.overflow_wrap = OverflowWrap::BreakWord;
        } else if (*ow == "anywhere") {
            style.overflow_wrap = OverflowWrap::Anywhere;
        }
    } else if (const std::string* ww = style_literal(node, "word-wrap")) {
        if (*ww == "break-word") {
            style.overflow_wrap = OverflowWrap::BreakWord;
        } else if (*ww == "anywhere") {
            style.overflow_wrap = OverflowWrap::Anywhere;
        }
    }
    read_line_height(style, style_literal(node, "line-height"));
    read_text_align(style, style_literal(node, "text-align"));
    if (const std::string* transform = style_literal(node, "text-transform")) {
        if (*transform == "uppercase") {
            style.text_transform = TextTransform::Uppercase;
        } else if (*transform == "lowercase") {
            style.text_transform = TextTransform::Lowercase;
        } else if (*transform == "capitalize") {
            style.text_transform = TextTransform::Capitalize;
        }
    }
    const ParsedLength indent = parse_length(node, style_literal(node, "text-indent"), false);
    style.text_indent_percent = indent.valid && indent.is_percent;
    style.text_indent_px = indent.valid && !indent.is_auto ? indent.value : 0;
    if (const std::string* ratio = style_literal(node, "aspect-ratio")) {
        // The syntax also allows a leading `auto`; the subset takes a bare `<number> [ / <number> ]`.
        if (*ratio != "auto" && ratio->rfind("auto", 0) != 0) {
            const std::size_t slash = ratio->find('/');
            const float width = std::strtof(ratio->c_str(), nullptr);
            const float height =
                slash != std::string::npos
                    ? std::strtof(ratio->c_str() + static_cast<int>(slash) + 1, nullptr)
                    : 1.f;
            if (width > 0.f && height > 0.f) {
                style.aspect_ratio = width / height;
            }
        }
    }
    if (const std::string* spacing = style_literal(node, "letter-spacing")) {
        if (*spacing != "normal") {
            // A length, so `em` resolves against this element's font size. Reading the number
            // straight out of the string made `letter-spacing: 0.08em` count as 0.08px.
            const ParsedLength value = parse_length(node, spacing, false);
            style.letter_spacing = value.valid && !value.is_auto ? value.value : 0.f;
        }
    }
    if (const std::string* spacing = style_literal(node, "word-spacing")) {
        if (*spacing != "normal") {
            const ParsedLength value = parse_length(node, spacing, false);
            style.word_spacing = value.valid && !value.is_auto ? value.value : 0.f;
        }
    }
    if (const std::string* overflow = style_literal(node, "text-overflow")) {
        if (*overflow == "ellipsis") {
            style.text_overflow = TextOverflow::Ellipsis;
        }
    }
}

void read_gutter(LayoutStyle& style, const html::Node* node) {
    const std::string* value = node == nullptr ? nullptr : style_literal(node, "scrollbar-gutter");
    if (value == nullptr) {
        return;
    }
    if (value->find("both-edges") != std::string::npos) {
        style.scrollbar_gutter = ScrollbarGutter::BothEdges;
    } else if (value->find("stable") != std::string::npos) {
        style.scrollbar_gutter = ScrollbarGutter::Stable;
    }
}

void read_scrollbar_width(LayoutStyle& style, const html::Node* node) {
    const std::string* value = node == nullptr ? nullptr : style_literal(node, "scrollbar-width");
    if (value == nullptr) {
        return;
    }
    if (value->find("none") != std::string::npos) {
        style.scrollbar_width = ScrollbarWidth::None;
    } else if (value->find("thin") != std::string::npos) {
        style.scrollbar_width = ScrollbarWidth::Thin;
    }
}

void apply_max(bool& is_none, bool& is_percent, float& value, LayoutStyle& style,
               LengthProperty prop, const html::Node* node, const std::string* raw) {
    is_none = true;
    is_percent = false;
    value = 0;
    if (node == nullptr || raw == nullptr || *raw == "none") {
        return;
    }
    const ParsedLength parsed = parse_length_kept(style, prop, node, raw, false);
    if (!parsed.valid || parsed.is_auto) {
        return;
    }
    is_none = false;
    is_percent = parsed.is_percent;
    value = parsed.value;
}

}  // namespace intern_detail
}  // namespace layout
}  // namespace zui
