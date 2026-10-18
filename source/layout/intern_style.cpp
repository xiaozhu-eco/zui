#include "layout/intern_style.h"
#include "layout/intern_detail.h"
#include "layout/style_literal.h"

#include "layout/cache/key_policy.h"

#include "layout/intern_grid.h"

#include "layout/style_table.h"

#include "css/values.h"
#include "text/font_chain.h"

#include "log/log.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace zui {
namespace layout {

using namespace intern_detail;

// Font-relative units for the intern session bound by `LayoutEngine::layout`.

/// The viewport a `<length>` in viewport units resolves against (CSS Values section 5.1). It is
/// set on entry to `intern_style_as`, which is where every length of one box is read. A window
/// resize re-interns the boxes that consumed one of these units (`Box::depends_on_viewport`);
/// a scrollbar shrinking the initial containing block does not, so `vw` / `vh` stay on the
/// full window.
namespace {

bool is_viewport_unit(std::string_view unit) {
    return unit == "vw" || unit == "dvw" || unit == "svw" || unit == "lvw" || unit == "vi" ||
           unit == "vh" || unit == "dvh" || unit == "svh" || unit == "lvh" || unit == "vb" ||
           unit == "vmin" || unit == "vmax";
}

int font_rel_weight(const std::string* value) {
    if (value == nullptr || *value == "normal") {
        return 400;
    }
    if (*value == "bold") {
        return 700;
    }
    char* end = nullptr;
    const long parsed = std::strtol(value->c_str(), &end, 10);
    if (end != nullptr && end != value->c_str() && *end == '\0' && parsed >= 1 && parsed <= 1000) {
        return static_cast<int>(parsed);
    }
    return 400;
}

bool font_rel_italic(const std::string* value) {
    return value != nullptr && (*value == "italic" || *value == "oblique");
}

/// What the FreeType half of `font_rel_of` produces for one used font. It depends only on the
/// family list, the weight, italic and the used size - not on the node, the viewport, or the
/// inherited line-height - so those four are the whole memo key.
struct FaceMetrics {
    float ch = 0.f;
    float ex = 0.f;
    float ascent = 0.f;
    float descent = 0.f;
};

struct FaceMetricsKey {
    std::string family;
    int weight = 400;
    bool italic = false;
    float font_px = 16.f;
    /// `register_embedded_font` may change what a family resolves to after the first layout.
    std::uint64_t registry = 0;

