// Per-property transition values: the specified value read off computed style, the used
// overlay written back to `Node::used`, and interpolation between two values.

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

bool is_length_prop(Prop prop) {
    switch (prop) {
        case Prop::Width:
        case Prop::Height:
        case Prop::MarginTop:
        case Prop::MarginRight:
        case Prop::MarginBottom:
        case Prop::MarginLeft:
        case Prop::PaddingTop:
        case Prop::PaddingRight:
        case Prop::PaddingBottom:
        case Prop::PaddingLeft:
            return true;
        default:
            return false;
    }
}

float element_font_px(const html::Node* node) {
    if (node == nullptr) {
        return 16.f;
    }
    if (const std::string* fs = node_get(node, "font-size")) {
        const ResolvedLength r = resolve_length(*fs, element_font_px(node->parent), 16.f);
        if (r.valid && !r.is_percent) {
            return r.px;
        }
    }
    return node->parent != nullptr ? element_font_px(node->parent) : 16.f;
}

float root_font_px(const html::Node* node) {
    const html::Node* n = node;
    while (n != nullptr && n->parent != nullptr) {
        n = n->parent;
    }
    return element_font_px(n);
}

/// Parse a transition length endpoint: absolute, percentage, or `auto`.
bool parse_length_endpoint(const html::Node* node, const std::string* css, Value* out) {
    if (css == nullptr || out == nullptr) {
        return false;
    }
    const std::string_view t = trim(*css);
    if (t.empty()) {
        return false;
    }
    if (t == "auto") {
        out->length_auto = true;
        out->length_percent = false;
        out->length_ok = false;
        out->length = 0;
        return true;
    }
    // CSS Values: unitless 0 is a valid length.
    if (t == "0") {
        out->length = 0;
        out->length_ok = true;
        out->length_percent = false;
        out->length_auto = false;
        return true;
    }
    // Bare percentage (and calc that is purely %-based left for resolve_length).
    if (!t.empty() && t.back() == '%' && t.find('(') == std::string_view::npos) {
        try {
            out->length = std::stof(std::string(t.substr(0, t.size() - 1)));
            out->length_percent = true;
            out->length_auto = false;
            out->length_ok = false;
            return true;
        } catch (...) {
            return false;
        }
    }
    if (t.find('%') != std::string_view::npos) {
        // Mixed %-calc: keep as percent of the resolved absolute when resolve_length can.
        const ResolvedLength r = resolve_length(t, element_font_px(node), root_font_px(node));
        if (r.valid && r.is_percent) {
            out->length = r.px;  // resolve_length stores the % number in px for is_percent
            out->length_percent = true;
            out->length_auto = false;
            out->length_ok = false;
            return true;
        }
        return false;
    }
    const ResolvedLength r = resolve_length(t, element_font_px(node), root_font_px(node));
    if (!r.valid || r.is_percent) {
        return false;
    }
    out->length = r.px;
    out->length_ok = true;
    out->length_percent = false;
    out->length_auto = false;
    return true;
}

bool allow_keyword_size(const html::Node* node) {
    const std::string* v = node_get(node, "interpolate-size");
    if (v == nullptr) {
        return false;
    }
    return lower(trim(*v)) == "allow-keywords";
}

bool axis_is_block(Prop prop) {
    return prop == Prop::Height || prop == Prop::MarginTop || prop == Prop::MarginBottom ||
           prop == Prop::PaddingTop || prop == Prop::PaddingBottom;
}

bool try_resolve_length(Prop prop, Value& v, const LengthBasis& basis, bool allow_auto) {
    if (v.length_ok) {
        return true;
    }
    if (v.length_percent) {
        const bool block = axis_is_block(prop);
        // Margin/padding % always use the inline containing-block size (CSS Box 4).
        const bool use_inline = prop == Prop::Width || prop == Prop::MarginTop ||
                                prop == Prop::MarginRight || prop == Prop::MarginBottom ||
                                prop == Prop::MarginLeft || prop == Prop::PaddingTop ||
                                prop == Prop::PaddingRight || prop == Prop::PaddingBottom ||
                                prop == Prop::PaddingLeft || !block;
        if (use_inline) {
            if (!basis.has_inline) {
                return false;
            }
            v.length = basis.inline_px * v.length / 100.f;
        } else {
            if (!basis.has_block) {
                return false;
            }
            v.length = basis.block_px * v.length / 100.f;
        }
        v.length_percent = false;
        v.length_ok = true;
        return true;
    }
    if (v.length_auto) {
        if (!allow_auto) {
            return false;
        }
        if (prop == Prop::Width) {
            if (!basis.has_used_width) {
                return false;
            }
            v.length = basis.used_width;
        } else if (prop == Prop::Height) {
            if (!basis.has_used_height) {
                return false;
            }
            v.length = basis.used_height;
        } else {
            // margin/padding auto is 0 for transition purposes.
            v.length = 0;
        }
        v.length_auto = false;
        v.length_ok = true;
        return true;
    }
    return false;
}

