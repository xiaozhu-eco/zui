// Media query feature evaluation (Media Queries 4 / Containment 3 size features).

#include "css/values.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#include "css/values_internal.h"
#include "css/media_internal.h"

namespace zui {
namespace css {
namespace values_detail {

bool feature_matches(std::string_view name, std::string_view value, bool boolean,
                     const MediaState& media) {
    const std::string n = lower_copy(name);
    const std::string v = lower_copy(value);
    auto dim = [&](float have, bool min_prefix, bool max_prefix, bool exact) {
        if (boolean) {
            return have != 0.0f;
        }
        Scan sc{v, 0};
        float px = 0;
        if (!parse_mq_length(sc, media, px) || !sc.eof()) {
            return false;
        }
        if (min_prefix) {
            return have + 0.01f >= px;
        }
        if (max_prefix) {
            return have <= px + 0.01f;
        }
        if (exact) {
            return std::fabs(have - px) <= 0.01f;
        }
        return false;
    };
    if (n == "width" || n == "min-width" || n == "max-width") {
        return dim(media.width, n == "min-width", n == "max-width", n == "width");
    }
    if (n == "height" || n == "min-height" || n == "max-height") {
        return dim(media.height, n == "min-height", n == "max-height", n == "height");
    }
    // CSS Containment 3 size features. Layout stores the content inline size in `width`
    // and the content block size in `height`. They are not media features.
    if (n == "inline-size" || n == "min-inline-size" || n == "max-inline-size" ||
        n == "block-size" || n == "min-block-size" || n == "max-block-size") {
        if (!media.size_query) {
            return false;
        }
        const bool block = n == "block-size" || n == "min-block-size" || n == "max-block-size";
        return dim(block ? media.height : media.width, n == "min-inline-size" || n == "min-block-size",
                   n == "max-inline-size" || n == "max-block-size",
                   n == "inline-size" || n == "block-size");
    }
    if (n == "hover" || n == "any-hover") {
        const bool can = n == "any-hover" ? media.any_hover : media.hover;
        if (boolean) {
            return can;
        }
        if (v == "hover") {
            return can;
        }
        if (v == "none") {
            return !can;
        }
        return false;
    }
    if (n == "pointer" || n == "any-pointer") {
        const PointerAccuracy have = n == "any-pointer" ? media.any_pointer : media.pointer;
        if (boolean) {
            return have != PointerAccuracy::None;
        }
        if (v == "none") {
            return have == PointerAccuracy::None;
        }
        if (v == "coarse") {
            return have == PointerAccuracy::Coarse;
        }
        if (v == "fine") {
            return have == PointerAccuracy::Fine;
        }
        return false;
    }
    if (n == "prefers-reduced-motion") {
        const bool reduce = media.reduced_motion;
        if (boolean) {
            return reduce;
        }
        if (v == "reduce") {
            return reduce;
        }
        if (v == "no-preference") {
            return !reduce;
        }
        return false;
    }
    if (n == "prefers-color-scheme") {
        const ColorSchemePreference scheme = media.color_scheme;
        if (boolean) {
            return scheme == ColorSchemePreference::Light || scheme == ColorSchemePreference::Dark;
        }
        if (v == "dark") {
            return scheme == ColorSchemePreference::Dark;
        }
        if (v == "light") {
            return scheme == ColorSchemePreference::Light;
        }
        if (v == "no-preference") {
            return scheme == ColorSchemePreference::NoPreference;
        }
        return false;
    }
    // Media Queries 4 <ratio>: width/height. A bare number is that number over 1.
    // Zero on either side is not a finite ratio, so the comparison does not match.
    if (n == "aspect-ratio" || n == "min-aspect-ratio" || n == "max-aspect-ratio") {
        if (boolean) {
            return media.width > 0.0f && media.height > 0.0f;
        }
        float num = 0.0f;
        float den = 1.0f;
        if (!parse_ratio_text(v, num, den)) {
            return false;
        }
        const MfCmp cmp = n == "min-aspect-ratio" ? MfCmp::Ge
                          : n == "max-aspect-ratio" ? MfCmp::Le
                                                    : MfCmp::Eq;
        return relate_ratio(cmp, media.width, media.height, num, den);
    }
    // Media Queries 4 resolution. `1dppx` = `1x` = `96dpi`. Not a container size feature.
    if (n == "resolution" || n == "min-resolution" || n == "max-resolution") {
        if (media.size_query) {
            return false;
        }
        if (boolean) {
            return media.resolution_dppx > 0.0f;
        }
        float dppx = 0.0f;
        if (!parse_resolution_text(v, dppx)) {
            return false;
        }
        const MfCmp cmp = n == "min-resolution" ? MfCmp::Ge
                          : n == "max-resolution" ? MfCmp::Le
                                                  : MfCmp::Eq;
        return relate_mf(cmp, media.resolution_dppx, dppx);
    }
    // Media Queries 4 color index. A screen is 8 bits per component. `monochrome` is 0 on
    // that screen, so the boolean query does not match and `(monochrome: 0)` does.
    // `scripting` is `enabled` (Edge reports that even when the page has no script).
    // None of these are container size features.
    if (n == "color" || n == "min-color" || n == "max-color" || n == "monochrome" ||
        n == "min-monochrome" || n == "max-monochrome") {
        if (media.size_query) {
            return false;
        }
        const bool mono = n.find("monochrome") != std::string::npos;
        const float have = mono ? 0.f : 8.f;
        if (boolean) {
            return have > 0.f;
        }
        float want = 0.f;
        if (!parse_mq_integer(v, want)) {
            return false;
        }
        if (n.compare(0, 4, "min-") == 0) {
            return have + 0.01f >= want;
        }
        if (n.compare(0, 4, "max-") == 0) {
            return have <= want + 0.01f;
        }
        return std::fabs(have - want) <= 0.01f;
    }
    if (n == "scripting") {
        if (media.size_query) {
            return false;
        }
        if (boolean) {
            return true;
        }
        if (v == "enabled") {
            return true;
        }
        if (v == "none" || v == "initial-only") {
            return false;
        }
        return false;
    }
    // Media Queries 5, for a normal browser window (not a container size query).
    // display-mode is `browser`. update is `fast` (a screen can animate). overflow-block
    // and overflow-inline are `scroll`. grid is 0 on a bitmap. Boolean context is false
    // for the keyword `none` and for the number 0; display-mode has neither, so it matches.
    // These features are discrete: min-/max- prefixes are unknown names, and a range
    // comparison is rejected in eval_axis_cmp.
    if (n == "display-mode" || n == "update" || n == "overflow-block" || n == "overflow-inline" ||
        n == "grid") {
        if (media.size_query) {
            return false;
        }
        if (n == "grid") {
            // Boolean `(grid)` is true only for a grid device (value 1).
            if (boolean) {
                return false;
            }
            float want = 0.f;
            if (!parse_mq_integer(v, want) || (want != 0.f && want != 1.f)) {
                return false;
            }
            return want == 0.f;
        }
        if (n == "display-mode") {
            if (boolean) {
                return true;
            }
            return v == "browser";
        }
        if (n == "update") {
            if (boolean) {
                return true;
            }
            return v == "fast";
        }
        // overflow-block and overflow-inline.
        if (boolean) {
            return true;
        }
        return v == "scroll";
    }
    // Media Queries 4: portrait when height >= width, otherwise landscape. A square
    // viewport is portrait. The feature always applies on screen, so a boolean query matches.
    if (n == "orientation") {
        const bool portrait = media.height >= media.width;
        if (boolean) {
            return true;
        }
        if (v == "portrait") {
            return portrait;
        }
        if (v == "landscape") {
            return !portrait;
        }
        return false;
    }
    return false;
}

// Same 0.01px slack as min-width / max-width, so `(width >= 700px)` agrees with
// `(min-width: 700px)` and a strict inequality is the complement.
bool relate_mf(MfCmp cmp, float left, float right) {
    switch (cmp) {
    case MfCmp::Gt:
        return left > right + 0.01f;
    case MfCmp::Ge:
        return left + 0.01f >= right;
    case MfCmp::Lt:
        return left + 0.01f < right;
    case MfCmp::Le:
        return left <= right + 0.01f;
    case MfCmp::Eq:
        return std::fabs(left - right) <= 0.01f;
    }
    return false;
}

// Cross-multiply so 800/600 equals 4/3 and 8/6. A non-positive denominator is not a ratio.
bool relate_ratio(MfCmp cmp, float left_num, float left_den, float right_num, float right_den) {
    if (!(left_den > 0.0f) || !(right_den > 0.0f) || left_num < 0.0f || right_num < 0.0f) {
        return false;
    }
    const double left = static_cast<double>(left_num) * static_cast<double>(right_den);
    const double right = static_cast<double>(right_num) * static_cast<double>(left_den);
    const double scale = std::max(std::abs(left), std::abs(right));
    const double slack = 1e-4 * std::max(1.0, scale);
    switch (cmp) {
    case MfCmp::Gt:
        return left > right + slack;
    case MfCmp::Ge:
        return left + slack >= right;
    case MfCmp::Lt:
        return left + slack < right;
    case MfCmp::Le:
        return left <= right + slack;
    case MfCmp::Eq:
        return std::abs(left - right) <= slack;
    }
    return false;
}

// `feature_on_left` is the name-first form (`aspect-ratio >= 16/9`). The other way is
// value-first (`16/9 <= aspect-ratio`). A known axis with the wrong bound type fails the query.
// An unknown axis with a parsed bound does not match.
bool eval_axis_cmp(std::string_view name, MfCmp cmp, bool feature_on_left, const MfBound& bound,
                    const MediaState& media, bool& value) {
    const std::string n = lower_copy(name);
    if (n == "width" || n == "height" || n == "inline-size" || n == "block-size") {
        // inline-size / block-size exist only on a container. In a media query they are
        // unknown, so the comparison does not match and `not` can still invert it.
        if ((n == "inline-size" || n == "block-size") && !media.size_query) {
            value = false;
            return true;
        }
        float px = 0.0f;
        if (bound.kind == MfBound::Kind::Length) {
            px = bound.px;
        } else if (bound.kind == MfBound::Kind::Bare && bound.num == 0.0f) {
            px = 0.0f;
        } else {
            return false;
        }
        const float have = (n == "width" || n == "inline-size") ? media.width : media.height;
        value = feature_on_left ? relate_mf(cmp, have, px) : relate_mf(cmp, px, have);
        return true;
    }
    if (n == "aspect-ratio") {
        if (bound.kind != MfBound::Kind::Ratio && bound.kind != MfBound::Kind::Bare) {
            return false;
        }
        if (feature_on_left) {
            value = relate_ratio(cmp, media.width, media.height, bound.num, bound.den);
        } else {
            value = relate_ratio(cmp, bound.num, bound.den, media.width, media.height);
        }
        return true;
    }
    if (n == "resolution") {
        // Container queries only answer size features. A resolution query does not match there.
        if (media.size_query) {
            value = false;
            return true;
        }
        if (bound.kind != MfBound::Kind::Resolution) {
            return false;
        }
        value = feature_on_left ? relate_mf(cmp, media.resolution_dppx, bound.px)
                                : relate_mf(cmp, bound.px, media.resolution_dppx);
        return true;
    }
    if (n == "color" || n == "monochrome") {
        if (media.size_query) {
            value = false;
            return true;
        }
        if (bound.kind != MfBound::Kind::Bare || bound.num < 0.f ||
            std::fabs(bound.num - std::round(bound.num)) > 0.001f) {
            return false;
        }
        const float have = n == "color" ? 8.f : 0.f;
        value = feature_on_left ? relate_mf(cmp, have, bound.num)
                                : relate_mf(cmp, bound.num, have);
        return true;
    }
    // Discrete. A range comparison is a syntax error, so `not` does not invert it.
    if (n == "display-mode" || n == "update" || n == "overflow-block" || n == "overflow-inline" ||
        n == "grid") {
        return false;
    }
    value = false;
    return true;
}

}  // namespace values_detail
}  // namespace css
}  // namespace zui
