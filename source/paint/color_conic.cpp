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

namespace {

bool parse_turn_offset(std::string_view s, float& fraction) {
    float deg = 0;
    if (parse_angle(s, deg)) {
        fraction = deg / 360.f;
        return true;
    }
    float v = 0;
    std::string_view unit;
    if (!scan_number(s, v, unit) || unit != "%") {
        return false;
    }
    fraction = v * 0.01f;
    return true;
}

bool conic_prelude(std::string_view head) {
    const std::string l = lowered(trim(head));
    return l.rfind("from", 0) == 0 || l.rfind("at", 0) == 0;
}

}  // namespace

bool conic_gradient_for_box(std::string_view css, Rect area, Gradient& out,
                            const layout::Box* color_box) {
    css = trim(css);
    constexpr std::string_view kFn = "conic-gradient(";
    if (css.size() <= kFn.size() + 1 || css.back() != ')' ||
        !equals_ci(css.substr(0, kFn.size()), kFn)) {
        return false;
    }
    const std::vector<std::string_view> args =
        split_args(css.substr(kFn.size(), css.size() - kFn.size() - 1));
    if (args.size() < 2) {
        return false;
    }

    float from_deg = 0.f;
    float cx = area.x + area.w * 0.5f;
    float cy = area.y + area.h * 0.5f;
    size_t first_stop = 0;
    std::string method;
    const std::string_view head = trim(args[0]);
    std::vector<std::string> head_toks;
    tokenize_ws(head, head_toks);
    if (!take_interpolation_method(head_toks, method)) {
        return false;
    }
    std::string rebuilt;
    for (const std::string& tok : head_toks) {
        if (!rebuilt.empty()) {
            rebuilt.push_back(' ');
        }
        rebuilt.append(tok);
    }
    if (!method.empty() || (!rebuilt.empty() && conic_prelude(rebuilt))) {
        first_stop = 1;
        const std::vector<std::string>& toks = head_toks;
        std::vector<std::string> pos_toks;
        bool saw_at = false;
        for (size_t i = 0; i < toks.size(); ++i) {
            const std::string l = lowered(toks[i]);
            if (!saw_at && l == "from" && i + 1 < toks.size() && parse_angle(toks[i + 1], from_deg)) {
                ++i;
                continue;
            }
            if (l == "at") {
                saw_at = true;
                continue;
            }
            if (saw_at) {
                pos_toks.push_back(toks[i]);
            }
        }
        auto place = [&](const std::string& tok, float basis, float origin, bool horiz, float& o) {
            const std::string l = lowered(tok);
            if (l == "center") {
                o = origin + basis * 0.5f;
                return true;
            }
            if (horiz) {
                if (l == "left") {
                    o = origin;
                    return true;
                }
                if (l == "right") {
                    o = origin + basis;
                    return true;
                }
            } else {
                if (l == "top") {
                    o = origin;
                    return true;
                }
                if (l == "bottom") {
                    o = origin + basis;
                    return true;
                }
            }
            float v = 0.f;
            if (parse_length_against(tok, basis, v)) {
                o = origin + v;
                return true;
            }
            return false;
        };
        if (pos_toks.size() == 1) {
            place(pos_toks[0], area.w, area.x, true, cx);
        } else if (pos_toks.size() >= 2) {
            place(pos_toks[0], area.w, area.x, true, cx);
            place(pos_toks[1], area.h, area.y, false, cy);
        }
    }

    struct Raw {
        Color color;
        bool has_position = false;
        float position = 0;
    };
    std::vector<Raw> raw;
    std::vector<GradientHint> hints;
    for (size_t i = first_stop; i < args.size(); ++i) {
        std::vector<std::string> toks;
        tokenize_ws(args[i], toks);
        if (toks.empty()) {
            continue;
        }
        // A position-only token is a color-interpolation hint, same as on a linear gradient.
        float hint_pos = 0;
        if (toks.size() == 1 && parse_turn_offset(toks[0], hint_pos)) {
            if (!raw.empty()) {
                hints.push_back(GradientHint{raw.size() - 1, hint_pos});
            }
            continue;
        }
        const Color color = resolve_gradient_stop(toks[0], color_box);
        std::vector<float> positions;
        for (size_t t = 1; t < toks.size(); ++t) {
            float fraction = 0;
            if (parse_turn_offset(toks[t], fraction)) {
                positions.push_back(fraction);
            }
        }
        if (positions.empty()) {
            raw.push_back(Raw{color, false, 0});
        } else {
            for (float p : positions) {
                raw.push_back(Raw{color, true, p});
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
        resample_in_method(stops, hints, method);
    } else if (!hints.empty()) {
        expand_srgb_hints(stops, hints);
    }

    out = Gradient{};
    out.kind = GradientKind::Conic;
    out.repeating = false;
    out.x = cx;
    out.y = cy;
    out.angle = from_deg * kPi / 180.f;
    out.stops = std::move(stops);
    return true;
}

bool repeating_conic_gradient_for_box(std::string_view css, Rect area, Gradient& out,
                                      const layout::Box* color_box) {
    css = trim(css);
    constexpr std::string_view kFn = "repeating-conic-gradient(";
    if (css.size() <= kFn.size() + 1 || css.back() != ')' ||
        !equals_ci(css.substr(0, kFn.size()), kFn)) {
        return false;
    }
    std::string rewritten = "conic-gradient(";
    rewritten.append(css.substr(kFn.size()));
    if (!conic_gradient_for_box(rewritten, area, out, color_box)) {
        return false;
    }
    out.repeating = true;
    return true;
}

}  // namespace paint
}  // namespace zui
