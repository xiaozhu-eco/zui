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
enum class PosKw { Length, Left, Right, Top, Bottom, Center };

PosKw pos_kw(std::string_view tok) {
    const std::string l = lower_ascii(std::string(tok));
    if (l == "left") {
        return PosKw::Left;
    }
    if (l == "right") {
        return PosKw::Right;
    }
    if (l == "top") {
        return PosKw::Top;
    }
    if (l == "bottom") {
        return PosKw::Bottom;
    }
    if (l == "center") {
        return PosKw::Center;
    }
    return PosKw::Length;
}

bool pos_offset(std::string_view tok, float basis, float& out) {
    bool bad = false;
    return parse_bg_length(tok, basis, out, bad) && !bad;
}

bool apply_pos_edge(PosKw edge, bool has_off, float off, float basis, float& dest) {
    if (edge == PosKw::Center) {
        if (has_off) {
            return false;
        }
        dest = basis * 0.5f;
        return true;
    }
    if (edge == PosKw::Left || edge == PosKw::Top) {
        dest = has_off ? off : 0.f;
        return true;
    }
    if (edge == PosKw::Right || edge == PosKw::Bottom) {
        dest = basis - (has_off ? off : 0.f);
        return true;
    }
    return false;
}

/// CSS Backgrounds 3 §3.6 `<position>`. One or two values keep the old axis pairing.
/// Three or four values are an edge plus an offset (`right 16px bottom 12px`).
bool resolve_css_position(std::string_view css, float area_w, float area_h, float img_w, float img_h,
                          float& px, float& py) {
    const float bx = area_w - img_w;
    const float by = area_h - img_h;
    std::vector<std::string> parts;
    std::string cur;
    for (char c : css) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!cur.empty()) {
                parts.push_back(cur);
                cur.clear();
            }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) {
        parts.push_back(cur);
    }
    if (parts.empty() || parts.size() > 4) {
        return false;
    }
    PosKw toks[4];
    for (std::size_t i = 0; i < parts.size(); ++i) {
        toks[i] = pos_kw(parts[i]);
    }
    const auto horiz_edge = [](PosKw k) { return k == PosKw::Left || k == PosKw::Right; };
    const auto vert_edge = [](PosKw k) { return k == PosKw::Top || k == PosKw::Bottom; };
    const auto horiz_item = [](PosKw k) {
        return k == PosKw::Left || k == PosKw::Right || k == PosKw::Center;
    };
    const auto vert_item = [](PosKw k) {
        return k == PosKw::Top || k == PosKw::Bottom || k == PosKw::Center;
    };

    PosKw hx = PosKw::Center;
    PosKw hy = PosKw::Center;
    bool hx_off = false;
    bool hy_off = false;
    float ox = 0.f;
    float oy = 0.f;
    const std::string* hx_raw = nullptr;
    const std::string* hy_raw = nullptr;

    auto take_h = [&](PosKw edge, const std::string* raw) {
        if (edge == PosKw::Top || edge == PosKw::Bottom) {
            return false;
        }
        if (edge == PosKw::Center && raw != nullptr) {
            return false;
        }
        hx = edge == PosKw::Length ? PosKw::Left : edge;
        if (raw != nullptr) {
            hx_raw = raw;
            hx_off = true;
        }
        return true;
    };
    auto take_v = [&](PosKw edge, const std::string* raw) {
        if (edge == PosKw::Left || edge == PosKw::Right) {
            return false;
        }
        if (edge == PosKw::Center && raw != nullptr) {
            return false;
        }
        hy = edge == PosKw::Length ? PosKw::Top : edge;
        if (raw != nullptr) {
            hy_raw = raw;
            hy_off = true;
        }
        return true;
    };

    const std::size_t n = parts.size();
    if (n == 1) {
        if (toks[0] == PosKw::Top || toks[0] == PosKw::Bottom) {
            if (!take_v(toks[0], nullptr) || !take_h(PosKw::Center, nullptr)) {
                return false;
            }
        } else if (toks[0] == PosKw::Length) {
            if (!take_h(PosKw::Length, &parts[0]) || !take_v(PosKw::Center, nullptr)) {
                return false;
            }
        } else if (!take_h(toks[0], nullptr) || !take_v(PosKw::Center, nullptr)) {
            return false;
        }
    } else if (n == 2) {
        const bool first_vert = toks[0] == PosKw::Top || toks[0] == PosKw::Bottom;
        const bool second_horiz = toks[1] == PosKw::Left || toks[1] == PosKw::Right;
        const std::size_t ix = (first_vert || second_horiz) ? 1 : 0;
        const std::size_t iy = (first_vert || second_horiz) ? 0 : 1;
        const std::string* xr = toks[ix] == PosKw::Length ? &parts[ix] : nullptr;
        const std::string* yr = toks[iy] == PosKw::Length ? &parts[iy] : nullptr;
        if (!take_h(toks[ix], xr) || !take_v(toks[iy], yr)) {
            return false;
        }
    } else if (n == 3) {
        if (horiz_edge(toks[0]) && toks[1] == PosKw::Length && vert_item(toks[2])) {
            if (!take_h(toks[0], &parts[1]) || !take_v(toks[2], nullptr)) {
                return false;
            }
        } else if (vert_edge(toks[0]) && toks[1] == PosKw::Length && horiz_item(toks[2])) {
            if (!take_v(toks[0], &parts[1]) || !take_h(toks[2], nullptr)) {
                return false;
            }
        } else if (horiz_item(toks[0]) && vert_edge(toks[1]) && toks[2] == PosKw::Length) {
            if (!take_h(toks[0], nullptr) || !take_v(toks[1], &parts[2])) {
                return false;
            }
        } else if (vert_item(toks[0]) && horiz_edge(toks[1]) && toks[2] == PosKw::Length) {
            if (!take_v(toks[0], nullptr) || !take_h(toks[1], &parts[2])) {
                return false;
            }
        } else {
            return false;
        }
    } else if (horiz_edge(toks[0]) && toks[1] == PosKw::Length && vert_edge(toks[2]) &&
               toks[3] == PosKw::Length) {
        if (!take_h(toks[0], &parts[1]) || !take_v(toks[2], &parts[3])) {
            return false;
        }
    } else if (vert_edge(toks[0]) && toks[1] == PosKw::Length && horiz_edge(toks[2]) &&
               toks[3] == PosKw::Length) {
        if (!take_v(toks[0], &parts[1]) || !take_h(toks[2], &parts[3])) {
            return false;
        }
    } else {
        return false;
    }

    if (hx_off && !pos_offset(*hx_raw, bx, ox)) {
        return false;
    }
    if (hy_off && !pos_offset(*hy_raw, by, oy)) {
        return false;
    }
    return apply_pos_edge(hx, hx_off, ox, bx, px) && apply_pos_edge(hy, hy_off, oy, by, py);
}

