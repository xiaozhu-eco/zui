#include "paint/color_internal.h"
#include "paint/display_list.h"
#include "paint/painter_internal.h"

#include "ascii.h"
#include "css/values.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace zui {
namespace paint {
using namespace color_detail;

bool linear_gradient_for_box(std::string_view css, Rect area, Gradient& out,
                             const layout::Box* color_box) {
    css = trim(css);
    constexpr std::string_view kFn = "linear-gradient(";
    if (css.size() <= kFn.size() + 1 || css.back() != ')' ||
        !equals_ci(css.substr(0, kFn.size()), kFn)) {
        return false;
    }
    const std::vector<std::string_view> args =
        split_args(css.substr(kFn.size(), css.size() - kFn.size() - 1));
    if (args.size() < 2) {
        return false;
    }

    // Direction: a `to <side|corner>` keyword or an angle. `linear-gradient(a, b)` points at the
    // bottom edge (CSS Images 3 section 3.4.2). CSS Images 4 lets that slot also carry
    // `in <space>` (`to right in oklch`, `in hsl longer hue`). No method keeps premultiplied sRGB.
    float angle = 180.0f;
    size_t first_stop = 0;
    std::string method;
    const std::string_view head = trim(args[0]);
    std::vector<std::string> head_toks;
    tokenize_ws(head, head_toks);
    if (!take_interpolation_method(head_toks, method)) {
        return false;
    }
    std::string direction;
    for (const std::string& tok : head_toks) {
        if (!direction.empty()) {
            direction.push_back(' ');
        }
        direction.append(tok);
    }
    if (!method.empty()) {
        first_stop = 1;
    }
    if (!direction.empty() &&
        lowered(direction.substr(0, direction.size() < 3 ? direction.size() : 3)) == "to ") {
        const std::string where = lowered(direction.substr(3));
        // A corner keyword puts the line perpendicular to the line joining the two neighbouring
        // corners, which is what makes a 50% stop intersect them (section 3.4.2). For a w x h box
        // that direction is (h, w), so the angle comes from atan2(h, w) and the four quadrants
        // mirror it.
        const float diag = std::atan2(area.h, area.w) * 180.0f / kPi;
        const bool top = where.find("top") != std::string::npos;
        const bool bottom = where.find("bottom") != std::string::npos;
        const bool left = where.find("left") != std::string::npos;
        const bool right = where.find("right") != std::string::npos;
        if (top && right) {
            angle = diag;
        } else if (bottom && right) {
            angle = 180.0f - diag;
        } else if (bottom && left) {
            angle = 180.0f + diag;
        } else if (top && left) {
            angle = 360.0f - diag;
        } else if (top) {
            angle = 0.0f;
        } else if (right) {
            angle = 90.0f;
        } else if (bottom) {
            angle = 180.0f;
        } else if (left) {
            angle = 270.0f;
        } else {
            return false;
        }
        first_stop = 1;
    } else if (!direction.empty()) {
        float deg = 0;
        if (parse_angle(direction, deg)) {
            angle = deg;
            first_stop = 1;
        } else if (!method.empty()) {
            return false;
        }
    }

    const float radians = angle * kPi / 180.0f;
    const float dx = std::sin(radians);
    const float dy = -std::cos(radians);  // device space grows downward
    // The line runs through the centre and its ends are the projections of the corners onto it,
    // so its length is |w sin| + |h cos|.
    const float len = std::fabs(area.w * dx) + std::fabs(area.h * dy);

    struct Raw {
        Color color;
        bool has_position = false;
        float position = 0;
    };
    // A hint sits after the colour stop at `after` (CSS Images 3: the midpoint colour lands there).
    std::vector<Raw> raw;
    std::vector<GradientHint> hints;
    for (size_t i = first_stop; i < args.size(); ++i) {
        std::vector<std::string> toks;
        tokenize_ws(args[i], toks);
        if (toks.empty()) {
            continue;
        }
        float hint_pos = 0;
        if (toks.size() == 1 && parse_stop_position(toks[0], len, hint_pos)) {
            if (!raw.empty()) {
                hints.push_back(GradientHint{raw.size() - 1, hint_pos});
            }
            continue;
        }
        // CSS Images 3 §3.4.1: one color plus one or two positions. Two positions are two stops
        // of the same color (`#e23 0% 50%`), which is how stripes get a hard edge.
        float positions[2] = {0.f, 0.f};
        int npos = 0;
        for (size_t t = 1; t < toks.size() && npos < 2; ++t) {
            float fraction = 0;
            if (!parse_stop_position(toks[t], len, fraction)) {
                break;
            }
            positions[npos++] = fraction;
        }
        if (npos == 0) {
            raw.push_back(Raw{resolve_gradient_stop(toks[0], color_box), false, 0});
        } else {
            const Color color = resolve_gradient_stop(toks[0], color_box);
            for (int p = 0; p < npos; ++p) {
                raw.push_back(Raw{color, true, positions[p]});
            }
        }
    }
    if (raw.size() < 2) {
        return false;
    }

    std::vector<GradientStop> stops;
    stops.reserve(raw.size());
    for (const Raw& r : raw) {
        stops.push_back(GradientStop{r.has_position ? r.position : 0.0f, r.color});
    }
    // Section 3.4.3: a missing first position is 0, a missing last one is 1, and a run of missing
    // positions between two stated ones spreads evenly.
    if (!raw.front().has_position) {
        stops.front().offset = 0.0f;
        raw.front().has_position = true;
    }
    if (!raw.back().has_position) {
        stops.back().offset = 1.0f;
        raw.back().has_position = true;
    }
    for (size_t i = 0; i < stops.size();) {
        if (raw[i].has_position) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < stops.size() && !raw[j].has_position) {
            ++j;
        }
        const float from = stops[i - 1].offset;
        const float to = j < stops.size() ? stops[j].offset : 1.0f;
        const float step = (to - from) / static_cast<float>(j - i + 1);
        for (size_t k = i; k < j; ++k) {
            stops[k].offset = from + step * static_cast<float>(k - i + 1);
        }
        i = j;
    }
    for (size_t i = 1; i < stops.size(); ++i) {
        stops[i].offset = std::max(stops[i].offset, stops[i - 1].offset);
    }
    if (!method.empty()) {
        // Specified space replaces the legacy nine-stop sRGB expansion. Hints still bend the
        // weight, but the mix itself is color-mix in that space.
        resample_in_method(stops, hints, method);
    } else if (!hints.empty()) {
        expand_srgb_hints(stops, hints);
    }

    out = Gradient{};
    out.kind = GradientKind::Linear;
    out.repeating = false;
    out.x = area.x + area.w * 0.5f - dx * len * 0.5f;
    out.y = area.y + area.h * 0.5f - dy * len * 0.5f;
    out.dx = dx;
    out.dy = dy;
    out.len = len;
    out.stops = std::move(stops);
    return true;
}

bool repeating_linear_gradient_for_box(std::string_view css, Rect area, Gradient& out,
                                       const layout::Box* color_box) {
    // Reuse linear parsing by rewriting the function name, then mark repeating.
    css = trim(css);
    constexpr std::string_view kFn = "repeating-linear-gradient(";
    if (css.size() <= kFn.size() + 1 || css.back() != ')' ||
        !equals_ci(css.substr(0, kFn.size()), kFn)) {
        return false;
    }
    std::string rewritten = "linear-gradient(";
    rewritten.append(css.substr(kFn.size()));
    if (!linear_gradient_for_box(rewritten, area, out, color_box)) {
        return false;
    }
    out.repeating = true;
    return true;
}

}  // namespace paint
}  // namespace zui
