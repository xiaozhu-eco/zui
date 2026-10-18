#ifndef ZUI_SOURCE_LAYOUT_CACHE_LAYOUT_CONTEXT_INTERNAL_H
#define ZUI_SOURCE_LAYOUT_CACHE_LAYOUT_CONTEXT_INTERNAL_H

#include "layout/box.h"
#include "layout/cache/layout_result.h"
#include "layout/cache/used_length.h"
#include "html/document.h"
#include "trace/trace.h"

#include <chrono>
#include <string_view>

namespace zui {
namespace layout {
namespace layout_context_detail {

inline double ms_since(const std::chrono::steady_clock::time_point& start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
        .count();
}

struct AlgoFrame {
    explicit AlgoFrame(int) {}
};

inline zui::trace::BoxRef box_ref(const Box& box, const char* kind) {
    const html::Node* node = box.node;
    return zui::trace::BoxRef{node != nullptr ? std::string_view(node->tag) : std::string_view(),
                              node != nullptr ? std::string_view(node->id) : std::string_view(),
                              node != nullptr ? std::string_view(node->class_name)
                                              : std::string_view(),
                              std::string_view(kind)};
}

inline bool is_query_container(const Box& box) {
    return box.style_ptr != nullptr && box.style().container_type != ContainerType::Normal;
}

inline LayoutResult synthetic_container(const Box& box, const ConstraintSpace& space) {
    LayoutResult out;
    out.converged = true;
    out.margin_strut.blocks_margin_collapse = true;
    const LayoutStyle& style = box.style();
    bool width_auto = false;
    bool height_auto = false;
    const float width = used_main(style.width_auto, style.width_percent, style.width_px,
                                  space.percentage.inline_size, &width_auto);
    const float height = used_main(style.height_auto, style.height_percent, style.height_px,
                                   space.percentage.block_size, &height_auto);
    const float pad_l = used_edge(style, LengthProperty::PaddingLeft, space.percentage.inline_size);
    const float pad_r =
        used_edge(style, LengthProperty::PaddingRight, space.percentage.inline_size);
    const float pad_t = used_edge(style, LengthProperty::PaddingTop, space.percentage.inline_size);
    const float pad_b =
        used_edge(style, LengthProperty::PaddingBottom, space.percentage.inline_size);
    const float chrome_x = pad_l + pad_r + style.border_left + style.border_right;
    const float chrome_y = pad_t + pad_b + style.border_top + style.border_bottom;
    const float margin_l =
        used_edge(style, LengthProperty::MarginLeft, space.percentage.inline_size);
    const float margin_r =
        used_edge(style, LengthProperty::MarginRight, space.percentage.inline_size);
    const float margin_t =
        used_edge(style, LengthProperty::MarginTop, space.percentage.inline_size);
    const float margin_b =
        used_edge(style, LengthProperty::MarginBottom, space.percentage.inline_size);
    float content_w = 0;
    if (!width_auto) {
        content_w = style.border_box ? std::max(0.f, width - chrome_x) : std::max(0.f, width);
    } else if (space.available.inline_size.definite) {
        content_w = std::max(0.f, space.available.inline_size.px - margin_l - margin_r - chrome_x);
    }
    float content_h = 0;
    if (!height_auto) {
        content_h = style.border_box ? std::max(0.f, height - chrome_y) : std::max(0.f, height);
    }
    out.geometry.margin = {margin_l, margin_r, margin_t, margin_b};
    out.geometry.border = {style.border_left, style.border_right, style.border_top,
                           style.border_bottom};
    out.geometry.padding = {pad_l, pad_r, pad_t, pad_b};
    out.geometry.content_inline = content_w;
    out.geometry.content_block = content_h;
    out.geometry.border_box_inline = content_w + chrome_x;
    out.geometry.border_box_block = content_h + chrome_y;
    out.margin_strut.start = margin_t;
    out.margin_strut.end = margin_b;
    return out;
}

}  // namespace layout_context_detail
}  // namespace layout
}  // namespace zui

#endif