/// `intrinsic_w/h` > 0 enable bitmap sizing (auto / cover / contain / one-auto axis). Gradients
/// pass zeros and keep the previous "fill the origin box" behaviour.
bool resolve_bg_image_rect(const Rect& origin, const std::string* pos_v, const std::string* size_v,
                           Rect& image_rect, float intrinsic_w, float intrinsic_h) {
    const bool has_intrinsic = intrinsic_w > 0.f && intrinsic_h > 0.f;
    float img_w = has_intrinsic ? intrinsic_w : origin.w;
    float img_h = has_intrinsic ? intrinsic_h : origin.h;
    bool unparseable = false;
    if (size_v != nullptr && !size_v->empty() && lower_ascii(*size_v) != "auto") {
        const std::string size_l = lower_ascii(*size_v);
        if (size_l == "cover" || size_l == "contain") {
            if (has_intrinsic) {
                const float sx = origin.w / intrinsic_w;
                const float sy = origin.h / intrinsic_h;
                const float scale = size_l == "cover" ? std::max(sx, sy) : std::min(sx, sy);
                img_w = intrinsic_w * scale;
                img_h = intrinsic_h * scale;
            } else {
                // Gradients have no intrinsic aspect; treat cover/contain as the origin box.
                img_w = origin.w;
                img_h = origin.h;
            }
        } else {
            // One or two lengths.
            std::string_view rest(*size_v);
            std::string_view t0;
            std::string_view t1;
            const auto sp = rest.find_first_of(" \t");
            if (sp == std::string_view::npos) {
                t0 = trim_sv_local(rest);
            } else {
                t0 = trim_sv_local(rest.substr(0, sp));
                t1 = trim_sv_local(rest.substr(sp + 1));
            }
            if (t0.empty()) {
                return false;
            }
            const bool t0_auto = lower_ascii(std::string(t0)) == "auto";
            const bool t1_auto = t1.empty() || lower_ascii(std::string(t1)) == "auto";
            if (t0_auto) {
                img_w = has_intrinsic ? intrinsic_w : origin.w;
            } else if (!parse_bg_length(t0, origin.w, img_w, unparseable) || unparseable) {
                return false;
            }
            if (t1.empty()) {
                // Single length: width set, height auto.
                if (has_intrinsic && !t0_auto && intrinsic_w > 0.f) {
                    img_h = img_w * (intrinsic_h / intrinsic_w);
                } else {
                    img_h = has_intrinsic ? intrinsic_h : origin.h;
                }
            } else if (t1_auto) {
                if (has_intrinsic && !t0_auto && intrinsic_w > 0.f) {
                    img_h = img_w * (intrinsic_h / intrinsic_w);
                } else {
                    img_h = has_intrinsic ? intrinsic_h : origin.h;
                }
            } else if (!parse_bg_length(t1, origin.h, img_h, unparseable) || unparseable) {
                return false;
            }
            // `auto <length>`: width from height × aspect when intrinsic is known.
            if (t0_auto && !t1_auto && has_intrinsic && intrinsic_h > 0.f) {
                img_w = img_h * (intrinsic_w / intrinsic_h);
            }
        }
    }
    if (img_w <= 0.f || img_h <= 0.f) {
        return false;
    }

    float px = 0.f;
    float py = 0.f;
    if (pos_v != nullptr && !pos_v->empty() && *pos_v != "0% 0%") {
        if (!resolve_css_position(*pos_v, origin.w, origin.h, img_w, img_h, px, py)) {
            return false;
        }
    }

    image_rect = Rect{origin.x + px, origin.y + py, img_w, img_h};
    return true;
}

