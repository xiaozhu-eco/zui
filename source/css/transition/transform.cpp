// `transform` / `opacity` parsing: the transform component functions and their composition.

#include "css/transition/transition.h"
#include "css/transition/internal.h"

#include "css/values.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace zui {
namespace css {

using namespace transition_detail;

namespace transition_detail {

struct Comp {
    float v = 0;
    bool pct = false;
};

Comp parse_comp(std::string_view tok) {
    tok = trim(tok);
    std::string s = lower(tok);
    Comp c;
    if (!s.empty() && s.back() == '%') {
        s.pop_back();
        c.pct = true;
        try {
            c.v = std::stof(s);
        } catch (...) {
            c.v = 0;
        }
        return c;
    }
    if (s.size() > 2 && s.substr(s.size() - 2) == "px") {
        s.resize(s.size() - 2);
    } else if (s.size() > 3 && s.substr(s.size() - 3) == "deg") {
        s.resize(s.size() - 3);
        try {
            c.v = std::stof(s) * (3.14159265f / 180.0f);
        } catch (...) {
            c.v = 0;
        }
        return c;
    } else if (s.size() > 3 && s.substr(s.size() - 3) == "rad") {
        s.resize(s.size() - 3);
    }
    try {
        c.v = std::stof(s);
    } catch (...) {
        c.v = 0;
    }
    return c;
}

float parse_px(std::string_view tok) { return parse_comp(tok).v; }

Xform parse_one_fn(std::string_view fn) {
    Xform x;
    std::string s = lower(trim(fn));
    auto lp = s.find('(');
    auto rp = s.rfind(')');
    std::string name = lp == std::string::npos ? s : s.substr(0, lp);
    std::string inner = (lp != std::string::npos && rp > lp) ? s.substr(lp + 1, rp - lp - 1) : std::string();
    std::vector<std::string_view> args;
    {
        size_t i = 0;
        while (i < inner.size()) {
            while (i < inner.size() &&
                   (inner[i] == ',' || std::isspace(static_cast<unsigned char>(inner[i])))) {
                ++i;
            }
            if (i >= inner.size()) {
                break;
            }
            size_t j = i;
            while (j < inner.size() && inner[j] != ',' && !std::isspace(static_cast<unsigned char>(inner[j]))) {
                ++j;
            }
            args.push_back(std::string_view(inner).substr(i, j - i));
            i = j;
        }
    }
    if (name == "translate") {
        if (!args.empty()) {
            Comp a = parse_comp(args[0]);
            x.tx = a.v;
            x.tx_pct = a.pct;
        }
        if (args.size() > 1) {
            Comp a = parse_comp(args[1]);
            x.ty = a.v;
            x.ty_pct = a.pct;
        }
    } else if (name == "translatex") {
        if (!args.empty()) {
            Comp a = parse_comp(args[0]);
            x.tx = a.v;
            x.tx_pct = a.pct;
        }
    } else if (name == "translatey") {
        if (!args.empty()) {
            Comp a = parse_comp(args[0]);
            x.ty = a.v;
            x.ty_pct = a.pct;
        }
    } else if (name == "scale") {
        if (!args.empty()) {
            x.sx = parse_px(args[0]);
            x.sy = args.size() > 1 ? parse_px(args[1]) : x.sx;
        }
    } else if (name == "scalex") {
        if (!args.empty()) {
            x.sx = parse_px(args[0]);
        }
    } else if (name == "scaley") {
        if (!args.empty()) {
            x.sy = parse_px(args[0]);
        }
    } else if (name == "rotate") {
        if (!args.empty()) {
            x.rot = parse_px(args[0]);
        }
    }
    return x;
}

Xform compose(Xform a, Xform b) {
    Xform o;
    o.sx = a.sx * b.sx;
    o.sy = a.sy * b.sy;
    o.rot = a.rot + b.rot;
    o.tx_pct = a.tx_pct && (b.tx_pct || b.tx == 0);
    o.ty_pct = a.ty_pct && (b.ty_pct || b.ty == 0);
    if (a.tx_pct == b.tx_pct || b.tx == 0 || a.tx == 0) {
        o.tx = a.tx + b.tx * a.sx;
        o.tx_pct = a.tx_pct || b.tx_pct;
    } else {
        o.tx = a.tx + b.tx * a.sx;
        o.tx_pct = false;
    }
    if (a.ty_pct == b.ty_pct || b.ty == 0 || a.ty == 0) {
        o.ty = a.ty + b.ty * a.sy;
        o.ty_pct = a.ty_pct || b.ty_pct;
    } else {
        o.ty = a.ty + b.ty * a.sy;
        o.ty_pct = false;
    }
    return o;
}

}  // namespace transition_detail

Xform parse_transform(std::string_view css) {
    css = trim(css);
    if (css.empty() || css == "none") {
        return {};
    }
    Xform out;
    auto toks = split_tokens(css);
    for (const std::string& tok : toks) {
        out = compose(out, parse_one_fn(tok));
    }
    return out;
}

float parse_opacity(std::string_view css) {
    css = trim(css);
    if (css.empty()) {
        return 1;
    }
    std::string s(css);
    float mul = 1;
    if (!s.empty() && s.back() == '%') {
        s.pop_back();
        mul = 0.01f;
    }
    try {
        return std::max(0.0f, std::min(1.0f, std::stof(s) * mul));
    } catch (...) {
        return 1;
    }
}

}  // namespace css
}  // namespace zui