Value specified_value(const html::Node* node, const StyleMap* map, Prop prop) {
    Value v;
    switch (prop) {
        case Prop::Color:
            v.color = color_from(map, node, "color");
            break;
        case Prop::Background:
            v.color = color_from(map, node, "background-color");
            break;
        case Prop::BorderTop:
            v.color = color_from(map, node, "border-top-color");
            break;
        case Prop::BorderRight:
            v.color = color_from(map, node, "border-right-color");
            break;
        case Prop::BorderBottom:
            v.color = color_from(map, node, "border-bottom-color");
            break;
        case Prop::BorderLeft:
            v.color = color_from(map, node, "border-left-color");
            break;
        case Prop::Opacity: {
            const std::string* s = map_get(map, "opacity");
            v.opacity = s ? parse_opacity(*s) : 1.0f;
            break;
        }
        case Prop::Transform: {
            const std::string* s = map_get(map, "transform");
            v.xform = s ? parse_transform(*s) : Xform{};
            break;
        }
        case Prop::Width:
            parse_length_endpoint(node, map_get(map, "width"), &v);
            break;
        case Prop::Height:
            parse_length_endpoint(node, map_get(map, "height"), &v);
            break;
        case Prop::MarginTop:
            parse_length_endpoint(node, map_get(map, "margin-top"), &v);
            break;
        case Prop::MarginRight:
            parse_length_endpoint(node, map_get(map, "margin-right"), &v);
            break;
        case Prop::MarginBottom:
            parse_length_endpoint(node, map_get(map, "margin-bottom"), &v);
            break;
        case Prop::MarginLeft:
            parse_length_endpoint(node, map_get(map, "margin-left"), &v);
            break;
        case Prop::PaddingTop:
            parse_length_endpoint(node, map_get(map, "padding-top"), &v);
            break;
        case Prop::PaddingRight:
            parse_length_endpoint(node, map_get(map, "padding-right"), &v);
            break;
        case Prop::PaddingBottom:
            parse_length_endpoint(node, map_get(map, "padding-bottom"), &v);
            break;
        case Prop::PaddingLeft:
            parse_length_endpoint(node, map_get(map, "padding-left"), &v);
            break;
    }
    return v;
}

void write_used(html::Node* node, Prop prop, const Value& v) {
    if (node == nullptr) {
        return;
    }
    // The overlay is a paint input of its own: it lives outside the interned style, so nothing in
    // the cascade or the layout engine sees it move (`css/transition/transition.h`). Stamping the paint clock
    // here is what keeps a retained display list from showing the previous frame's overlay.
    html::note_paint_change(node);
    auto put = [](std::uint8_t& r, std::uint8_t& g, std::uint8_t& b, std::uint8_t& a, Rgba c) {
        auto cl = [](float x) {
            return static_cast<std::uint8_t>(std::max(0.0f, std::min(255.0f, x + 0.5f)));
        };
        r = cl(c.r);
        g = cl(c.g);
        b = cl(c.b);
        a = cl(c.a);
    };
    switch (prop) {
        case Prop::Color:
            node->used.flags |= html::VisualUsed::kColor;
            put(node->used.color_r, node->used.color_g, node->used.color_b, node->used.color_a, v.color);
            break;
        case Prop::Background:
            node->used.flags |= html::VisualUsed::kBg;
            put(node->used.bg_r, node->used.bg_g, node->used.bg_b, node->used.bg_a, v.color);
            break;
        case Prop::Opacity:
            node->used.flags |= html::VisualUsed::kOpacity;
            node->used.opacity = std::max(0.0f, std::min(1.0f, v.opacity));
            break;
        case Prop::BorderTop:
            node->used.flags |= html::VisualUsed::kBorderTop;
            put(node->used.border_top_r, node->used.border_top_g, node->used.border_top_b,
                node->used.border_top_a, v.color);
            break;
        case Prop::BorderRight:
            node->used.flags |= html::VisualUsed::kBorderRight;
            put(node->used.border_right_r, node->used.border_right_g, node->used.border_right_b,
                node->used.border_right_a, v.color);
            break;
        case Prop::BorderBottom:
            node->used.flags |= html::VisualUsed::kBorderBottom;
            put(node->used.border_bottom_r, node->used.border_bottom_g, node->used.border_bottom_b,
                node->used.border_bottom_a, v.color);
            break;
        case Prop::BorderLeft:
            node->used.flags |= html::VisualUsed::kBorderLeft;
            put(node->used.border_left_r, node->used.border_left_g, node->used.border_left_b,
                node->used.border_left_a, v.color);
            break;
        case Prop::Transform:
            node->used.flags |= html::VisualUsed::kTransform;
            node->used.tx = v.xform.tx;
            node->used.ty = v.xform.ty;
            node->used.sx = v.xform.sx;
            node->used.sy = v.xform.sy;
            node->used.rot = v.xform.rot;
            node->used.tx_pct = v.xform.tx_pct;
            node->used.ty_pct = v.xform.ty_pct;
            break;
        case Prop::Width:
            node->used.flags |= html::VisualUsed::kWidth;
            node->used.width = v.length;
            break;
        case Prop::Height:
            node->used.flags |= html::VisualUsed::kHeight;
            node->used.height = v.length;
            break;
        case Prop::MarginTop:
            node->used.flags |= html::VisualUsed::kMarginTop;
            node->used.margin_top = v.length;
            break;
        case Prop::MarginRight:
            node->used.flags |= html::VisualUsed::kMarginRight;
            node->used.margin_right = v.length;
            break;
        case Prop::MarginBottom:
            node->used.flags |= html::VisualUsed::kMarginBottom;
            node->used.margin_bottom = v.length;
            break;
        case Prop::MarginLeft:
            node->used.flags |= html::VisualUsed::kMarginLeft;
            node->used.margin_left = v.length;
            break;
        case Prop::PaddingTop:
            node->used.flags |= html::VisualUsed::kPaddingTop;
            node->used.padding_top = v.length;
            break;
        case Prop::PaddingRight:
            node->used.flags |= html::VisualUsed::kPaddingRight;
            node->used.padding_right = v.length;
            break;
        case Prop::PaddingBottom:
            node->used.flags |= html::VisualUsed::kPaddingBottom;
            node->used.padding_bottom = v.length;
            break;
        case Prop::PaddingLeft:
            node->used.flags |= html::VisualUsed::kPaddingLeft;
            node->used.padding_left = v.length;
            break;
    }
}

