#include "ascii.h"
#include "paint/painter_internal.h"

#include "css/image_set.h"

#include "paint/display_list.h"

#include "log/log.h"
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
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace zui {
namespace paint {
namespace painter_detail {
bool has_top_level_comma(std::string_view s) {
    int depth = 0;
    char quote = 0;
    for (char c : s) {
        if (quote != 0) {
            quote = c == quote ? 0 : quote;
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '(') {
            ++depth;
        } else if (c == ')') {
            depth = depth > 0 ? depth - 1 : 0;
        } else if (c == ',' && depth == 0) {
            return true;
        }
    }
    return false;
}

std::string lower_ascii(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

BgBoxKind parse_bg_clip(const std::string* v) {
    if (v == nullptr) {
        return BgBoxKind::Border;
    }
    std::string_view raw(*v);
    while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.front()))) {
        raw.remove_prefix(1);
    }
    while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.back()))) {
        raw.remove_suffix(1);
    }
    const std::string l = lower_ascii(raw);
    if (l == "padding-box") {
        return BgBoxKind::Padding;
    }
    if (l == "content-box") {
        return BgBoxKind::Content;
    }
    if (l == "text") {
        return BgBoxKind::Text;
    }
    return BgBoxKind::Border;
}

BgBoxKind parse_bg_origin(const std::string* v) {
    if (v == nullptr) {
        return BgBoxKind::Padding;
    }
    const std::string l = lower_ascii(*v);
    if (l == "border-box") {
        return BgBoxKind::Border;
    }
    if (l == "content-box") {
        return BgBoxKind::Content;
    }
    return BgBoxKind::Padding;
}

Rect bg_box_rect(const layout::Box& box, float dx, float dy, BgBoxKind kind) {
    switch (kind) {
        case BgBoxKind::Padding:
            return padding_rect(box, dx, dy);
        case BgBoxKind::Content:
            return content_rect(box, dx, dy);
        case BgBoxKind::Text:
            // Glyph coverage clips the layer. The geometric paint stays the border box.
        case BgBoxKind::Border:
        default:
            return border_rect(box, dx, dy);
    }
}

CornerRadii bg_box_radii(const layout::Box& box, const CornerRadii& border_radii, BgBoxKind kind) {
    if (kind == BgBoxKind::Border || kind == BgBoxKind::Text) {
        return border_radii;
    }
    CornerRadii r = inset_radii_sides(border_radii, box.geo.border.top, box.geo.border.right,
                                      box.geo.border.bottom, box.geo.border.left);
    if (kind == BgBoxKind::Content) {
        r = inset_radii_sides(r, box.geo.padding.top, box.geo.padding.right, box.geo.padding.bottom,
                              box.geo.padding.left);
    }
    return r;
}

std::string_view trim_sv_local(std::string_view s) { return zui::trim_sv(s); }

bool parse_bg_length(std::string_view tok, float basis, float& out_px, bool& unparseable) {
    tok = trim_sv_local(tok);
    if (tok.empty()) {
        unparseable = true;
        return false;
    }
    if (tok == "0") {
        out_px = 0;
        return true;
    }
    char* end = nullptr;
    const std::string tmp(tok);
    const float v = std::strtof(tmp.c_str(), &end);
    if (end == tmp.c_str()) {
        unparseable = true;
        return false;
    }
    std::string_view unit(end);
    while (!unit.empty() && std::isspace(static_cast<unsigned char>(unit.front()))) {
        unit.remove_prefix(1);
    }
    if (unit.empty() || unit == "px") {
        out_px = v;
        return true;
    }
    if (unit == "%") {
        out_px = basis * v / 100.f;
        return true;
    }
    // em/rem/vh/calc/… — not resolved here; caller must refuse to paint.
    unparseable = true;
    return false;
}

/// Single-layer `url(...)` only. Rejects data/remote and multi-layer comma lists (caller guards
/// commas separately). Accepts `url(path)`, `url("path")`, and `url('path')`.
bool parse_bg_url(std::string_view image, std::string& path_out) {
    image = trim_sv_local(image);
    if (image.size() < 6 || image.back() != ')') {
        return false;
    }
    const char* prefix = "url(";
    for (size_t i = 0; i < 4; ++i) {
        if (std::tolower(static_cast<unsigned char>(image[i])) !=
            static_cast<unsigned char>(prefix[i])) {
            return false;
        }
    }
    std::string_view inner = trim_sv_local(image.substr(4, image.size() - 5));
    if (inner.empty()) {
        return false;
    }
    if ((inner.front() == '"' || inner.front() == '\'') && inner.size() >= 2 &&
        inner.back() == inner.front()) {
        inner = inner.substr(1, inner.size() - 2);
    }
    inner = trim_sv_local(inner);
    if (inner.empty() || inner.find("://") != std::string_view::npos) {
        return false;
    }
    path_out.assign(inner.begin(), inner.end());
    return true;
}

bool is_paintable_bg_url(std::string_view image) {
    std::string path;
    if (parse_bg_url(image, path)) {
        return true;
    }
    // `image-set()` resolves to a url (or a gradient) in `paint_one_background_image`. Callers
    // only enter that path when this is true.
    return zui::css::is_image_set(image);
}

