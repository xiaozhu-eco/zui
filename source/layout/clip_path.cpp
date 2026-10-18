#include "layout/clip_path.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace zui {
namespace layout {
namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool parse_len(std::string_view tok, float basis, float& px) {
    tok = trim(tok);
    if (tok == "0") {
        px = 0.f;
        return true;
    }
    char* end = nullptr;
    const std::string tmp(tok);
    const float v = std::strtof(tmp.c_str(), &end);
    if (end == tmp.c_str()) {
        return false;
    }
    std::string_view unit(end);
    while (!unit.empty() && std::isspace(static_cast<unsigned char>(unit.front()))) {
        unit.remove_prefix(1);
    }
    if (unit.empty() || unit == "px") {
        px = v;
        return true;
    }
    if (unit == "%") {
        px = basis * v / 100.f;
        return true;
    }
    return false;
}

std::vector<std::string> split_ws(std::string_view s) {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : s) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
        } else {
            cur.push_back(ch);
        }
    }
    if (!cur.empty()) {
        out.push_back(cur);
    }
    return out;
}

bool parse_position(const std::vector<std::string>& toks, size_t start, float box_w, float box_h,
                    float& cx, float& cy) {
    auto axis = [&](const std::string& tok, float basis, float& o, bool horiz) {
        const std::string l = lower(tok);
        if (l == "center") {
            o = basis * 0.5f;
            return true;
        }
        if (horiz) {
            if (l == "left") {
                o = 0.f;
                return true;
            }
            if (l == "right") {
                o = basis;
                return true;
            }
        } else {
            if (l == "top") {
                o = 0.f;
                return true;
            }
            if (l == "bottom") {
                o = basis;
                return true;
            }
        }
        float v = 0.f;
        if (parse_len(tok, basis, v)) {
            o = v;
            return true;
        }
        return false;
    };
    if (start >= toks.size()) {
        cx = box_w * 0.5f;
        cy = box_h * 0.5f;
        return true;
    }
    if (start + 1 >= toks.size()) {
        return axis(toks[start], box_w, cx, true);
    }
    return axis(toks[start], box_w, cx, true) && axis(toks[start + 1], box_h, cy, false);
}

std::string_view fn_args(std::string_view css, std::string_view prefix) {
    const std::string l = lower(css);
    if (l.size() <= prefix.size() + 1 || css.back() != ')') {
        return {};
    }
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (l[i] != prefix[i]) {
            return {};
        }
    }
    return trim(css.substr(prefix.size(), css.size() - prefix.size() - 1));
}

}  // namespace