void clear_flag(html::Node* node, Prop prop) {
    if (node == nullptr) {
        return;
    }
    html::note_paint_change(node);
    switch (prop) {
        case Prop::Color:
            node->used.flags &= ~html::VisualUsed::kColor;
            break;
        case Prop::Background:
            node->used.flags &= ~html::VisualUsed::kBg;
            break;
        case Prop::BorderTop:
            node->used.flags &= ~html::VisualUsed::kBorderTop;
            break;
        case Prop::BorderRight:
            node->used.flags &= ~html::VisualUsed::kBorderRight;
            break;
        case Prop::BorderBottom:
            node->used.flags &= ~html::VisualUsed::kBorderBottom;
            break;
        case Prop::BorderLeft:
            node->used.flags &= ~html::VisualUsed::kBorderLeft;
            break;
        case Prop::Opacity:
            node->used.flags &= ~html::VisualUsed::kOpacity;
            node->used.opacity = 1;
            break;
        case Prop::Transform:
            node->used.flags &= ~html::VisualUsed::kTransform;
            node->used.tx = 0;
            node->used.ty = 0;
            node->used.sx = 1;
            node->used.sy = 1;
            node->used.rot = 0;
            node->used.tx_pct = false;
            node->used.ty_pct = false;
            break;
        case Prop::Width:
            node->used.flags &= ~html::VisualUsed::kWidth;
            node->used.width = 0;
            break;
        case Prop::Height:
            node->used.flags &= ~html::VisualUsed::kHeight;
            node->used.height = 0;
            break;
        case Prop::MarginTop:
            node->used.flags &= ~html::VisualUsed::kMarginTop;
            node->used.margin_top = 0;
            break;
        case Prop::MarginRight:
            node->used.flags &= ~html::VisualUsed::kMarginRight;
            node->used.margin_right = 0;
            break;
        case Prop::MarginBottom:
            node->used.flags &= ~html::VisualUsed::kMarginBottom;
            node->used.margin_bottom = 0;
            break;
        case Prop::MarginLeft:
            node->used.flags &= ~html::VisualUsed::kMarginLeft;
            node->used.margin_left = 0;
            break;
        case Prop::PaddingTop:
            node->used.flags &= ~html::VisualUsed::kPaddingTop;
            node->used.padding_top = 0;
            break;
        case Prop::PaddingRight:
            node->used.flags &= ~html::VisualUsed::kPaddingRight;
            node->used.padding_right = 0;
            break;
        case Prop::PaddingBottom:
            node->used.flags &= ~html::VisualUsed::kPaddingBottom;
            node->used.padding_bottom = 0;
            break;
        case Prop::PaddingLeft:
            node->used.flags &= ~html::VisualUsed::kPaddingLeft;
            node->used.padding_left = 0;
            break;
    }
}