std::string resolve_paint_resource(const std::string& src, const std::string& resource_base) {
    if (src.empty()) {
        return {};
    }
    const std::filesystem::path path(src);
    if (path.is_absolute() || resource_base.empty()) {
        return src;
    }
    return (std::filesystem::path(resource_base) / path).lexically_normal().string();
}

bool starts_with_ci(std::string_view image, std::string_view prefix) {
    return zui::starts_with_ci(image, prefix);
}

bool is_paintable_gradient_image(std::string_view image) {
    image = trim_sv_local(image);
    if (image.size() < 8 || image.back() != ')' || has_top_level_comma(image)) {
        return false;
    }
    return starts_with_ci(image, "linear-gradient(") ||
           starts_with_ci(image, "repeating-linear-gradient(") ||
           starts_with_ci(image, "radial-gradient(") ||
           starts_with_ci(image, "repeating-radial-gradient(") ||
           starts_with_ci(image, "conic-gradient(") ||
           starts_with_ci(image, "repeating-conic-gradient(");
}

bool is_paintable_linear_gradient(std::string_view image) {
    return is_paintable_gradient_image(image) &&
           starts_with_ci(trim_sv_local(image), "linear-gradient(");
}

std::vector<std::string> split_bg_list(std::string_view value) {
    std::vector<std::string> out;
    size_t start = 0;
    int depth = 0;
    char quote = 0;
    auto push = [&](size_t from, size_t to) {
        out.emplace_back(trim_sv_local(value.substr(from, to - from)));
    };
    for (size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (quote != 0) {
            quote = c == quote ? 0 : quote;
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '(' || c == '[') {
            ++depth;
        } else if (c == ')' || c == ']') {
            depth = depth > 0 ? depth - 1 : 0;
        } else if (c == ',' && depth == 0) {
            push(start, i);
            start = i + 1;
        }
    }
    push(start, value.size());
    return out;
}

MixBlendMode blend_mode_of(std::string_view value) {
    const std::string token = lower_ascii(trim_sv_local(value));
    if (token == "multiply") {
        return MixBlendMode::Multiply;
    }
    if (token == "screen") {
        return MixBlendMode::Screen;
    }
    if (token == "overlay") {
        return MixBlendMode::Overlay;
    }
    if (token == "darken") {
        return MixBlendMode::Darken;
    }
    if (token == "lighten") {
        return MixBlendMode::Lighten;
    }
    if (token == "color-dodge") {
        return MixBlendMode::ColorDodge;
    }
    if (token == "color-burn") {
        return MixBlendMode::ColorBurn;
    }
    if (token == "hard-light") {
        return MixBlendMode::HardLight;
    }
    if (token == "soft-light") {
        return MixBlendMode::SoftLight;
    }
    if (token == "difference") {
        return MixBlendMode::Difference;
    }
    if (token == "exclusion") {
        return MixBlendMode::Exclusion;
    }
    return MixBlendMode::Normal;
}

std::string bg_list_item(const std::string* prop, size_t index, const char* fallback) {
    if (prop == nullptr || prop->empty()) {
        return fallback;
    }
    const std::vector<std::string> parts = split_bg_list(*prop);
    if (parts.empty()) {
        return fallback;
    }
    const std::string& item = parts[index % parts.size()];
    return item.empty() ? std::string(fallback) : item;
}

BgBoxKind clip_kind_of(std::string_view value) {
    const std::string stored(value);
    return parse_bg_clip(&stored);
}

BgBoxKind origin_kind_of(std::string_view value) {
    const std::string stored(value);
    return parse_bg_origin(&stored);
}

std::vector<BgPaintLayer> background_layers(const std::string* image, const std::string* position,
                                            const std::string* size, const std::string* repeat,
                                            const std::string* attachment, const std::string* clip,
                                            const std::string* origin) {
    std::vector<BgPaintLayer> layers;
    if (image == nullptr || trim_sv_local(*image).empty()) {
        return layers;
    }
    const std::vector<std::string> images = split_bg_list(*image);
    layers.reserve(images.size());
    for (size_t i = 0; i < images.size(); ++i) {
        BgPaintLayer layer;
        layer.image = images[i];
        layer.position = bg_list_item(position, i, "0% 0%");
        layer.size = bg_list_item(size, i, "auto");
        layer.repeat = bg_list_item(repeat, i, "repeat");
        layer.attachment = bg_list_item(attachment, i, "scroll");
        layer.clip = clip_kind_of(bg_list_item(clip, i, "border-box"));
        layer.origin = origin_kind_of(bg_list_item(origin, i, "padding-box"));
        layers.push_back(std::move(layer));
    }
    return layers;
}

void apply_background_blend(std::vector<BgPaintLayer>& layers, const std::string* blend) {
    if (blend == nullptr || layers.empty()) {
        return;
    }
    for (size_t i = 0; i < layers.size(); ++i) {
        layers[i].blend = blend_mode_of(bg_list_item(blend, i, "normal"));
    }
}

}  // namespace painter_detail
}  // namespace paint
}  // namespace zui
