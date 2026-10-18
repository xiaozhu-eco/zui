#include "layout/box_tree/detail.h"

#include "css/values.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace zui {
namespace layout {
namespace box_tree_detail {
namespace {

bool is_ws(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

std::string_view trim_sv(std::string_view s) {
    while (!s.empty() && is_ws(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && is_ws(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

std::string lower_copy(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

std::string_view attr_of(const html::Node* node, const char* key) {
    if (node == nullptr) {
        return {};
    }
    const auto it = node->attrs.find(key);
    if (it == node->attrs.end()) {
        return {};
    }
    return it->second;
}

/// Decoders this engine can paint. An unsupported `type` is skipped so a later PNG/JPEG/SVG
/// candidate still shows. WebP and AVIF are not in that set.
bool image_mime_supported(std::string_view type) {
    type = trim_sv(type);
    const size_t semi = type.find(';');
    if (semi != std::string_view::npos) {
        type = trim_sv(type.substr(0, semi));
    }
    const std::string mime = lower_copy(type);
    return mime == "image/png" || mime == "image/jpeg" || mime == "image/jpg" ||
           mime == "image/svg+xml" || mime == "image/svg";
}

struct Candidate {
    std::string url;
    bool has_width = false;
    float width = 0.f;
    bool has_density = false;
    float density = 1.f;
};

bool parse_w_descriptor(std::string_view num, float& width) {
    if (num.empty()) {
        return false;
    }
    for (char c : num) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    char* end = nullptr;
    const std::string text(num);
    const long value = std::strtol(text.c_str(), &end, 10);
    if (end == nullptr || end == text.c_str() || *end != '\0' || value <= 0) {
        return false;
    }
    width = static_cast<float>(value);
    return true;
}

bool parse_x_descriptor(std::string_view num, float& density) {
    if (num.empty()) {
        return false;
    }
    char* end = nullptr;
    const std::string text(num);
    const float value = std::strtof(text.c_str(), &end);
    if (end == nullptr || end == text.c_str() || *end != '\0' || !(value > 0.f)) {
        return false;
    }
    density = value;
    return true;
}

bool parse_h_descriptor(std::string_view num) {
    float ignored = 0.f;
    return parse_w_descriptor(num, ignored);
}

/// HTML "parse a srcset attribute". A descriptor that fails drops that candidate only.
std::vector<Candidate> parse_srcset(std::string_view input) {
    std::vector<Candidate> out;
    size_t i = 0;
    const size_t n = input.size();
    while (i < n) {
        while (i < n && (is_ws(input[i]) || input[i] == ',')) {
            ++i;
        }
        if (i >= n) {
            break;
        }
        const size_t url_start = i;
        while (i < n && !is_ws(input[i])) {
            ++i;
        }
        std::string url(input.substr(url_start, i - url_start));
        bool url_ended_comma = false;
        while (!url.empty() && url.back() == ',') {
            url.pop_back();
            url_ended_comma = true;
        }
        std::vector<std::string> descriptors;
        if (!url_ended_comma) {
            std::string current;
            bool in_parens = false;
            while (i < n && input[i] != ',') {
                const char c = input[i];
                if (in_parens) {
                    current.push_back(c);
                    if (c == ')') {
                        in_parens = false;
                    }
                    ++i;
                    continue;
                }
                if (is_ws(c)) {
                    if (!current.empty()) {
                        descriptors.push_back(current);
                        current.clear();
                    }
                    ++i;
                    continue;
                }
                if (c == '(') {
                    current.push_back(c);
                    in_parens = true;
                    ++i;
                    continue;
                }
                current.push_back(c);
                ++i;
            }
            if (!current.empty()) {
                descriptors.push_back(current);
            }
            if (i < n && input[i] == ',') {
                ++i;
            }
        }
        if (url.empty()) {
            continue;
        }
        Candidate candidate;
        candidate.url = std::move(url);
        bool bad = false;
        bool has_h = false;
        for (const std::string& descriptor : descriptors) {
            if (descriptor.empty()) {
                bad = true;
                break;
            }
            char unit = descriptor.back();
            if (unit >= 'A' && unit <= 'Z') {
                unit = static_cast<char>(unit - 'A' + 'a');
            }
            const std::string_view num(descriptor.data(), descriptor.size() - 1);
            if (unit == 'w') {
                float width = 0.f;
                if (candidate.has_width || candidate.has_density || !parse_w_descriptor(num, width)) {
                    bad = true;
                    break;
                }
                candidate.has_width = true;
                candidate.width = width;
            } else if (unit == 'x') {
                float density = 0.f;
                if (candidate.has_width || candidate.has_density || has_h ||
                    !parse_x_descriptor(num, density)) {
                    bad = true;
                    break;
                }
                candidate.has_density = true;
                candidate.density = density;
            } else if (unit == 'h') {
                if (has_h || candidate.has_density || !parse_h_descriptor(num)) {
                    bad = true;
                    break;
                }
                has_h = true;
            } else {
                bad = true;
                break;
            }
        }
        if (has_h && !candidate.has_width) {
            bad = true;
        }
        if (!bad) {
            out.push_back(std::move(candidate));
        }
    }
    return out;
}

bool is_math_fn(std::string_view name) {
    const std::string n = lower_copy(name);
    return n == "calc" || n == "min" || n == "max" || n == "clamp";
}

/// Split one sizes entry into an optional media condition and the trailing length (or `auto`).
bool peel_source_size(std::string_view item, std::string_view& condition, std::string_view& length) {
    item = trim_sv(item);
    if (item.empty()) {
        return false;
    }
    if (item.back() == ')') {
        int depth = 0;
        size_t open = std::string_view::npos;
        for (size_t i = item.size(); i-- > 0;) {
            if (item[i] == ')') {
                ++depth;
            } else if (item[i] == '(') {
                --depth;
                if (depth == 0) {
                    open = i;
                    break;
                }
            }
        }
        if (open == std::string_view::npos || open == 0) {
            return false;
        }
        size_t name_start = open;
        while (name_start > 0 &&
               ((item[name_start - 1] >= 'A' && item[name_start - 1] <= 'Z') ||
                (item[name_start - 1] >= 'a' && item[name_start - 1] <= 'z') ||
                item[name_start - 1] == '-')) {
            --name_start;
        }
        if (name_start == open || !is_math_fn(item.substr(name_start, open - name_start))) {
            return false;
        }
        length = trim_sv(item.substr(name_start));
        condition = trim_sv(item.substr(0, name_start));
        return !length.empty();
    }
    const size_t sp = item.find_last_of(" \t\n\r\f");
    if (sp == std::string_view::npos) {
        length = item;
        condition = {};
        return true;
    }
    length = trim_sv(item.substr(sp + 1));
    condition = trim_sv(item.substr(0, sp));
    return !length.empty();
}

/// HTML "parse a sizes attribute". Negative means the length did not resolve (viewport still 0).
float parse_sizes(std::string_view sizes, float viewport_w, float viewport_h, float dpr) {
    css::MediaState media;
    media.width = viewport_w;
    media.height = viewport_h;
    media.resolution_dppx = dpr > 0.f ? dpr : 1.f;
    size_t start = 0;
    int depth = 0;
    auto consider = [&](std::string_view item) -> float {
        std::string_view condition;
        std::string_view length;
        if (!peel_source_size(item, condition, length)) {
            return -2.f;
        }
        if (lower_copy(length) == "auto") {
            return -2.f;
        }
        const css::ResolvedLength resolved =
            css::resolve_length(length, 16.f, 16.f, viewport_w, viewport_h);
        if (!resolved.valid || resolved.is_percent || resolved.px < 0.f) {
            return -2.f;
        }
        if (!condition.empty() && !css::media_matches(condition, media)) {
            return -2.f;
        }
        return resolved.px;
    };
    auto next_item = [&](size_t from, size_t to) -> float {
        return consider(sizes.substr(from, to - from));
    };
    for (size_t i = 0; i < sizes.size(); ++i) {
        const char c = sizes[i];
        if (c == '(') {
            ++depth;
        } else if (c == ')') {
            if (depth > 0) {
                --depth;
            }
        } else if (c == ',' && depth == 0) {
            const float got = next_item(start, i);
            if (got >= 0.f) {
                return got;
            }
            start = i + 1;
        }
    }
    const float tail = next_item(start, sizes.size());
    if (tail >= 0.f) {
        return tail;
    }
    const css::ResolvedLength vw = css::resolve_length("100vw", 16.f, 16.f, viewport_w, viewport_h);
    if (!vw.valid || vw.is_percent || vw.px < 0.f) {
        return -1.f;
    }
    return vw.px;
}

/// Blink picks the density closest to the device pixel ratio. An exact tie keeps the higher one,
/// which is the candidate that is still at least the device ratio when both distances match.
std::string pick_density(std::vector<Candidate> candidates, float dpr) {
    if (!(dpr > 0.f)) {
        dpr = 1.f;
    }
    std::vector<Candidate> unique;
    unique.reserve(candidates.size());
    for (Candidate& candidate : candidates) {
        bool duplicate = false;
        for (const Candidate& kept : unique) {
            const float scale = std::max(1.f, std::max(std::fabs(kept.density), std::fabs(candidate.density)));
            if (std::fabs(kept.density - candidate.density) <= 1e-4f * scale) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            unique.push_back(std::move(candidate));
        }
    }
    if (unique.empty()) {
        return {};
    }
    std::stable_sort(unique.begin(), unique.end(),
                     [](const Candidate& a, const Candidate& b) { return a.density < b.density; });
    size_t winner = unique.size() - 1;
    for (size_t i = 0; i < unique.size(); ++i) {
        if (unique[i].density + 1e-4f >= dpr) {
            winner = i;
            break;
        }
    }
    if (winner > 0) {
        const float higher = unique[winner].density;
        const float lower = unique[winner - 1].density;
        if ((dpr - lower) < (higher - dpr)) {
            winner = winner - 1;
        }
    }
    return unique[winner].url;
}

std::string select_from_attributes(std::string_view src, std::string_view srcset,
                                   std::string_view sizes, float viewport_w, float viewport_h,
                                   float dpr) {
    std::vector<Candidate> candidates = parse_srcset(srcset);
    bool any_width = false;
    bool has_density_one = false;
    for (const Candidate& candidate : candidates) {
        any_width = any_width || candidate.has_width;
        if (candidate.has_density && std::fabs(candidate.density - 1.f) <= 1e-4f) {
            has_density_one = true;
        }
    }
    const std::string_view fallback = trim_sv(src);
    if (!fallback.empty() && !any_width && !has_density_one) {
        Candidate extra;
        extra.url.assign(fallback.begin(), fallback.end());
        candidates.push_back(std::move(extra));
    }
    if (candidates.empty()) {
        return std::string(fallback);
    }
    if (any_width) {
        const float source_size = parse_sizes(sizes, viewport_w, viewport_h, dpr);
        if (!(source_size > 0.f)) {
            return std::string(fallback);
        }
        for (Candidate& candidate : candidates) {
            if (candidate.has_density) {
                continue;
            }
            if (candidate.has_width) {
                candidate.density = candidate.width / source_size;
                candidate.has_density = true;
            } else {
                candidate.density = 1.f;
                candidate.has_density = true;
            }
        }
    } else {
        for (Candidate& candidate : candidates) {
            if (!candidate.has_density) {
                candidate.density = 1.f;
                candidate.has_density = true;
            }
        }
    }
    return pick_density(std::move(candidates), dpr);
}

}  // namespace

std::string select_img_url(const html::Node* img, float viewport_width, float viewport_height,
                           float device_pixel_ratio) {
    if (img == nullptr || img->tag != "img") {
        return {};
    }
    const float dpr = device_pixel_ratio > 0.f ? device_pixel_ratio : 1.f;
    if (img->parent != nullptr && img->parent->tag == "picture") {
        for (const auto& child : img->parent->children) {
            if (child.get() == img) {
                break;
            }
            if (child == nullptr || child->tag != "source") {
                continue;
            }
            const std::string_view srcset = attr_of(child.get(), "srcset");
            if (trim_sv(srcset).empty()) {
                continue;
            }
            const std::string_view media = trim_sv(attr_of(child.get(), "media"));
            if (!media.empty()) {
                css::MediaState state;
                state.width = viewport_width;
                state.height = viewport_height;
                state.resolution_dppx = dpr;
                if (!css::media_matches(media, state)) {
                    continue;
                }
            }
            const std::string_view type = attr_of(child.get(), "type");
            if (!trim_sv(type).empty() && !image_mime_supported(type)) {
                continue;
            }
            const std::string chosen = select_from_attributes(
                {}, srcset, attr_of(child.get(), "sizes"), viewport_width, viewport_height, dpr);
            if (!chosen.empty()) {
                return chosen;
            }
        }
    }
    return select_from_attributes(attr_of(img, "src"), attr_of(img, "srcset"), attr_of(img, "sizes"),
                                  viewport_width, viewport_height, dpr);
}

}  // namespace box_tree_detail
}  // namespace layout
}  // namespace zui