    bool operator==(const FaceMetricsKey& other) const {
        return family == other.family && weight == other.weight && italic == other.italic &&
               font_px == other.font_px && registry == other.registry;
    }
};

struct FaceMetricsKeyHash {
    std::size_t operator()(const FaceMetricsKey& key) const {
        std::size_t hash = std::hash<std::string>{}(key.family);
        const auto mix = [&hash](std::size_t value) {
            hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
        };
        mix(static_cast<std::size_t>(key.weight));
        mix(key.italic ? 1u : 0u);
        mix(std::hash<float>{}(key.font_px));
        mix(std::hash<std::uint64_t>{}(key.registry));
        return hash;
    }
};

/// Bounded: a page has a handful of font configurations, but a `font-size` animation would
/// otherwise keep one entry per frame. Leaked like the intern memo, for the same reason - what a
/// leaking static does at process teardown already bit these binaries once.
constexpr std::size_t kFaceMetricsMemoLimit = 512;

std::unordered_map<FaceMetricsKey, FaceMetrics, FaceMetricsKeyHash>& face_metrics_memo() {
    static auto* memo = new std::unordered_map<FaceMetricsKey, FaceMetrics, FaceMetricsKeyHash>();
    return *memo;
}

FaceMetrics face_metrics_for(const std::string& family, int weight, bool italic, float font_px) {
    FaceMetricsKey key;
    key.family = family;
    key.weight = weight;
    key.italic = italic;
    key.font_px = font_px;
    key.registry = text::font_registry_generation();
    auto& memo = face_metrics_memo();
    const auto hit = memo.find(key);
    if (hit != memo.end()) {
        return hit->second;
    }
    FaceMetrics metrics;
    text::FontChain chain;
    if (chain.init(family, weight, italic)) {
        const float zero = chain.load(static_cast<std::uint32_t>('0'), font_px, false).advance;
        metrics.ch = zero > 0.f ? zero : font_px * 0.5f;
        metrics.ex = chain.x_height() > 0.f ? chain.x_height() : font_px * 0.5f;
        metrics.ascent = chain.ascent() > 0.f ? std::round(chain.ascent()) : font_px * 0.8f;
        metrics.descent = chain.descent() > 0.f ? std::round(chain.descent()) : font_px * 0.2f;
    } else {
        metrics.ch = font_px * 0.5f;
        metrics.ex = font_px * 0.5f;
        metrics.ascent = font_px * 0.8f;
        metrics.descent = font_px * 0.2f;
    }
    if (memo.size() >= kFaceMetricsMemoLimit) {
        memo.clear();
    }
    memo.emplace(std::move(key), metrics);
    return metrics;
}

/// True when a computed value contains a `ch` / `ex` / `lh` dimension. Those are the only
/// consumers of `font_rel_of`'s FreeType face: used line-height is measured later from the
/// layout font, and every other length ignores the font-relative bases. A miss here would
/// resolve those units through the 0.5em / 1em fallback, so the scan is conservative.
bool value_uses_font_relative_unit(std::string_view text) {
    const std::size_t n = text.size();
    for (std::size_t i = 0; i < n; ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (std::isdigit(c) == 0 && text[i] != '.') {
            continue;
        }
        std::size_t j = i;
        bool digit = false;
        while (j < n && (std::isdigit(static_cast<unsigned char>(text[j])) != 0 || text[j] == '.')) {
            if (std::isdigit(static_cast<unsigned char>(text[j])) != 0) {
                digit = true;
            }
            ++j;
        }
        if (!digit || j + 1 >= n) {
            i = j;
            continue;
        }
        const char a = text[j] >= 'A' && text[j] <= 'Z' ? static_cast<char>(text[j] - 'A' + 'a')
                                                        : text[j];
        const char b = text[j + 1] >= 'A' && text[j + 1] <= 'Z'
                           ? static_cast<char>(text[j + 1] - 'A' + 'a')
                           : text[j + 1];
        const bool unit = (a == 'c' && b == 'h') || (a == 'e' && b == 'x') || (a == 'l' && b == 'h');
        if (unit) {
            const char after = j + 2 < n ? text[j + 2] : '\0';
            if (after == '\0' || std::isalpha(static_cast<unsigned char>(after)) == 0) {
                return true;
            }
        }
        i = j;
    }
    return false;
}

}  // namespace

bool style_needs_font_rel(const StyleMap& style) {
    for (const auto& kv : style) {
        if (value_uses_font_relative_unit(kv.second.str())) {
            return true;
        }
    }
    return false;
}

/// Measure `ch` / `ex` / `lh` for @p node at its used font size (CSS Values §6.1.1).
css::FontRelativeUnits font_rel_of(const html::Node* node) {
    css::FontRelativeUnits out;
    const float font = font_size_px(node);
    if (font <= 0.f) {
        return out;
    }
    std::string family = "sans-serif";
    if (const std::string* f = node == nullptr ? nullptr : style_literal(node, "font-family")) {
        if (!f->empty()) {
            family = *f;
        }
    }
    const int weight = font_rel_weight(node == nullptr ? nullptr : style_literal(node, "font-weight"));
    const bool italic = font_rel_italic(node == nullptr ? nullptr : style_literal(node, "font-style"));
    // The FreeType part - building a chain and measuring `0` - is memoized by the used font: the
    // same few configurations otherwise pay `FT_Init_FreeType` + `FT_New_Face` once per box, which
    // is what made a `set_model` rebuild cost more than the rest of the frame together.
    const FaceMetrics face = face_metrics_for(family, weight, italic, font);
    out.ch = face.ch;
    out.ex = face.ex;
    // Used line-height: `normal` → font box; number/percent/length as interned elsewhere.
    const float font_box = std::max(0.f, face.ascent + face.descent);
    float used_lh = font_box;
    if (const std::string* lh = node == nullptr ? nullptr : style_literal(node, "line-height")) {
        if (*lh != "normal") {
            if (!lh->empty() && lh->back() == '%') {
                used_lh = font * std::strtof(lh->c_str(), nullptr) / 100.f;
            } else {
                char* end = nullptr;
                const float num = std::strtof(lh->c_str(), &end);
                if (end != nullptr && end != lh->c_str() && *end == '\0') {
                    used_lh = num * font;
                } else {
                    const css::ResolvedLength resolved =
                        css::resolve_length(*lh, font, root_font_size_px(node),
                                            g_length_viewport.width, g_length_viewport.height, {});
                    if (resolved.valid && !resolved.is_percent) {
                        used_lh = resolved.px;
                    }
                }
            }
        }
    }
    out.lh = std::max(0.f, std::floor(used_lh * 64.f) / 64.f);
    return out;
}

void note_viewport_unit_text(std::string_view text) {
    if (g_intern_used_viewport_unit || text.empty()) {
        return;
    }
    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (!std::isdigit(c) && text[i] != '.') {
            ++i;
            continue;
        }
        while (i < text.size() &&
               (std::isdigit(static_cast<unsigned char>(text[i])) || text[i] == '.')) {
            ++i;
        }
        const std::size_t unit_begin = i;
        while (i < text.size() && std::isalpha(static_cast<unsigned char>(text[i]))) {
            ++i;
        }
        const std::size_t unit_n = i - unit_begin;
        if (unit_n == 0 || unit_n > 8) {
            continue;
        }
        char unit[8];
        for (std::size_t k = 0; k < unit_n; ++k) {
            unit[k] = static_cast<char>(
                std::tolower(static_cast<unsigned char>(text[unit_begin + k])));
        }
        if (is_viewport_unit(std::string_view(unit, unit_n))) {
            g_intern_used_viewport_unit = true;
            return;
        }
    }
}