Value lerp_value(Prop prop, const Value& a, const Value& b, float t) {
    Value o;
    switch (prop) {
        case Prop::Color:
        case Prop::Background:
        case Prop::BorderTop:
        case Prop::BorderRight:
        case Prop::BorderBottom:
        case Prop::BorderLeft:
            o.color = lerp_rgba(a.color, b.color, t);
            break;
        case Prop::Opacity:
            o.opacity = a.opacity + (b.opacity - a.opacity) * t;
            break;
        case Prop::Transform:
            o.xform.tx = a.xform.tx + (b.xform.tx - a.xform.tx) * t;
            o.xform.ty = a.xform.ty + (b.xform.ty - a.xform.ty) * t;
            o.xform.sx = a.xform.sx + (b.xform.sx - a.xform.sx) * t;
            o.xform.sy = a.xform.sy + (b.xform.sy - a.xform.sy) * t;
            o.xform.rot = a.xform.rot + (b.xform.rot - a.xform.rot) * t;
            o.xform.tx_pct = a.xform.tx_pct || b.xform.tx_pct;
            o.xform.ty_pct = a.xform.ty_pct || b.xform.ty_pct;
            break;
        case Prop::Width:
        case Prop::Height:
        case Prop::MarginTop:
        case Prop::MarginRight:
        case Prop::MarginBottom:
        case Prop::MarginLeft:
        case Prop::PaddingTop:
        case Prop::PaddingRight:
        case Prop::PaddingBottom:
        case Prop::PaddingLeft:
            o.length = a.length + (b.length - a.length) * t;
            o.length_ok = true;
            break;
    }
    return o;
}

bool value_eq(Prop prop, const Value& a, const Value& b) {
    switch (prop) {
        case Prop::Color:
        case Prop::Background:
        case Prop::BorderTop:
        case Prop::BorderRight:
        case Prop::BorderBottom:
        case Prop::BorderLeft:
            return rgba_eq(a.color, b.color);
        case Prop::Opacity:
            return std::fabs(a.opacity - b.opacity) < 0.0005f;
        case Prop::Transform:
            return a.xform.tx_pct == b.xform.tx_pct && a.xform.ty_pct == b.xform.ty_pct &&
                   std::fabs(a.xform.tx - b.xform.tx) < 0.05f && std::fabs(a.xform.ty - b.xform.ty) < 0.05f &&
                   std::fabs(a.xform.sx - b.xform.sx) < 0.001f && std::fabs(a.xform.sy - b.xform.sy) < 0.001f &&
                   std::fabs(a.xform.rot - b.xform.rot) < 0.001f;
        case Prop::Width:
        case Prop::Height:
        case Prop::MarginTop:
        case Prop::MarginRight:
        case Prop::MarginBottom:
        case Prop::MarginLeft:
        case Prop::PaddingTop:
        case Prop::PaddingRight:
        case Prop::PaddingBottom:
        case Prop::PaddingLeft:
            if (a.length_auto || b.length_auto || a.length_percent || b.length_percent) {
                return a.length_auto == b.length_auto && a.length_percent == b.length_percent &&
                       std::fabs(a.length - b.length) < 0.05f;
            }
            return a.length_ok == b.length_ok && std::fabs(a.length - b.length) < 0.05f;
    }
    return true;
}

float progress_of(const Record& r, double t) {
    if (r.duration <= 0) {
        return 1;
    }
    const float u = static_cast<float>((t - r.start - static_cast<double>(r.delay)) / static_cast<double>(r.duration));
    return std::max(0.0f, std::min(1.0f, u));
}

const char* css_name(Prop p) {
    switch (p) {
        case Prop::Color:
            return "color";
        case Prop::Background:
            return "background-color";
        case Prop::BorderTop:
            return "border-top-color";
        case Prop::BorderRight:
            return "border-right-color";
        case Prop::BorderBottom:
            return "border-bottom-color";
        case Prop::BorderLeft:
            return "border-left-color";
        case Prop::Opacity:
            return "opacity";
        case Prop::Transform:
            return "transform";
        case Prop::Width:
            return "width";
        case Prop::Height:
            return "height";
        case Prop::MarginTop:
            return "margin-top";
        case Prop::MarginRight:
            return "margin-right";
        case Prop::MarginBottom:
            return "margin-bottom";
        case Prop::MarginLeft:
            return "margin-left";
        case Prop::PaddingTop:
            return "padding-top";
        case Prop::PaddingRight:
            return "padding-right";
        case Prop::PaddingBottom:
            return "padding-bottom";
        case Prop::PaddingLeft:
            return "padding-left";
    }
    return "color";
}

}  // namespace transition_detail

}  // namespace css
}  // namespace zui
