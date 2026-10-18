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

bool looks_like_radial_geometry(std::string_view s) {
    const std::string l = lowered(s);
    // `circle`, `circle 50px`, `ellipse 40px 20px`. An explicit radius is still geometry
    // (CSS Images 3 §3.2); only the bare keyword used to match, so `circle 50px` was read
    // as a colour stop and the ending shape stayed farthest-corner.
    if (l.rfind("circle", 0) == 0 || l.rfind("ellipse", 0) == 0) {
        return true;
    }
    if (l.find("closest-side") != std::string::npos ||
        l.find("farthest-side") != std::string::npos ||
        l.find("closest-corner") != std::string::npos ||
        l.find("farthest-corner") != std::string::npos || l.find("at ") != std::string::npos ||
        l.rfind("at", 0) == 0) {
        return true;
    }
    float dummy = 0;
    std::string_view unit;
    if (scan_number(l, dummy, unit) && l.find('#') == std::string::npos &&
        l.find("rgb") == std::string::npos) {
        return unit.empty() || unit == "%" || equals_ci(unit, "px") ||
               l.find(' ') != std::string::npos;
    }
    return false;
}

}  // namespace

bool radial_gradient_for_box(std::string_view css, Rect area, Gradient& out,
                             const layout::Box* color_box) {
    css = trim(css);
    constexpr std::string_view kFn = "radial-gradient(";
    if (css.size() <= kFn.size() + 1 || css.back() != ')' ||
        !equals_ci(css.substr(0, kFn.size()), kFn)) {
        return false;
    }
    const std::vector<std::string_view> args =
        split_args(css.substr(kFn.size(), css.size() - kFn.size() - 1));
    if (args.size() < 2) {
        return false;
    }

    bool is_circle = false;
    enum class SizeKind { FarthestCorner, ClosestSide, Explicit };
    SizeKind size_kind = SizeKind::FarthestCorner;
    float explicit_rx = 0.f;
    float explicit_ry = 0.f;
    float cx = area.x + area.w * 0.5f;
    float cy = area.y + area.h * 0.5f;
    size_t first_stop = 0;

    const std::string_view head = trim(args[0]);
    std::vector<std::string> head_toks;
    tokenize_ws(head, head_toks);
    std::string method;
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
    // `in oklch` alone is a method, not a colour stop. Geometry tokens stay after it is removed.
    const bool geometry = !rebuilt.empty() && looks_like_radial_geometry(rebuilt);
    if (geometry || !method.empty()) {
        first_stop = 1;
    }
    if (geometry) {
        const std::vector<std::string>& toks = head_toks;
        bool saw_at = false;
        std::vector<std::string> pos_toks;
        std::vector<std::string> size_toks;
        for (const std::string& tok : toks) {
            const std::string l = lowered(tok);
            if (l == "at") {
                saw_at = true;
                continue;
            }
            if (saw_at) {
                pos_toks.push_back(tok);
                continue;
            }
            if (l == "circle") {
                is_circle = true;
                continue;
            }
            if (l == "ellipse") {
                is_circle = false;
                continue;
            }
            if (l == "closest-side") {
                size_kind = SizeKind::ClosestSide;
                continue;
            }
            if (l == "farthest-corner" || l == "farthest-side" || l == "closest-corner") {
                size_kind = SizeKind::FarthestCorner;
                continue;
            }
            size_toks.push_back(tok);
        }
        if (!size_toks.empty()) {
            float a = 0.f;
            float b = 0.f;
            if (parse_length_against(size_toks[0], area.w, a)) {
                size_kind = SizeKind::Explicit;
                explicit_rx = a;
                explicit_ry = a;
                is_circle = true;
                if (size_toks.size() >= 2 && parse_length_against(size_toks[1], area.h, b)) {
                    explicit_ry = b;
                    is_circle = false;
                }
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

    const float left = cx - area.x;
    const float right = area.x + area.w - cx;
    const float top = cy - area.y;
    const float bot = area.y + area.h - cy;
    float rx = 0.f;
    float ry = 0.f;
    if (size_kind == SizeKind::Explicit) {
        rx = std::max(0.f, explicit_rx);
        ry = std::max(0.f, explicit_ry);
    } else if (size_kind == SizeKind::ClosestSide) {
        if (is_circle) {
            rx = ry = std::max(0.f, std::min(std::min(left, right), std::min(top, bot)));
        } else {
            rx = std::max(0.f, std::min(left, right));
            ry = std::max(0.f, std::min(top, bot));
        }
    } else if (is_circle) {
        const float dx = std::max(left, right);
        const float dy = std::max(top, bot);
        rx = ry = std::sqrt(dx * dx + dy * dy);
    } else {
        // Ellipse + farthest-corner: same aspect ratio as the gradient box, through the
        // farthest corner (CSS Images 3 §3.2.2). For a centred box that is rx=w/√2, ry=h/√2.
        const float dx = std::max(left, right);
        const float dy = std::max(top, bot);
        const float aspect = area.h > 0.f ? area.w / area.h : 1.f;
        ry = std::sqrt((dx * dx) / (aspect * aspect) + dy * dy);
        rx = aspect * ry;
    }
    if (rx <= 0.f || ry <= 0.f) {
        return false;
    }

    // Reuse linear stop resolution: forge `to right` so % maps against ref_len as the line length.
    // The interpolation method rides in that prelude (`to right in oklch`) so the stop list is
    // sampled in the specified space before these radii are attached.
    const float ref_len = std::max(1.f, 0.5f * (rx + ry));
    std::string forged = "linear-gradient(to right";
    if (!method.empty()) {
        forged.push_back(' ');
        forged.append(method);
    }
    for (size_t i = first_stop; i < args.size(); ++i) {
        forged.push_back(',');
        forged.append(args[i]);
    }
    forged.push_back(')');
    Gradient tmp;
    if (!linear_gradient_for_box(forged, Rect{0, 0, ref_len, 1}, tmp, color_box) ||
        tmp.stops.size() < 2) {
        return false;
    }

    out = Gradient{};
    out.kind = GradientKind::Radial;
    out.repeating = false;
    out.x = cx;
    out.y = cy;
    out.rx = rx;
    out.ry = ry;
    out.stops = std::move(tmp.stops);
    return true;
}

bool repeating_radial_gradient_for_box(std::string_view css, Rect area, Gradient& out,
                                       const layout::Box* color_box) {
    css = trim(css);
    constexpr std::string_view kFn = "repeating-radial-gradient(";
    if (css.size() <= kFn.size() + 1 || css.back() != ')' ||
        !equals_ci(css.substr(0, kFn.size()), kFn)) {
        return false;
    }
    std::string rewritten = "radial-gradient(";
    rewritten.append(css.substr(kFn.size()));
    if (!radial_gradient_for_box(rewritten, area, out, color_box)) {
        return false;
    }
    out.repeating = true;
    return true;
}

}  // namespace paint
}  // namespace zui