bool intern_used_viewport_unit() { return g_intern_used_viewport_unit; }

void note_viewport_unit_used() { g_intern_used_viewport_unit = true; }

css::ResolvedLength resolve_with_viewport(std::string_view text, float font_size,
                                          float root_font_size) {
    note_viewport_unit_text(text);
    return css::resolve_length(text, font_size, root_font_size, g_length_viewport.width,
                               g_length_viewport.height, g_font_rel);
}

// justify-content and align-content share their value space (CSS Box Alignment). `normal` maps to
// stretch, which Grid reads as "share the leftover space with the auto tracks" and Flexbox reads
// as its own start.
FlexPack parse_pack(const std::string& value) {
    if (value == "flex-end" || value == "end" || value == "right") {
        return FlexPack::End;
    }
    if (value == "center") {
        return FlexPack::Center;
    }
    if (value == "space-between") {
        return FlexPack::SpaceBetween;
    }
    if (value == "space-around") {
        return FlexPack::SpaceAround;
    }
    if (value == "space-evenly") {
        return FlexPack::SpaceEvenly;
    }
    if (value == "normal" || value == "stretch") {
        return FlexPack::Stretch;
    }
    // flex-start, start, left, and anything else fall back to the start edge.
    return FlexPack::Start;
}

std::shared_ptr<const LayoutStyle> intern_style(const html::Node* node, BoxRole role,
                                                DisplayOuter outer, DisplayInner inner,
                                                Viewport viewport) {
    return intern_style_as(node, role, outer, inner, TablePart::None, viewport);
}

}  // namespace layout
}  // namespace zui
