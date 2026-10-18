#include "layout/cache/in_flow.h"

#include "layout/box.h"
#include "layout/cache/box_cache.h"
#include "layout/cache/exclusion_space.h"
#include "layout/cache/inline_content.h"
#include "layout/cache/layout_context.h"
#include "layout/cache/layout_key.h"
#include "layout/cache/line_box.h"
#include "layout/cache/logical_axes.h"
#include "layout/cache/sticky.h"
#include "layout/cache/used_length.h"
#include "layout/intern_grid.h"
#include "layout/overflow_mode.h"
#include "text/font_chain.h"

#include "css/values.h"

#include <algorithm>
#include <memory>

namespace zui {
namespace layout {
namespace {

const LayoutStyle& style_of(const Box& box) {
    static const LayoutStyle kEmpty;
    return box.style_ptr != nullptr ? box.style() : kEmpty;
}

// `width: calc(100% - 80px)` and `min()` / `max()` / `clamp()` mix a percentage with a
// length. That has no value until the containing block is known, so the interner leaves
// the axis auto and the used size is resolved here.
float math_used(const Box& box, const char* prop, float font, float basis, float icb_w,
                float icb_h) {
    if (box.node == nullptr) {
        return -1.f;
    }
    const std::string* raw = box.node->style(prop);
    if (raw == nullptr || raw->empty()) {
        return -1.f;
    }
    const bool math = raw->rfind("calc(", 0) == 0 || raw->rfind("min(", 0) == 0 ||
                      raw->rfind("max(", 0) == 0 || raw->rfind("clamp(", 0) == 0;
    if (!math) {
        return -1.f;
    }
    // `rem` inside a math function is the root element's font size, like everywhere else; passing
    // the element's own `font` for both made `width: min(41rem, 100%)` 41 x the element's font size
    // (a 19px reading column came out 779px instead of the browser's 656px).
    const css::ResolvedLength resolved =
        css::resolve_length_basis(*raw, font, root_font_size_px(box.node), icb_w, icb_h, basis);
    if (!resolved.valid || resolved.is_percent) {
        return -1.f;
    }
    return std::max(0.f, resolved.px);
}

struct UsedEdges {
    float margin_inline_start = 0;
    float margin_inline_end = 0;
    float margin_block_start = 0;
    float margin_block_end = 0;
    float padding_inline_start = 0;
    float padding_inline_end = 0;
    float padding_block_start = 0;
    float padding_block_end = 0;
};

UsedEdges resolve_edges(const LayoutStyle& style, const AxisSize& inline_basis,
                        const html::Node* node = nullptr) {
    UsedEdges edges;
    const WritingMode mode = style.writing_mode;
    const Direction direction = style.direction;
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

Edges border_edges_of(const LayoutStyle& style) {
    return physical_edges_to_logical(style.writing_mode, style.direction, style.border_top,
                                     style.border_right, style.border_bottom, style.border_left);
}

float chrome_inline(const LayoutStyle& style, const UsedEdges& edges) {
    const Edges border = border_edges_of(style);
    return border.inline_start + border.inline_end + edges.padding_inline_start +
           edges.padding_inline_end;
}

float chrome_block(const LayoutStyle& style, const UsedEdges& edges) {
    const Edges border = border_edges_of(style);
    return border.block_start + border.block_end + edges.padding_block_start +
           edges.padding_block_end;
}

void write_edges(BoxGeometry& geometry, const LayoutStyle& style, const UsedEdges& edges) {
    geometry.margin.inline_start = edges.margin_inline_start;
    geometry.margin.inline_end = edges.margin_inline_end;
    geometry.margin.block_start = edges.margin_block_start;
    geometry.margin.block_end = edges.margin_block_end;
    geometry.border = border_edges_of(style);
    geometry.padding.inline_start = edges.padding_inline_start;
    geometry.padding.inline_end = edges.padding_inline_end;
    geometry.padding.block_start = edges.padding_block_start;
    geometry.padding.block_end = edges.padding_block_end;
}

bool scrolls(const LayoutStyle& style) {
    // `overflow: clip` clips without a scroll container (CSS Overflow 3).
    auto scrollable = [](OverflowMode mode) {
        return mode == OverflowMode::Auto || mode == OverflowMode::Scroll ||
               mode == OverflowMode::Hidden;
    };
    return scrollable(style.overflow_x) || scrollable(style.overflow_y);
}

std::shared_ptr<const ExclusionSpace>
shared_exclusion(const std::shared_ptr<const ExclusionSpace>& current) {
    if (current) {
        return current;
    }
    static const auto empty = std::make_shared<ExclusionSpace>();
    return empty;
}

bool out_of_flow(const Box& box) {
    const LayoutStyle& style = style_of(box);
    return style.position_absolute || style.position_fixed;
}

ConstraintSpace child_space(const LayoutKey& key, const LayoutStyle& parent_style,
                            const UsedEdges& edges, float content_inline, float content_block,
                            bool block_definite,
                            const std::shared_ptr<const ExclusionSpace>& exclusion, Axis main) {
    ConstraintSpace space;
    const bool inline_main = main == Axis::Inline;
    space.available.inline_size =
        inline_main ? AxisSize{false, 0} : AxisSize{true, std::max(0.f, content_inline)};
    space.available.block_size =
        block_definite ? AxisSize{true, std::max(0.f, content_block)} : AxisSize{false, 0};
    if (inline_main && block_definite) {
        space.available.block_size = {true, std::max(0.f, content_block)};
    }
    const float padding_inline =
        std::max(0.f, content_inline + edges.padding_inline_start + edges.padding_inline_end);
    const float padding_block =
        std::max(0.f, content_block + edges.padding_block_start + edges.padding_block_end);
    space.percentage.inline_size = {true, padding_inline};
    space.percentage.block_size =
        block_definite ? AxisSize{true, padding_block} : AxisSize{false, 0};
    space.containing_block.inline_size = space.percentage.inline_size;
    space.containing_block.block_size = space.percentage.block_size;
    space.request.kind = RequestKind::Final;
    space.request.axis = main;
    space.writing_mode = parent_style.writing_mode;
    space.direction = parent_style.direction;
    space.exclusion = exclusion;
    space.fragmentation = key.space.fragmentation;
    space.allow_break = key.space.allow_break;
    space.icb = key.space.icb;
    return space;
}

void layout_text(Box& box, const LayoutKey& key, LayoutContext& cx, LayoutResult& out) {
    std::vector<ShapedRun> runs = cx.shape(box.text, style_of(box), key.space.direction);
    box.depends_on_font = true;
    float advance = 0;
    for (const ShapedRun& run : runs) {
        advance += run.advance_inline;
    }
    const float size = style_of(box).font_size;
    // CSS Text 3: letter-spacing is added after every character, including the last one, so a text
    // box is one spacing per character wider than the sum of its glyph advances.
    std::size_t glyphs = 0;
    for (const ShapedRun& run : runs) {
        glyphs += run.glyphs.size();
    }
    advance += style_of(box).letter_spacing * static_cast<float>(glyphs);
    const float word_spacing = style_of(box).word_spacing;
    if (word_spacing != 0.f) {
        for (const ShapedRun& run : runs) {
            std::size_t byte = 0;
            for (const GlyphInline& glyph : run.glyphs) {
                if (glyph.forced_break || byte >= run.text.size()) {
                    continue;
                }
                const std::uint32_t cp = text::next_cp(run.text, byte);
                if (cp == ' ' || cp == 0xA0) {
                    advance += word_spacing;
                }
            }
        }
    }
    out.geometry.content_inline = advance;
    out.geometry.content_block = size;
    out.geometry.border_box_inline = advance;
    out.geometry.border_box_block = size;
    out.geometry.baselines.first = size;
    out.geometry.baselines.last = size;
    auto lines = std::make_unique<InlineContent>();
    LineBox line;
    line.inline_size = advance;
    line.block_offset = 0;
    line.baseline = size;
    line.runs = std::move(runs);
    // Same as the flow path's text layout: a standalone text box is a single line, so each run's
    // origin is the advance of the runs before it. Without it every run is drawn at the line start.
    float cursor = 0;
    const float spacing = style_of(box).letter_spacing;
    for (ShapedRun& run : line.runs) {
        run.has_origin = true;
        run.origin = cursor;
        cursor += run.advance_inline + spacing * static_cast<float>(run.glyphs.size());
    }
    lines->lines.push_back(std::move(line));
    out.lines = std::move(lines);
}

void layout_replaced(Box& box, const LayoutKey& key, LayoutContext& cx, LayoutResult& out) {
    ReplacedRequest request;
    request.available = key.space.available;
    request.percentage = key.space.percentage;
    request.request = key.space.request;
    request.writing_mode = key.space.writing_mode;
    request.direction = key.space.direction;
    request.icb = key.space.icb;
    const ReplacedMeasurement measured = cx.measure_replaced(box, request);
    const LayoutStyle& style = style_of(box);
    const UsedEdges edges = resolve_edges(style, key.space.percentage.inline_size, box.node);
    write_edges(out.geometry, style, edges);
    bool width_auto = false;
    bool height_auto = false;
    float width_used = used_main(style.width_auto, style.width_percent, style.width_px,
                                 key.space.percentage.inline_size, &width_auto);
    float height_used = used_main(style.height_auto, style.height_percent, style.height_px,
                                  key.space.percentage.block_size, &height_auto);
    apply_size_overlays(box.node, width_used, width_auto, height_used, height_auto);
    float width = width_auto ? -1.f : width_used;
    float height = height_auto ? -1.f : height_used;
    if (measured.resource_ready && measured.has_intrinsic_ratio && measured.intrinsic_ratio > 0.f) {
        if (width < 0.f && height < 0.f) {
            width = measured.intrinsic_width;
            height = measured.intrinsic_height;
        } else if (width < 0.f) {
            width = height * measured.intrinsic_ratio;
        } else if (height < 0.f) {
            height = width / measured.intrinsic_ratio;
        }
    } else if (!measured.resource_ready) {
        if (width < 0.f && height < 0.f) {
            width = 300.f;
            height = 150.f;
        } else if (width < 0.f) {
            width = 300.f;
        } else if (height < 0.f) {
            height = 150.f;
        }
    } else {
        if (width < 0.f) {
            width = measured.has_intrinsic_width ? measured.intrinsic_width : 300.f;
        }
        if (height < 0.f) {
            height = measured.has_intrinsic_height ? measured.intrinsic_height : 150.f;
        }
    }
    out.geometry.content_inline = width;
    out.geometry.content_block = height;
    out.geometry.border_box_inline = width + chrome_inline(style, edges);
    out.geometry.border_box_block = height + chrome_block(style, edges);
}

}  // namespace

LayoutResult InFlowPass::layout(Box& box, const LayoutKey& key, LayoutContext& cx, Axis main) {
    LayoutResult out;
    const LayoutStyle& style = style_of(box);
    const UsedEdges edges = resolve_edges(style, key.space.percentage.inline_size, box.node);
    write_edges(out.geometry, style, edges);
    out.margin_strut.start = edges.margin_block_start;
    out.margin_strut.end = edges.margin_block_end;
    out.converged = true;
    if (box.role == BoxRole::Text) {
        layout_text(box, key, cx, out);
        return out;
    }
    if (box.is_replaced) {
        layout_replaced(box, key, cx, out);
        return out;
    }

    bool width_auto = false;
    bool height_auto = false;
    float width_used = used_main(style.width_auto, style.width_percent, style.width_px,
                                 key.space.percentage.inline_size, &width_auto);
    float height_used = used_main(style.height_auto, style.height_percent, style.height_px,
                                  key.space.percentage.block_size, &height_auto);
    apply_size_overlays(box.node, width_used, width_auto, height_used, height_auto);
    const float icb_w = key.space.icb.inline_size;
    const float icb_h = key.space.icb.block_size;
    if (width_auto && key.space.percentage.inline_size.definite) {
        const float math = math_used(box, "width", style.font_size,
                                     key.space.percentage.inline_size.px, icb_w, icb_h);
        if (math >= 0.f) {
            width_used = math;
            width_auto = false;
        }
    }
    if (height_auto) {
        const float basis =
            key.space.percentage.block_size.definite ? key.space.percentage.block_size.px : -1.f;
        const float math = math_used(box, "height", style.font_size, basis, icb_w, icb_h);
        if (math >= 0.f) {
            height_used = math;
            height_auto = false;
        }
    }
    // CSS Sizing section 5.2: a percentage whose basis is indefinite behaves as `auto`, so it must
    // not be read as a used zero (see the matching rule in flow.cpp).
    if (!width_auto && style.width_percent && !key.space.percentage.inline_size.definite) {
        width_auto = true;
        width_used = 0;
    }
    if (!height_auto && style.height_percent && !key.space.percentage.block_size.definite) {
        height_auto = true;
        height_used = 0;
    }
    const float inline_chrome = chrome_inline(style, edges);
    const float block_chrome = chrome_block(style, edges);
    const bool skip_children =
        style.content_visibility_hidden || (style.content_visibility_auto && box.contents_skipped);
    const bool inline_contained =
        style.contain_inline_size || style.container_type != ContainerType::Normal;
    const bool block_contained =
        style.contain_block_size || style.container_type == ContainerType::Size;
    const bool lock_inline =
        inline_contained && width_auto && !key.space.available.inline_size.definite;
    const bool lock_block = block_contained && height_auto;
    const bool inline_definite = !width_auto || key.space.available.inline_size.definite;
    const bool block_definite =
        !height_auto || (key.space.available.block_size.definite && box.children.empty());
    float content_inline = 0;
    float content_block = 0;
    if (!width_auto) {
        content_inline = width_used;
    } else if (key.space.available.inline_size.definite && !lock_inline) {
        content_inline = std::max(0.f, key.space.available.inline_size.px - inline_chrome);
    }
    if (!height_auto) {
        content_block = height_used;
    } else if (!lock_block && block_definite && key.space.available.block_size.definite) {
        content_block = std::max(0.f, key.space.available.block_size.px - block_chrome);
    }
    if (skip_children) {
        // The placeholder stands in for a size the content would have decided; a size taken from a
        // definite available size (or a stretched item) is not one. See the same rule in
        // `flow.cpp`.
        if (width_auto && (lock_inline || !key.space.available.inline_size.definite)) {
            content_inline =
                style.contain_intrinsic_inline >= 0 ? style.contain_intrinsic_inline : 0;
        }
        if (height_auto &&
            (lock_block || !block_definite || !key.space.available.block_size.definite)) {
            content_block = style.contain_intrinsic_block >= 0 ? style.contain_intrinsic_block : 0;
        }
    }
    // CSS Sizing section 5.1: a shrink-to-fit box with a definite block size and an `auto` inline
    // size lets the ratio decide the inline size, so `#h-driven { height: 90px;
    // aspect-ratio: 16 / 9 }` is 160 wide even with no content. An indefinite available inline
    // size is exactly the shrink-to-fit condition (a block-level box in normal flow is handed its
    // container's width instead and stretches, CSS 2.1 section 10.3.3), and a width the box states
    // itself wins because `width_auto` is false then.
    if (style.aspect_ratio > 0.f && width_auto && !lock_inline && !height_auto &&
        height_used > 0.f && !key.space.available.inline_size.definite) {
        content_inline =
            std::max(0.f, (content_block + block_chrome) * style.aspect_ratio - inline_chrome);
    }

    LogicalPoint cursor;
    const bool inline_main = main == Axis::Inline;
    std::shared_ptr<const ExclusionSpace> exclusion = shared_exclusion(key.space.exclusion);
    const AxisSize child_margin_basis =
        width_auto && !key.space.available.inline_size.definite
            ? AxisSize{false, 0}
            : AxisSize{true, std::max(0.f, content_inline + edges.padding_inline_start +
                                               edges.padding_inline_end)};
    if (!skip_children) {
        for (Box* child : box.children) {
            if (child == nullptr) {
                continue;
            }
            const LayoutStyle& child_style = style_of(*child);
            const float child_margin_inline =
                used_edge(child_style, LengthProperty::MarginLeft, child_margin_basis);
            const float child_margin_inline_end =
                used_edge(child_style, LengthProperty::MarginRight, child_margin_basis);
            const float child_margin_block =
                used_edge(child_style, LengthProperty::MarginTop, child_margin_basis);
            const float child_margin_block_end =
                used_edge(child_style, LengthProperty::MarginBottom, child_margin_basis);
            LogicalPoint local = cursor;
            if (cx.origin_at_padding_edge()) {
                local.inline_offset += style.border_left + edges.padding_inline_start;
                local.block_offset += style.border_top + edges.padding_block_start;
            }
            local.inline_offset += child_margin_inline;
            local.block_offset += child_margin_block;
            // Border-box origin in this box's padding box. This parent is the in-flow containing block.
            LogicalPoint placed;
            placed.inline_offset =
                cursor.inline_offset + edges.padding_inline_start + child_margin_inline;
            placed.block_offset =
                cursor.block_offset + edges.padding_block_start + child_margin_block;
            if (out_of_flow(*child)) {
                child->placement = {};
                cx.note_out_of_flow(*child, local);
                continue;
            }
            child->placement.cb_offset = placed;
            child->placement.sticky_offset = {};
            LogicalPoint content = local;
            content.inline_offset +=
                child_style.border_left +
                used_edge(child_style, LengthProperty::PaddingLeft, child_margin_basis);
            content.block_offset +=
                child_style.border_top +
                used_edge(child_style, LengthProperty::PaddingTop, child_margin_basis);
            cx.push_content_origin(content);
            const bool child_block_definite = !height_auto || block_definite || lock_block;
            const LayoutResult* child_result =
                cx.ask(*child, child_space(key, style, edges, content_inline, content_block,
                                           child_block_definite, exclusion, main));
            cx.pop_content_origin();
            if (child_result != nullptr) {
                exclusion = ExclusionSpace::adopt(exclusion, child_result->floats);
            }
            const float child_inline =
                child_result != nullptr ? child_result->geometry.border_box_inline : 0;
            const float child_block =
                child_result != nullptr ? child_result->geometry.border_box_block : 0;
            if (inline_main) {
                cursor.inline_offset +=
                    child_margin_inline + child_inline + child_margin_inline_end;
                if (!lock_block) {
                    content_block = std::max(content_block, child_block);
                }
            } else {
                cursor.block_offset += child_margin_block + child_block + child_margin_block_end;
                if (!lock_inline) {
                    content_inline = std::max(content_inline, child_margin_inline + child_inline +
                                                                  child_margin_inline_end);
                }
            }
        }
    }

    if (!lock_inline && width_auto && !key.space.available.inline_size.definite) {
        content_inline = std::max(content_inline, cursor.inline_offset);
    } else if (!lock_inline && inline_main && width_auto) {
        content_inline = std::max(content_inline, cursor.inline_offset);
    }
    if (!lock_block && height_auto && !block_definite && !skip_children) {
        content_block = inline_main ? content_block : cursor.block_offset;
    } else if (!lock_block && height_auto && box.children.empty() &&
               key.space.available.block_size.definite) {
        content_block = std::max(0.f, key.space.available.block_size.px - block_chrome);
    }
    if (!lock_block && !inline_main && height_auto && !box.children.empty() && !skip_children) {
        content_block = cursor.block_offset;
    }

    out.geometry.content_inline = std::max(0.f, content_inline);
    out.geometry.content_block = std::max(0.f, content_block);
    out.geometry.border_box_inline = out.geometry.content_inline + inline_chrome;
    out.geometry.border_box_block = out.geometry.content_block + block_chrome;
    if (style.visibility_collapse && style.table_row) {
        out.geometry.content_block = 0;
        out.geometry.border_box_block = 0;
    }
    if (style.visibility_collapse && style.table_column) {
        // CSS 2.1 §17.5.5: the column disappears. Keep content_block so CSSOM clientHeight /
        // scrollHeight can still report the track size (Chrome keeps those; border box is 0).
        out.geometry.content_inline = 0;
        out.geometry.border_box_inline = 0;
        out.geometry.border_box_block = 0;
        out.overflow.scrollable = {};
        out.overflow.ink = {};
    }
    if (scrolls(style) && box.node != nullptr) {
        const float cb_inline =
            out.geometry.border_box_inline - style.border_left - style.border_right;
        const float cb_block =
            out.geometry.border_box_block - style.border_top - style.border_bottom;
        for (Box* child : box.children) {
            if (child == nullptr || child->style_ptr == nullptr ||
                !child->style().position_sticky || out_of_flow(*child)) {
                continue;
            }
            float child_inline = 0;
            float child_block = 0;
            if (child->cache != nullptr && child->cache->pinned_final() != nullptr) {
                child_inline = child->cache->pinned_final()->geometry.border_box_inline;
                child_block = child->cache->pinned_final()->geometry.border_box_block;
            }
            const WritingMode wm = style.writing_mode;
            const float scroll_inline =
                is_horizontal_wm(wm) ? box.node->scroll_x : box.node->scroll_y;
            const float scroll_block =
                is_horizontal_wm(wm) ? box.node->scroll_y : box.node->scroll_x;
            child->placement.sticky_offset =
                sticky_shift(child->style(), child->placement.cb_offset, child_inline, child_block,
                             cb_inline, cb_block, scroll_inline, scroll_block);
        }
    }
    return out;
}

}  // namespace layout
}  // namespace zui