bool parse_clip_path(std::string_view css, float box_w, float box_h, ClipPathShape& out) {
    css = trim(css);
    out = ClipPathShape{};
    if (css.empty() || lower(css) == "none") {
        return false;
    }

    if (const std::string_view inner = fn_args(css, "inset("); !inner.empty()) {
        std::string body(inner);
        std::string round_part;
        const auto rpos = lower(body).find("round");
        if (rpos != std::string::npos) {
            round_part = std::string(trim(std::string_view(body).substr(rpos + 5)));
            body = std::string(trim(std::string_view(body).substr(0, rpos)));
        }
        const auto sides = split_ws(body);
        if (sides.empty() || sides.size() > 4) {
            return false;
        }
        float t = 0.f;
        float r = 0.f;
        float b = 0.f;
        float left = 0.f;
        if (sides.size() == 1) {
            if (!parse_len(sides[0], box_h, t) || !parse_len(sides[0], box_w, r)) {
                return false;
            }
            b = t;
            left = r;
        } else if (sides.size() == 2) {
            if (!parse_len(sides[0], box_h, t) || !parse_len(sides[1], box_w, r)) {
                return false;
            }
            b = t;
            left = r;
        } else if (sides.size() == 3) {
            if (!parse_len(sides[0], box_h, t) || !parse_len(sides[1], box_w, r) ||
                !parse_len(sides[2], box_h, b)) {
                return false;
            }
            left = r;
        } else if (!parse_len(sides[0], box_h, t) || !parse_len(sides[1], box_w, r) ||
                   !parse_len(sides[2], box_h, b) || !parse_len(sides[3], box_w, left)) {
            return false;
        }
        out.kind = ClipPathShape::Kind::Inset;
        out.inset_t = std::max(0.f, t);
        out.inset_r = std::max(0.f, r);
        out.inset_b = std::max(0.f, b);
        out.inset_l = std::max(0.f, left);
        if (!round_part.empty()) {
            const auto rt = split_ws(round_part);
            float rr = 0.f;
            if (!rt.empty() && parse_len(rt[0], std::min(box_w, box_h), rr)) {
                out.inset_round = std::max(0.f, rr);
            }
        }
        return true;
    }

    if (const std::string_view inner = fn_args(css, "circle("); !inner.empty()) {
        auto toks = split_ws(inner);
        float radius = 0.5f * std::min(box_w, box_h);
        size_t i = 0;
        if (!toks.empty() && lower(toks[0]) != "at") {
            const std::string tl = lower(toks[0]);
            if (tl == "closest-side") {
                radius = 0.5f * std::min(box_w, box_h);
                ++i;
            } else if (tl == "farthest-side") {
                radius = 0.5f * std::max(box_w, box_h);
                ++i;
            } else {
                const float pct_basis = std::sqrt((box_w * box_w + box_h * box_h) * 0.5f);
                if (parse_len(toks[0], pct_basis, radius)) {
                    ++i;
                }
            }
        }
        float cx = box_w * 0.5f;
        float cy = box_h * 0.5f;
        if (i < toks.size() && lower(toks[i]) == "at") {
            parse_position(toks, i + 1, box_w, box_h, cx, cy);
        }
        out.kind = ClipPathShape::Kind::Circle;
        out.cx = cx;
        out.cy = cy;
        out.rx = std::max(0.f, radius);
        out.ry = out.rx;
        return out.rx > 0.f;
    }

    if (const std::string_view inner = fn_args(css, "ellipse("); !inner.empty()) {
        auto toks = split_ws(inner);
        float rx = box_w * 0.5f;
        float ry = box_h * 0.5f;
        size_t i = 0;
        if (toks.size() >= 2 && lower(toks[0]) != "at") {
            if (lower(toks[0]) == "closest-side") {
                ++i;
            } else if (parse_len(toks[0], box_w, rx) && parse_len(toks[1], box_h, ry)) {
                i = 2;
            }
        }
        float cx = box_w * 0.5f;
        float cy = box_h * 0.5f;
        if (i < toks.size() && lower(toks[i]) == "at") {
            parse_position(toks, i + 1, box_w, box_h, cx, cy);
        }
        out.kind = ClipPathShape::Kind::Ellipse;
        out.cx = cx;
        out.cy = cy;
        out.rx = std::max(0.f, rx);
        out.ry = std::max(0.f, ry);
        return out.rx > 0.f && out.ry > 0.f;
    }

    if (const std::string_view inner = fn_args(css, "polygon("); !inner.empty()) {
        // CSS Shapes: `polygon( <fill-rule>? , <length-percentage> <length-percentage># )`.
        std::vector<std::string> parts;
        std::string cur;
        int depth = 0;
        for (char ch : std::string(inner)) {
            if (ch == '(') {
                ++depth;
                cur.push_back(ch);
            } else if (ch == ')') {
                depth = depth > 0 ? depth - 1 : 0;
                cur.push_back(ch);
            } else if (ch == ',' && depth == 0) {
                parts.push_back(std::string(trim(cur)));
                cur.clear();
            } else {
                cur.push_back(ch);
            }
        }
        if (!trim(cur).empty()) {
            parts.push_back(std::string(trim(cur)));
        }
        if (parts.empty()) {
            return false;
        }
        size_t start = 0;
        const std::string rule = lower(parts[0]);
        if (rule == "evenodd" || rule == "nonzero") {
            out.evenodd = rule == "evenodd";
            start = 1;
        }
        for (size_t i = start; i < parts.size(); ++i) {
            const auto toks = split_ws(parts[i]);
            if (toks.size() < 2) {
                return false;
            }
            float x = 0.f;
            float y = 0.f;
            if (!parse_len(toks[0], box_w, x) || !parse_len(toks[1], box_h, y)) {
                return false;
            }
            out.points.emplace_back(x, y);
        }
        if (out.points.size() < 3) {
            return false;
        }
        out.kind = ClipPathShape::Kind::Polygon;
        return true;
    }

    return false;
}