/// CSS Images 3 object-fit + object-position against the replaced content box.
bool resolve_object_fit_rect(const Rect& content, float nat_w, float nat_h,
                             const std::string* fit_v, const std::string* pos_v, Rect& out) {
    if (content.w <= 0.f || content.h <= 0.f || nat_w <= 0.f || nat_h <= 0.f) {
        return false;
    }
    const std::string fit = fit_v == nullptr || fit_v->empty() ? "fill" : lower_ascii(*fit_v);
    float img_w = content.w;
    float img_h = content.h;
    if (fit == "none") {
        img_w = nat_w;
        img_h = nat_h;
    } else if (fit == "contain" || fit == "cover" || fit == "scale-down") {
        const float sx = content.w / nat_w;
        const float sy = content.h / nat_h;
        float scale = fit == "cover" ? std::max(sx, sy) : std::min(sx, sy);
        if (fit == "scale-down") {
            scale = std::min(1.f, std::min(sx, sy));
        }
        img_w = nat_w * scale;
        img_h = nat_h * scale;
    }
    // fill (initial) keeps img_w/h == content. Initial object-position is `50% 50%`.
    const float bx = content.w - img_w;
    const float by = content.h - img_h;
    float px = bx * 0.5f;
    float py = by * 0.5f;
    if (pos_v != nullptr && !pos_v->empty() &&
        !resolve_css_position(*pos_v, content.w, content.h, img_w, img_h, px, py)) {
        px = bx * 0.5f;
        py = by * 0.5f;
    }
    out = Rect{content.x + px, content.y + py, img_w, img_h};
    return true;
}

}  // namespace painter_detail
}  // namespace paint
}  // namespace zui
