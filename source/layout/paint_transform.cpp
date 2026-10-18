#include "layout/paint_transform.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace zui {
namespace layout {
namespace paint_detail {
namespace {

std::string_view trim_view(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

std::string lower_ascii(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

/// One function argument: `-4`, `32px`, `110%`, `0.5turn`. `percent` means the value still has to
/// be resolved against the box size, `angle` that it was already converted to radians.
struct TransformArg {
    float value = 0;
    bool percent = false;
    bool angle = false;
};

TransformArg parse_arg(std::string_view token) {
    TransformArg out;
    token = trim_view(token);
    if (token.empty()) {
        return out;
    }
    std::string text(token);
    if (text.back() == '%') {
        out.percent = true;
        return TransformArg{std::strtof(text.substr(0, text.size() - 1).c_str(), nullptr), true,
                            false};
    }
    // `grad` before `rad`: "100grad" also ends in "rad", and the longer unit has to win.
    if (text.size() > 4 && text.compare(text.size() - 4, 4, "grad") == 0) {
        return TransformArg{std::strtof(text.substr(0, text.size() - 4).c_str(), nullptr) *
                                3.14159265f / 200.0f,
                            false, true};
    }
    if (text.size() > 3 && text.compare(text.size() - 3, 3, "rad") == 0) {
        return TransformArg{std::strtof(text.substr(0, text.size() - 3).c_str(), nullptr), false,
                            true};
    }
    if (text.size() > 4 && text.compare(text.size() - 4, 4, "turn") == 0) {
        return TransformArg{std::strtof(text.substr(0, text.size() - 4).c_str(), nullptr) *
                                6.28318531f,
                            false, true};
    }
    if (text.size() > 3 && text.compare(text.size() - 3, 3, "deg") == 0) {
        return TransformArg{std::strtof(text.substr(0, text.size() - 3).c_str(), nullptr) *
                                3.14159265f / 180.0f,
                            false, true};
    }
    return TransformArg{std::strtof(text.c_str(), nullptr), false, false};
}

std::vector<std::string_view> split_args(std::string_view args) {
    std::vector<std::string_view> parts;
    std::size_t i = 0;
    while (i < args.size()) {
        while (i < args.size() &&
               (args[i] == ',' || std::isspace(static_cast<unsigned char>(args[i])))) {
            ++i;
        }
        const std::size_t start = i;
        while (i < args.size() && args[i] != ',' &&
               !std::isspace(static_cast<unsigned char>(args[i]))) {
            ++i;
        }
        if (i > start) {
            parts.push_back(args.substr(start, i - start));
        }
    }
    return parts;
}

Aff2 scale_mat(float sx, float sy) {
    Aff2 m;
    m.a = sx;
    m.e = sy;
    return m;
}

Aff2 rotate_mat(float rad) {
    Aff2 m;
    const float co = std::cos(rad);
    const float si = std::sin(rad);
    m.a = co;
    m.b = -si;
    m.d = si;
    m.e = co;
    return m;
}

Aff2 skew_mat(float ax, float ay) {
    // CSS Transforms: skewX(ax) → (x + y·tan(ax), y); skewY(ay) → (x, y + x·tan(ay)).
    // Aff2 maps (x,y) → (a·x + b·y + c, d·x + e·y + f).
    Aff2 m;
    m.b = std::tan(ax);
    m.d = std::tan(ay);
    return m;
}

/// CSS `matrix(a, b, c, d, e, f)` → Aff2 columns `[a c e; b d f]`.
Aff2 css_matrix(float a, float b, float c, float d, float e, float f) {
    Aff2 m;
    m.a = a;
    m.b = c;
    m.c = e;
    m.d = b;
    m.e = d;
    m.f = f;
    return m;
}

bool is_none_value(std::string_view text) {
    text = trim_view(text);
    if (text.empty()) {
        return true;
    }
    const std::string word = lower_ascii(text);
    return word == "none" || word == "initial" || word == "unset";
}

float axis_length(const TransformArg& arg, float basis) {
    return arg.percent ? arg.value * 0.01f * basis : arg.value;
}

float unit_scale(const TransformArg& arg) {
    // CSS Transforms 2: a percentage on `scale` is a number (`100%` == 1), not a box fraction.
    return arg.percent ? arg.value * 0.01f : arg.value;
}

/// CSS Transforms 2 §6. The point sees the `transform` functions first, then `scale`, then
/// `rotate`, then `translate`. A non-zero Z and a rotation about x or y stay unapplied: this
/// paint path is the 2D subset.
Aff2 individual_transform(const Box& box, float width, float height) {
    Aff2 out;
    if (box.node == nullptr) {
        return out;
    }
    if (const std::string* scale = box.node->style("scale")) {
        if (!is_none_value(*scale)) {
            const std::vector<std::string_view> parts = split_args(*scale);
            if (!parts.empty()) {
                const float sx = unit_scale(parse_arg(parts[0]));
                float sy = sx;
                if (parts.size() > 1) {
                    sy = unit_scale(parse_arg(parts[1]));
                }
                out = out.then(scale_mat(sx, sy));
            }
        }
    }
    if (const std::string* rotate = box.node->style("rotate")) {
        if (!is_none_value(*rotate)) {
            const std::vector<std::string_view> parts = split_args(*rotate);
            if (!parts.empty()) {
                const std::string first = lower_ascii(parts[0]);
                if (first == "x" || first == "y") {
                    // 2D subset: no planar effect.
                } else if (first == "z" && parts.size() > 1) {
                    out = out.then(rotate_mat(parse_arg(parts[1]).value));
                } else if (parts.size() >= 4) {
                    const float ax = parse_arg(parts[0]).value;
                    const float ay = parse_arg(parts[1]).value;
                    const float az = parse_arg(parts[2]).value;
                    if (std::fabs(ax) < 1e-4f && std::fabs(ay) < 1e-4f && std::fabs(az) > 1e-4f) {
                        const float sign = az > 0.f ? 1.f : -1.f;
                        out = out.then(rotate_mat(sign * parse_arg(parts[3]).value));
                    }
                } else {
                    out = out.then(rotate_mat(parse_arg(parts[0]).value));
                }
            }
        }
    }
    if (const std::string* translate = box.node->style("translate")) {
        if (!is_none_value(*translate)) {
            const std::vector<std::string_view> parts = split_args(*translate);
            if (!parts.empty()) {
                const float tx = axis_length(parse_arg(parts[0]), width);
                const float ty =
                    parts.size() > 1 ? axis_length(parse_arg(parts[1]), height) : 0.f;
                out = out.then(translated(tx, ty));
            }
        }
    }
    return out;
}

/// Product of the CSS transform function list (CSS Transforms 1), without origin. Unknown
/// names are skipped; `perspective()` is a 2D no-op. The list applies right to left: while
/// walking left to right, each new function is applied before the product so far.
Aff2 transform_of(std::string_view css, float width, float height) {
    Aff2 out;
    std::size_t i = 0;
    while (i < css.size()) {
        const std::size_t open = css.find('(', i);
        if (open == std::string_view::npos) {
            break;
        }
        const std::size_t close = css.find(')', open);
        if (close == std::string_view::npos) {
            break;
        }
        const std::string name = lower_ascii(trim_view(css.substr(i, open - i)));
        const std::vector<std::string_view> args =
            split_args(css.substr(open + 1, close - open - 1));
        auto arg_value = [&](std::size_t index, float basis) {
            if (index >= args.size()) {
                return 0.f;
            }
            const TransformArg arg = parse_arg(args[index]);
            return arg.percent ? arg.value * 0.01f * basis : arg.value;
        };
        auto angle_at = [&](std::size_t index) {
            if (index >= args.size()) {
                return 0.f;
            }
            return parse_arg(args[index]).value;
        };

        Aff2 step;
        if (name == "translate" || name == "translatex" || name == "translatey" ||
            name == "translate3d" || name == "translatez") {
            float tx = 0.f;
            float ty = 0.f;
            if (name == "translatey") {
                ty = arg_value(0, height);
            } else if (name == "translatez") {
                // 2D paint: z translation is ignored.
            } else if (name == "translate3d") {
                tx = arg_value(0, width);
                ty = arg_value(1, height);
            } else if (name == "translatex") {
                tx = arg_value(0, width);
            } else {
                tx = arg_value(0, width);
                ty = arg_value(1, height);
            }
            step = translated(tx, ty);
        } else if (name == "scale" || name == "scalex" || name == "scaley" || name == "scale3d") {
            float sx = 1.f;
            float sy = 1.f;
            if (name == "scaley") {
                sy = arg_value(0, 0.f);
            } else if (name == "scalex") {
                sx = arg_value(0, 0.f);
            } else if (name == "scale3d") {
                sx = arg_value(0, 0.f);
                sy = arg_value(1, 0.f);
            } else {
                sx = arg_value(0, 0.f);
                sy = args.size() > 1 ? parse_arg(args[1]).value : sx;
            }
            step = scale_mat(sx, sy);
        } else if (name == "rotate" || name == "rotatez") {
            step = rotate_mat(angle_at(0));
        } else if (name == "skew") {
            step = skew_mat(angle_at(0), args.size() > 1 ? angle_at(1) : 0.f);
        } else if (name == "skewx") {
            step = skew_mat(angle_at(0), 0.f);
        } else if (name == "skewy") {
            step = skew_mat(0.f, angle_at(0));
        } else if (name == "matrix") {
            // matrix(a, b, c, d, e, f)
            const float a = args.size() > 0 ? parse_arg(args[0]).value : 1.f;
            const float b = args.size() > 1 ? parse_arg(args[1]).value : 0.f;
            const float c = args.size() > 2 ? parse_arg(args[2]).value : 0.f;
            const float d = args.size() > 3 ? parse_arg(args[3]).value : 1.f;
            const float e = args.size() > 4 ? parse_arg(args[4]).value : 0.f;
            const float f = args.size() > 5 ? parse_arg(args[5]).value : 0.f;
            step = css_matrix(a, b, c, d, e, f);
        } else if (name == "matrix3d") {
            // 2D slice: a=m11, b=m12, c=m21, d=m22, e=m41, f=m42 (1-based CSS matrix3d indices).
            const float a = args.size() > 0 ? parse_arg(args[0]).value : 1.f;
            const float b = args.size() > 1 ? parse_arg(args[1]).value : 0.f;
            const float c = args.size() > 4 ? parse_arg(args[4]).value : 0.f;
            const float d = args.size() > 5 ? parse_arg(args[5]).value : 1.f;
            const float e = args.size() > 12 ? parse_arg(args[12]).value : 0.f;
            const float f = args.size() > 13 ? parse_arg(args[13]).value : 0.f;
            step = css_matrix(a, b, c, d, e, f);
        } else if (name == "perspective") {
            // 2D paint path: perspective has no planar effect here.
            step = Aff2{};
        } else {
            i = close + 1;
            continue;
        }
        out = step.then(out);
        i = close + 1;
    }
    return out;
}

/// CSS Transforms 1 section 4: the point the whole function list is applied about, as used values.
/// The initial value is `50% 50%`, and the keywords are `left` / `center` / `right` on the inline
/// axis and `top` / `center` / `bottom` on the block one; a single vertical keyword leaves the other
/// component at `center`. A third length/percentage (z origin) is accepted and ignored.
void resolve_origin(std::string_view css, float width, float height, float& ox, float& oy) {
    ox = width * 0.5f;
    oy = height * 0.5f;
    const std::vector<std::string_view> parts = split_args(css);
    bool have_x = false;
    bool have_y = false;
    for (const std::string_view part : parts) {
        if (have_x && have_y) {
            // Third component is the z origin — 2D paint does not consume it.
            break;
        }
        const std::string word = lower_ascii(part);
        float fraction = 0.f;
        const bool keyword = word == "left" || word == "right" || word == "center" ||
                             word == "top" || word == "bottom";
        if (keyword) {
            fraction = (word == "left" || word == "top") ? 0.f : 0.5f;
            if (word == "right" || word == "bottom") {
                fraction = 1.f;
            }
            const bool vertical_only = word == "top" || word == "bottom";
            if (vertical_only && !have_x && !have_y) {
                oy = fraction * height;
                have_y = true;
            } else if (!have_x) {
                ox = fraction * width;
                have_x = true;
            } else {
                oy = fraction * height;
                have_y = true;
            }
            continue;
        }
        const TransformArg arg = parse_arg(part);
        const float value = arg.percent ? arg.value * 0.01f * (have_x ? height : width) : arg.value;
        if (!have_x) {
            ox = value;
            have_x = true;
        } else {
            oy = value;
            have_y = true;
        }
    }
}

/// The used `transform-origin` of a box, or the initial centre when the page does not state one.
void origin_of(const Box& box, float width, float height, float& ox, float& oy) {
    ox = width * 0.5f;
    oy = height * 0.5f;
    if (box.node == nullptr) {
        return;
    }
    const std::string* value = box.node->style("transform-origin");
    if (value == nullptr || value->empty()) {
        return;
    }
    resolve_origin(*value, width, height, ox, oy);
}

Aff2 from_decomposed(float tx, float ty, float sx, float sy, float rot) {
    const float co = std::cos(rot);
    const float si = std::sin(rot);
    Aff2 m;
    m.a = co * sx;
    m.b = -si * sy;
    m.d = si * sx;
    m.e = co * sy;
    m.c = tx;
    m.f = ty;
    return m;
}

}  // namespace

Aff2 translated(float x, float y) {
    Aff2 out;
    out.c = x;
    out.f = y;
    return out;
}

Xform xform_of(const Box& box, float width, float height) {
    Xform out;
    origin_of(box, width, height, out.ox, out.oy);
    Aff2 listed;
    if (box.node != nullptr && (box.node->used.flags & html::VisualUsed::kTransform)) {
        const float tx =
            box.node->used.tx_pct ? box.node->used.tx * 0.01f * width : box.node->used.tx;
        const float ty =
            box.node->used.ty_pct ? box.node->used.ty * 0.01f * height : box.node->used.ty;
        listed = from_decomposed(tx, ty, box.node->used.sx, box.node->used.sy, box.node->used.rot);
    } else if (box.node != nullptr) {
        const std::string* value = box.node->style("transform");
        if (value != nullptr && *value != "none" && !value->empty()) {
            listed = transform_of(*value, width, height);
        }
    }
    // Functions stay inside. scale / rotate / translate wrap them (Transforms 2 §6).
    const Aff2 indiv = individual_transform(box, width, height);
    out.m = listed.then(indiv);
    return out;
}

Aff2 local_transform(float width, float height, const Xform& xform) {
    // CSS Transforms 1 section 4: the list is applied about `transform-origin`; the box centre is
    // only that property's initial value, and a negative `ox` / `oy` means it was not resolved.
    const float cx = xform.ox >= 0.f ? xform.ox : width * 0.5f;
    const float cy = xform.oy >= 0.f ? xform.oy : height * 0.5f;
    return translated(-cx, -cy).then(xform.m).then(translated(cx, cy));
}

}  // namespace paint_detail
}  // namespace layout
}  // namespace zui