bool parse_shape_outside(std::string_view css, float box_w, float box_h, ClipPathShape& out) {
    css = trim(css);
    out = ClipPathShape{};
    if (css.empty() || lower(css) == "none") {
        return false;
    }
    // Drop a trailing <shape-box>; percentages still resolve against the caller's reference box
    // (float placement always uses the margin box for this subset).
    static constexpr const char* kBoxes[] = {
        "margin-box", "border-box", "padding-box", "content-box", "fill-box", "stroke-box",
        "view-box"};
    std::string body(css);
    {
        const std::string l = lower(body);
        for (const char* box : kBoxes) {
            const std::string token = std::string(" ") + box;
            if (l.size() >= token.size() &&
                l.compare(l.size() - token.size(), token.size(), token) == 0) {
                body = std::string(trim(std::string_view(body).substr(0, body.size() - token.size())));
                break;
            }
            if (l == box) {
                return false;
            }
        }
    }
    const std::string bl = lower(body);
    if (bl.rfind("url(", 0) == 0 || bl.rfind("polygon(", 0) == 0 || bl.rfind("path(", 0) == 0) {
        return false;
    }
    return parse_clip_path(body, box_w, box_h, out);
}

bool shape_inline_extent(const ClipPathShape& shape, float box_w, float box_h, float y0, float y1,
                         float shape_margin, float& out_left, float& out_right) {
    if (y1 < y0) {
        std::swap(y0, y1);
    }
    const float m = std::max(0.f, shape_margin);
    switch (shape.kind) {
        case ClipPathShape::Kind::None:
            return false;
        case ClipPathShape::Kind::Inset: {
            float x0 = shape.inset_l - m;
            float y_top = shape.inset_t - m;
            float x1 = box_w - shape.inset_r + m;
            float y_bot = box_h - shape.inset_b + m;
            if (x1 <= x0 || y_bot <= y_top) {
                return false;
            }
            if (y_bot <= y0 || y_top >= y1) {
                return false;
            }
            out_left = x0;
            out_right = x1;
            return true;
        }
        case ClipPathShape::Kind::Circle:
        case ClipPathShape::Kind::Ellipse: {
            const float rx = shape.rx + m;
            const float ry = shape.ry + m;
            if (rx <= 0.f || ry <= 0.f) {
                return false;
            }
            const float band0 = std::max(y0, shape.cy - ry);
            const float band1 = std::min(y1, shape.cy + ry);
            if (band1 <= band0) {
                return false;
            }
            // Widest chord in the band is at the y closest to the ellipse center.
            const float y_mid = std::clamp(shape.cy, band0, band1);
            const float ny = (y_mid - shape.cy) / ry;
            const float half = rx * std::sqrt(std::max(0.f, 1.f - ny * ny));
            out_left = shape.cx - half;
            out_right = shape.cx + half;
            return half > 0.f || m > 0.f;
        }
        case ClipPathShape::Kind::Polygon: {
            if (shape.points.size() < 3) {
                return false;
            }
            bool any = false;
            float left = 0.f;
            float right = 0.f;
            const auto consider = [&](float x, float y) {
                if (y < y0 - m || y > y1 + m) {
                    return;
                }
                if (!any) {
                    left = right = x;
                    any = true;
                } else {
                    left = std::min(left, x);
                    right = std::max(right, x);
                }
            };
            for (size_t i = 0; i < shape.points.size(); ++i) {
                const auto a = shape.points[i];
                const auto b = shape.points[(i + 1) % shape.points.size()];
                consider(a.first, a.second);
                const float y_lo = std::min(a.second, b.second);
                const float y_hi = std::max(a.second, b.second);
                const float band0 = y0 - m;
                const float band1 = y1 + m;
                if (y_hi < band0 || y_lo > band1 || y_hi <= y_lo) {
                    continue;
                }
                const float y_hit = std::clamp(0.5f * (band0 + band1), y_lo, y_hi);
                const float t = (y_hit - a.second) / (b.second - a.second);
                consider(a.first + t * (b.first - a.first), y_hit);
            }
            if (!any || right + m <= left - m) {
                return false;
            }
            out_left = left - m;
            out_right = right + m;
            return true;
        }
    }
    return false;
}

