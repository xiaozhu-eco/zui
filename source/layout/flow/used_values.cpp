#include "layout/flow/detail.h"

#include <algorithm>
#include <limits>

namespace zui {
namespace layout {
namespace flow_detail {

const LayoutStyle& style_of(const Box& box)
{
    static const LayoutStyle kEmpty;
    return box.style_ptr != nullptr ? box.style() : kEmpty;
}

void map_physical_auto(WritingMode mode, Direction direction, bool top_auto, bool right_auto,
                       bool bottom_auto, bool left_auto, bool* inline_start_auto,
                       bool* inline_end_auto, bool* block_start_auto, bool* block_end_auto)
{
    Edges flags;
    set_logical_edge(flags, mode, direction, top_auto ? 1.f : 0.f, right_auto ? 1.f : 0.f,
                     bottom_auto ? 1.f : 0.f, left_auto ? 1.f : 0.f);
    if (inline_start_auto != nullptr) {
        *inline_start_auto = flags.inline_start > 0.f;
    }
    if (inline_end_auto != nullptr) {
        *inline_end_auto = flags.inline_end > 0.f;
    }
    if (block_start_auto != nullptr) {
        *block_start_auto = flags.block_start > 0.f;
    }
    if (block_end_auto != nullptr) {
        *block_end_auto = flags.block_end > 0.f;
    }
}

UsedEdges resolve_edges(const LayoutStyle& style, const AxisSize& inline_basis,
                        const html::Node* node)
{
    UsedEdges edges;
    const WritingMode mode = style.writing_mode;
    const Direction direction = style.direction;
    map_physical_auto(mode, direction, style.margin_top_auto, style.margin_right_auto,
                      style.margin_bottom_auto, style.margin_left_auto, &edges.margin_inline_start_auto,
                      &edges.margin_inline_end_auto, &edges.margin_block_start_auto,
                      &edges.margin_block_end_auto);
    float top = used_edge(style, LengthProperty::MarginTop, inline_basis);
    float right = used_edge(style, LengthProperty::MarginRight, inline_basis);
    float bottom = used_edge(style, LengthProperty::MarginBottom, inline_basis);
    float left = used_edge(style, LengthProperty::MarginLeft, inline_basis);
    top = edge_with_overlay(node, html::VisualUsed::kMarginTop,
                            node != nullptr ? node->used.margin_top : 0.f, top);
    right = edge_with_overlay(node, html::VisualUsed::kMarginRight,
                              node != nullptr ? node->used.margin_right : 0.f, right);
    bottom = edge_with_overlay(node, html::VisualUsed::kMarginBottom,
                               node != nullptr ? node->used.margin_bottom : 0.f, bottom);
    left = edge_with_overlay(node, html::VisualUsed::kMarginLeft,
                             node != nullptr ? node->used.margin_left : 0.f, left);
    // A live margin overlay is a definite length: clear that side's auto bit so centering math
    // does not still treat it as free space.
    if (node != nullptr && (node->used.flags & html::VisualUsed::kMargin) != 0) {
        map_physical_auto(
            mode, direction,
            (node->used.flags & html::VisualUsed::kMarginTop) != 0 ? false : style.margin_top_auto,
            (node->used.flags & html::VisualUsed::kMarginRight) != 0 ? false
                                                                    : style.margin_right_auto,
            (node->used.flags & html::VisualUsed::kMarginBottom) != 0 ? false
                                                                     : style.margin_bottom_auto,
            (node->used.flags & html::VisualUsed::kMarginLeft) != 0 ? false : style.margin_left_auto,
            &edges.margin_inline_start_auto, &edges.margin_inline_end_auto,
            &edges.margin_block_start_auto, &edges.margin_block_end_auto);
    }
    const Edges margin = physical_edges_to_logical(mode, direction, top, right, bottom, left);
    edges.margin_inline_start = margin.inline_start;
    edges.margin_inline_end = margin.inline_end;
    edges.margin_block_start = margin.block_start;
    edges.margin_block_end = margin.block_end;

    float pad_top = used_edge(style, LengthProperty::PaddingTop, inline_basis);
    float pad_right = used_edge(style, LengthProperty::PaddingRight, inline_basis);
    float pad_bottom = used_edge(style, LengthProperty::PaddingBottom, inline_basis);
    float pad_left = used_edge(style, LengthProperty::PaddingLeft, inline_basis);
    pad_top = edge_with_overlay(node, html::VisualUsed::kPaddingTop,
                                node != nullptr ? node->used.padding_top : 0.f, pad_top);
    pad_right = edge_with_overlay(node, html::VisualUsed::kPaddingRight,
                                  node != nullptr ? node->used.padding_right : 0.f, pad_right);
    pad_bottom = edge_with_overlay(node, html::VisualUsed::kPaddingBottom,
                                   node != nullptr ? node->used.padding_bottom : 0.f, pad_bottom);
    pad_left = edge_with_overlay(node, html::VisualUsed::kPaddingLeft,
                                 node != nullptr ? node->used.padding_left : 0.f, pad_left);
    const Edges padding =
        physical_edges_to_logical(mode, direction, pad_top, pad_right, pad_bottom, pad_left);
    edges.padding_inline_start = padding.inline_start;
    edges.padding_inline_end = padding.inline_end;
    edges.padding_block_start = padding.block_start;
    edges.padding_block_end = padding.block_end;
    return edges;
}

Edges border_edges(const LayoutStyle& style)
{
    return physical_edges_to_logical(style.writing_mode, style.direction, style.border_top,
                                     style.border_right, style.border_bottom, style.border_left);
}

float chrome_inline(const LayoutStyle& style, const UsedEdges& edges)
{
    const Edges border = border_edges(style);
    return border.inline_start + border.inline_end + edges.padding_inline_start +
           edges.padding_inline_end;
}

float chrome_block(const LayoutStyle& style, const UsedEdges& edges)
{
    const Edges border = border_edges(style);
    return border.block_start + border.block_end + edges.padding_block_start +
           edges.padding_block_end;
}

float box_content(float specified, float chrome, bool border_box)
{
    if (!border_box) {
        return std::max(0.f, specified);
    }
    return std::max(0.f, specified - chrome);
}

float clamp_used_content(float content, float min_size, bool min_auto, float max_size,
                         bool border_box, float chrome)
{
    // CSS Sizing: the maximum is applied, then the minimum, so a minimum that is
    // larger than the maximum wins.
    float used = content;
    if (max_size < 1.0e8f) {
        used = std::min(used, box_content(max_size, chrome, border_box));
    }
    if (!min_auto) {
        used = std::max(used, box_content(min_size, chrome, border_box));
    }
    return used;
}

float content_inline_origin(WritingMode mode, Direction direction, const UsedEdges& edges)
{
    // Horizontal-tb stores a physical x. Under rtl, padding-left is the logical inline-end.
    if (is_horizontal_wm(mode) && direction == Direction::Rtl) {
        return edges.padding_inline_end;
    }
    return edges.padding_inline_start;
}

float used_line_height(const LayoutStyle& style, const LayoutContext::FontEm& em)
{
    const float font_box = std::max(0.f, em.ascent + em.descent);
    float used = font_box;
    if (style.line_height_kind == LineHeightKind::Number) {
        used = std::max(0.f, style.line_height_value * style.font_size);
    } else if (style.line_height_kind == LineHeightKind::Percent) {
        used = std::max(0.f, style.font_size * style.line_height_value / 100.f);
    } else if (style.line_height_kind == LineHeightKind::Length) {
        used = std::max(0.f, style.line_height_value);
    }
    // Used values land on the 1/64 quantum a browser's layout uses, floored: `line-height: 1.45`
    // at 16px is 23.2px of style but a 23.1875px line, and five of those are what makes a
    // paragraph's height match (measured on example/tests/text's `.sample`).
    return std::floor(used * 64.f) / 64.f;
}

float text_input_baseline(const Box& box, const LayoutStyle& style, const LayoutResult& result,
                          const LayoutContext::FontEm& em) {
    if (box.node == nullptr || box.node->tag != "input") {
        return std::numeric_limits<float>::quiet_NaN();
    }
    const auto type = box.node->attrs.find("type");
    const std::string kind = type == box.node->attrs.end() || type->second.empty()
                                 ? std::string("text")
                                 : type->second;
    const bool text_control = kind == "text" || kind == "search" || kind == "email" ||
                              kind == "url" || kind == "tel" || kind == "password" ||
                              kind == "number";
    if (!text_control) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    // The inner text is centred in the line-height box. `overflow: clip` (the UA value on a
    // text field) makes the atomic baseline fall back to the bottom edge, and a 36px search
    // field then grew its line by the parent's leftover descent (41.5 on the contacts label).
    const float font_box = em.ascent + em.descent;
    const float half = std::max(0.f, (used_line_height(style, em) - font_box) * 0.5f);
    return result.geometry.border.block_start + result.geometry.padding.block_start + em.ascent +
           half;
}

float used_indent(const LayoutStyle& style, const LayoutKey& key)
{
    if (!style.text_indent_percent) {
        return style.text_indent_px;
    }
    if (!key.space.percentage.inline_size.definite) {
        return 0;
    }
    return key.space.percentage.inline_size.px * style.text_indent_px / 100.f;
}

// LayoutUnit floors a positive length onto the 1/64 px grid. Blink's sub/super offsets
// are `font-size/5 + 1` and `font-size/3 + 1` of the parent, then that conversion.
float floor_layout_unit(float value)
{
    return std::floor(value * 64.f) / 64.f;
}

// Positive raises the box. sub, super, and middle are the parent's font, matching Blink:
// sub lowers by floor(font/5 + 1), super raises by floor(font/3 + 1), and middle moves the
// box so its midpoint meets the parent's baseline plus half the parent's x-height.
float align_shift(const LayoutStyle& line_style, const LayoutStyle& child_style,
                  LayoutContext& cx)
{
    if (child_style.vertical_align == VerticalAlign::Sub) {
        return -floor_layout_unit(line_style.font_size / 5.f + 1.f);
    }
    if (child_style.vertical_align == VerticalAlign::Super) {
        return floor_layout_unit(line_style.font_size / 3.f + 1.f);
    }
    if (child_style.vertical_align == VerticalAlign::Middle) {
        const LayoutContext::FontEm child = cx.font_em(child_style);
        const LayoutContext::FontEm parent = cx.font_em(line_style);
        // Blink divides the font's (ascent - descent) in integer pixels, so 12.5 becomes 12.
        const float midpoint = std::floor((child.ascent - child.descent) * 0.5f);
        const float half_x = std::round(parent.x_height * 0.5f * 64.f) / 64.f;
        return -(midpoint - half_x);
    }
    if (child_style.vertical_align != VerticalAlign::Length) {
        return 0;
    }
    if (!child_style.vertical_align_percent) {
        return child_style.vertical_align_px;
    }
    return used_line_height(child_style, cx.font_em(child_style)) * child_style.vertical_align_px /
           100.f;
}

void apply_relative(Box& child, float cb_inline, bool inline_known, const AxisSize& cb_block)
{
    const LayoutStyle& style = style_of(child);
    if (!style.position_relative) {
        return;
    }
    // Physical insets: left/right % use the containing block's physical width, top/bottom its
    // physical height (CSS 2.1 §9.4.3). Placement is logical, so map through writing-mode +
    // direction; when both ends of a logical axis are set, the start side wins.
    const WritingMode mode = style.writing_mode;
    const Direction direction = style.direction;
    AxisSize width_basis;
    AxisSize height_basis;
    if (is_horizontal_wm(mode)) {
        width_basis = AxisSize{inline_known, cb_inline};
        height_basis = cb_block;
    } else {
        width_basis = cb_block;
        height_basis = AxisSize{inline_known, cb_inline};
    }
    bool top_auto = false;
    bool right_auto = false;
    bool bottom_auto = false;
    bool left_auto = false;
    const float top = used_main(style, LengthProperty::Top, height_basis, &top_auto);
    const float right = used_main(style, LengthProperty::Right, width_basis, &right_auto);
    const float bottom = used_main(style, LengthProperty::Bottom, height_basis, &bottom_auto);
    const float left = used_main(style, LengthProperty::Left, width_basis, &left_auto);
    bool inline_start_auto = true;
    bool inline_end_auto = true;
    bool block_start_auto = true;
    bool block_end_auto = true;
    map_physical_auto(mode, direction, top_auto, right_auto, bottom_auto, left_auto,
                      &inline_start_auto, &inline_end_auto, &block_start_auto, &block_end_auto);
    const Edges offsets =
        physical_edges_to_logical(mode, direction, top, right, bottom, left);
    if (is_horizontal_wm(mode)) {
        // The stored inline offset is a physical x. `left` moves the box right and `right`
        // moves it left, in both directions. CSS 2.1 §9.4.3: when both are set, ltr uses
        // left and rtl ignores left and shifts by -right.
        if (!left_auto && !right_auto) {
            if (direction == Direction::Rtl) {
                child.placement.cb_offset.inline_offset -= right;
            } else {
                child.placement.cb_offset.inline_offset += left;
            }
        } else if (!left_auto) {
            child.placement.cb_offset.inline_offset += left;
        } else if (!right_auto) {
            child.placement.cb_offset.inline_offset -= right;
        }
    } else if (!inline_start_auto) {
        child.placement.cb_offset.inline_offset += offsets.inline_start;
    } else if (!inline_end_auto) {
        child.placement.cb_offset.inline_offset -= offsets.inline_end;
    }
    if (!block_start_auto) {
        child.placement.cb_offset.block_offset += offsets.block_start;
    } else if (!block_end_auto) {
        child.placement.cb_offset.block_offset -= offsets.block_end;
    }
}

void write_edges(BoxGeometry& geometry, const LayoutStyle& style, const UsedEdges& edges)
{
    geometry.margin.inline_start = edges.margin_inline_start;
    geometry.margin.inline_end = edges.margin_inline_end;
    geometry.margin.block_start = edges.margin_block_start;
    geometry.margin.block_end = edges.margin_block_end;
    geometry.border = border_edges(style);
    geometry.padding.inline_start = edges.padding_inline_start;
    geometry.padding.inline_end = edges.padding_inline_end;
    geometry.padding.block_start = edges.padding_block_start;
    geometry.padding.block_end = edges.padding_block_end;
}

ConstraintSpace child_space(const LayoutKey& key, const UsedEdges& edges, float content_inline,
                            bool inline_known, const AxisSize& inline_available,
                            const std::shared_ptr<const ExclusionSpace>& exclusion,
                            const AxisSize& block_available, const AxisSize& block_percent,
                            LogicalPoint bfc_origin, WritingMode writing_mode,
                            Direction direction)
{
    ConstraintSpace space;
    space.available.inline_size = inline_available;
    space.available.block_size = block_available;
    if (inline_known) {
        // The containing block of an in-flow child is the parent's *content* box (CSS 2.1 section
        // 10.1), so percentages on the child resolve against `content_inline`. Only an absolutely
        // positioned child measures against the padding box, and the out-of-flow path builds that
        // space itself.
        space.percentage.inline_size = {true, std::max(0.f, content_inline)};
    }
    space.percentage.block_size = block_percent;
    space.containing_block.inline_size = space.percentage.inline_size;
    space.containing_block.block_size = space.percentage.block_size;
    space.containing_block.fixed_is_viewport = key.space.containing_block.fixed_is_viewport;
    space.bfc_origin = bfc_origin;
    space.request.kind = RequestKind::Final;
    space.request.axis = Axis::Block;
    space.writing_mode = writing_mode;
    space.direction = direction;
    space.exclusion = shared_exclusion(exclusion);
    space.fragmentation = key.space.fragmentation;
    space.allow_break = key.space.allow_break;
    space.icb = key.space.icb;
    return space;
}

}  // namespace flow_detail
}  // namespace layout
}  // namespace zui