bool point_in_clip_path(const ClipPathShape& shape, float box_w, float box_h, float x, float y) {
    switch (shape.kind) {
        case ClipPathShape::Kind::None:
            return true;
        case ClipPathShape::Kind::Inset: {
            const float x0 = shape.inset_l;
            const float y0 = shape.inset_t;
            const float x1 = box_w - shape.inset_r;
            const float y1 = box_h - shape.inset_b;
            if (x1 <= x0 || y1 <= y0) {
                return false;
            }
            if (x < x0 || y < y0 || x > x1 || y > y1) {
                return false;
            }
            const float rr = shape.inset_round;
            if (rr <= 0.5f) {
                return true;
            }
            const float rw = x1 - x0;
            const float rh = y1 - y0;
            const float rad = std::min(rr, 0.5f * std::min(rw, rh));
            auto in_corner = [&](float px, float py, float ecx, float ecy) {
                const float nx = (px - ecx) / rad;
                const float ny = (py - ecy) / rad;
                return nx * nx + ny * ny <= 1.f;
            };
            if (x < x0 + rad && y < y0 + rad) {
                return in_corner(x, y, x0 + rad, y0 + rad);
            }
            if (x > x1 - rad && y < y0 + rad) {
                return in_corner(x, y, x1 - rad, y0 + rad);
            }
            if (x > x1 - rad && y > y1 - rad) {
                return in_corner(x, y, x1 - rad, y1 - rad);
            }
            if (x < x0 + rad && y > y1 - rad) {
                return in_corner(x, y, x0 + rad, y1 - rad);
            }
            return true;
        }
        case ClipPathShape::Kind::Circle:
        case ClipPathShape::Kind::Ellipse: {
            if (shape.rx <= 0.f || shape.ry <= 0.f) {
                return false;
            }
            const float nx = (x - shape.cx) / shape.rx;
            const float ny = (y - shape.cy) / shape.ry;
            return nx * nx + ny * ny <= 1.f;
        }
        case ClipPathShape::Kind::Polygon: {
            const int n = static_cast<int>(shape.points.size());
            if (n < 3) {
                return false;
            }
            int wind = 0;
            int cross = 0;
            for (int i = 0; i < n; ++i) {
                const auto a = shape.points[static_cast<size_t>(i)];
                const auto b = shape.points[static_cast<size_t>((i + 1) % n)];
                const bool up = a.second <= y && b.second > y;
                const bool down = b.second <= y && a.second > y;
                if (!up && !down) {
                    continue;
                }
                const float t = (y - a.second) / (b.second - a.second);
                const float x_at = a.first + t * (b.first - a.first);
                if (x_at <= x) {
                    continue;
                }
                if (shape.evenodd) {
                    ++cross;
                } else {
                    wind += up ? 1 : -1;
                }
            }
            return shape.evenodd ? (cross % 2) != 0 : wind != 0;
        }
    }
    return true;
}

}  // namespace layout
}  // namespace zui
