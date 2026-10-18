// Atomic in-flow content: text, replaced elements and simple ruby (they lay out as a whole
// inside the block that owns them, not through the line walk).

#include "layout/flow/detail.h"

namespace zui {
namespace layout {
namespace flow_detail {

void layout_text(Box& box, const LayoutKey& key, LayoutContext& cx, LayoutResult& out)
{
    const LayoutStyle& text_style = style_of(box);
    std::vector<ShapedRun> runs = cx.shape(box.text, text_style, key.space.direction);
    box.depends_on_font = true;
    // CSS Text 3: under `normal` / `nowrap` / `pre-line` a trailing run of collapsible
    // spaces hangs, so it is not part of the max-content width. A flex item's text is
    // measured here instead of by the line breaker, and counting the spaces after "相册"
    // made the album lightbox columns ~32px too wide.
    const bool collapse = text_style.white_space == WhiteSpace::Normal ||
                          text_style.white_space == WhiteSpace::Nowrap ||
                          text_style.white_space == WhiteSpace::PreLine;
    std::size_t trailing = 0;
    if (collapse) {
        bool stopped = false;
        for (int r = static_cast<int>(runs.size()) - 1; r >= 0 && !stopped; --r) {
            const std::vector<GlyphInline>& row = runs[static_cast<std::size_t>(r)].glyphs;
            for (int i = static_cast<int>(row.size()) - 1; i >= 0; --i) {
                if (!row[static_cast<std::size_t>(i)].collapsible) {
                    stopped = true;
                    break;
                }
                ++trailing;
            }
        }
    }
    float advance = 0;
    std::size_t glyphs = 0;
    std::size_t seen = 0;
    const std::size_t keep_limit = [&]() {
        std::size_t total = 0;
        for (const ShapedRun& run : runs) {
            total += run.glyphs.size();
        }
        return total - trailing;
    }();
    for (const ShapedRun& run : runs) {
        for (const GlyphInline& glyph : run.glyphs) {
            if (seen >= keep_limit) {
                break;
            }
            advance += glyph.advance;
            ++glyphs;
            ++seen;
        }
    }
    // CSS Text 3 / Chrome: letter-spacing is added after every character, including the last
    // one. Measuring without it made `letter-spacing: 10px` keep the unspaced intrinsic width.
    // Hung trailing spaces are not characters of the line, so they take none.
    advance += text_style.letter_spacing * static_cast<float>(glyphs);
    // Word-spacing is applied on IFC pieces (and paint via those advances). Here only the
    // intrinsic width needs the extra space count — do not mutate glyph advances or the parent
    // IFC would add word-spacing twice.
    const float word_spacing = text_style.word_spacing;
    if (word_spacing != 0.f) {
        seen = 0;
        for (const ShapedRun& run : runs) {
            std::size_t byte = 0;
            for (const GlyphInline& glyph : run.glyphs) {
                if (seen >= keep_limit) {
                    break;
                }
                ++seen;
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
    const LayoutContext::FontEm em = cx.font_em(text_style);
    const float font_box = std::max(0.f, em.ascent + em.descent);
    // Inside a block the line walker builds the strut. A text run that *is* the flex or grid
    // item is blockified (CSS Flexbox §4 / Grid §4): its border box is that line box, so the
    // used line-height is the block size. The font box alone (20px at 15px with this face)
    // left a `line-height: 1.5` button 2.5px short, and the sidebar then never overflowed.
    float size = font_box;
    float baseline = em.ascent;
    float half_leading = 0.f;
    if (box.is_flex_item || box.is_grid_item) {
        const float strut = used_line_height(text_style, em);
        half_leading = std::floor((strut - font_box) * 0.5f);
        size = strut;
        baseline = em.ascent + half_leading;
    }
    out.geometry.content_inline = advance;
    out.geometry.content_block = size;
    out.geometry.border_box_inline = advance;
    out.geometry.border_box_block = size;
    out.geometry.baselines.first = baseline;
    out.geometry.baselines.last = baseline;
    auto lines = std::make_unique<InlineContent>();
    LineBox line;
    line.inline_size = advance;
    line.baseline = baseline;
    line.block_size = size;
    line.runs = runs;
    // `shape` reports advances, not positions: a standalone text box is one line, so its runs have
    // to be placed one after the next here. Without an origin every run falls back to the line
    // start (paint_map's `shaped.has_origin ? shaped.origin : line.inline_start`) and a sentence
    // that shaped into several runs - mixed scripts, or a space before the font fallback - drew
    // every run on top of the first one.
    float cursor = 0;
    const float spacing = style_of(box).letter_spacing;
    for (ShapedRun& run : line.runs) {
        run.has_origin = true;
        run.origin = cursor;
        cursor += run.advance_inline + spacing * static_cast<float>(run.glyphs.size());
        // Range client rects start at the em box, which sits the half-leading below the
        // line-box top. Leaving this at 0 reported first_line_offset one pixel high.
        for (GlyphInline& glyph : run.glyphs) {
            glyph.block_offset = half_leading;
        }
    }
    // shape() already fills each run's text in visual order.
    lines->lines.push_back(std::move(line));
    out.lines = std::move(lines);
}

bool is_ruby_container(const Box& box)
{
    if (box.node == nullptr) {
        return false;
    }
    const std::string* display = box.node->style("display");
    return display != nullptr && *display == "ruby";
}

bool is_ruby_text(const Box& box)
{
    if (box.node == nullptr || box.role == BoxRole::Text) {
        return false;
    }
    const std::string* display = box.node->style("display");
    return display != nullptr &&
           (*display == "ruby-text" || *display == "ruby-text-container");
}

void layout_ruby(Box& box, const LayoutKey& key, LayoutContext& cx, const UsedEdges& edges,
                 LayoutResult& out)
{
    const LayoutStyle& style = style_of(box);
    const Edges border = border_edges(style);
    Walk walk;
    // Unknown inline available: the parts below are measured shrink-to-fit and the container's
    // inline size is max(base, annotation). `AxisSize` is {definite, px}; the literal used to be
    // written in the other order, which made this a definite 0px constraint.
    AxisSize available{false, 0.f};
    const AxisSize block_available{};
    const AxisSize block_percent = key.space.percentage.block_size;
    struct Part {
        Box* box = nullptr;
        const LayoutResult* result = nullptr;
        float inline_size = 0;
        float block_size = 0;
        float baseline = 0;
        bool annotation = false;
    };
    std::vector<Part> parts;
    parts.reserve(box.children.size());
    for (Box* child : box.children) {
        if (child == nullptr || child->style_ptr == nullptr) {
            continue;
        }
        const LayoutStyle& child_style = style_of(*child);
        if (out_of_flow(child_style) || child_style.floating) {
            continue;
        }
        const LayoutResult* result =
            ask_child(cx, *child,
                      child_space(key, edges, 0.f, false, available, key.space.exclusion,
                                  block_available, block_percent, {}, style.writing_mode,
                                  style.direction),
                      walk, nullptr);
        if (result == nullptr) {
            continue;
        }
        Part part;
        part.box = child;
        part.result = result;
        part.inline_size = result->geometry.border_box_inline + result->geometry.margin.inline_start +
                           result->geometry.margin.inline_end;
        part.block_size = result->geometry.border_box_block + result->geometry.margin.block_start +
                          result->geometry.margin.block_end;
        part.baseline = !std::isnan(result->geometry.baselines.first)
                            ? result->geometry.baselines.first + result->geometry.margin.block_start
                            : part.block_size;
        part.annotation = is_ruby_text(*child);
        parts.push_back(part);
    }

    float base_inline = 0.f;
    float base_block = 0.f;
    float base_baseline = 0.f;
    float ann_inline = 0.f;
    float ann_block = 0.f;
    for (const Part& part : parts) {
        if (part.annotation) {
            ann_inline += part.inline_size;
            ann_block = std::max(ann_block, part.block_size);
        } else {
            base_inline += part.inline_size;
            base_block = std::max(base_block, part.block_size);
            base_baseline = std::max(base_baseline, part.baseline);
        }
    }
    // Empty ruby still contributes a font strut so the line does not collapse.
    if (base_block <= 0.f && ann_block <= 0.f) {
        const LayoutContext::FontEm em = cx.font_em(style);
        base_block = em.ascent + em.descent;
        base_baseline = em.ascent;
    }
    // Chrome: the line box stays on the base line-height strut; annotations overhang above
    // into the margin rather than growing the paragraph. Advance is max(base, annotation).
    const LayoutContext::FontEm em = cx.font_em(style);
    const float strut = used_line_height(style, em);
    const float content_inline = std::max(base_inline, ann_inline);
    // Use the strut as the ruby border-box block size so an atomic ruby does not inflate the
    // line past `line-height` (text pieces contribute through line-height, not the font box).
    const float content_block = std::max(strut, 0.f);
    const float half = std::floor((strut - (em.ascent + em.descent)) * 0.5f);
    const float baseline = em.ascent + half;
    const float base_x0 = edges.padding_inline_start + border.inline_start +
                          std::max(0.f, (content_inline - base_inline) * 0.5f);
    const float ann_x0 = edges.padding_inline_start + border.inline_start +
                         std::max(0.f, (content_inline - ann_inline) * 0.5f);
    // Place the base's baseline on the ruby's baseline; annotations sit above with a small
    // overlap into the strut. The overlap is *not* a spec constant: it is the value case
    // `ruby-layout` pins against Chrome (a 22px annotation over a 40px line comes out ~9px into
    // the strut), so it needs a real ruby-overhang rule before it can serve other sizes.
    constexpr float kRubyAnnotationOverlapPx = 13.f;
    const float base_y = edges.padding_block_start + border.block_start +
                         std::max(0.f, baseline - base_baseline);
    const float overlap =
        std::min(ann_block, std::max(0.f, ann_block - kRubyAnnotationOverlapPx));
    const float ann_y = edges.padding_block_start + border.block_start - ann_block + overlap;
    float base_x = base_x0;
    float ann_x = ann_x0;
    for (Part& part : parts) {
        const float x = part.annotation ? ann_x : base_x;
        const float y = part.annotation ? ann_y : base_y;
        part.box->placement.cb_offset.inline_offset = x + part.result->geometry.margin.inline_start;
        part.box->placement.cb_offset.block_offset = y + part.result->geometry.margin.block_start;
        part.box->placement.sticky_offset = {};
        remember_place(walk, part.box);
        note_child_ink(walk, style, *part.box, *part.result, border);
        if (part.annotation) {
            ann_x += part.inline_size;
        } else {
            base_x += part.inline_size;
        }
    }

    out.geometry.content_inline = content_inline;
    out.geometry.content_block = content_block;
    out.geometry.border_box_inline = content_inline + chrome_inline(style, edges);
    out.geometry.border_box_block = content_block + chrome_block(style, edges);
    out.geometry.baselines.first = baseline;
    out.geometry.baselines.last = baseline;
    out.overflow.ink = walk.ink;
    out.child_places = std::move(walk.places);
    out.converged = true;
}

void layout_replaced(Box& box, const LayoutKey& key, LayoutContext& cx, LayoutResult& out)
{
    ReplacedRequest request;
    request.available = key.space.available;
    request.percentage = key.space.percentage;
    request.request = key.space.request;
    request.writing_mode = key.space.writing_mode;
    request.direction = key.space.direction;
    request.icb = key.space.icb;
    const ReplacedMeasurement measured = cx.measure_replaced(box, request);
    const LayoutStyle& style = style_of(box);
    const ConstraintSpace self_space =
        space_in_box_axes(key.space, style.writing_mode, style.direction);
    const UsedEdges edges = resolve_edges(style, self_space.percentage.inline_size, box.node);
    write_edges(out.geometry, style, edges);
    bool width_auto = false;
    bool height_auto = false;
    const AxisSize width_basis = physical_width_basis(style.writing_mode, self_space.percentage);
    const AxisSize height_basis = physical_height_basis(style.writing_mode, self_space.percentage);
    float width_used = used_main(style.width_auto, style.width_percent, style.width_px, width_basis,
                                 &width_auto);
    float height_used = used_main(style.height_auto, style.height_percent, style.height_px,
                                  height_basis, &height_auto);
    apply_size_overlays(box.node, width_used, width_auto, height_used, height_auto);
    float width = width_auto ? -1.f : width_used;
    float height = height_auto ? -1.f : height_used;
    // HTML width/height attributes are presentational hints, used only while CSS leaves the axis
    // auto. `width="120"` on an image with an intrinsic ratio is what produces the matching height.
    auto attr_px = [](const Box& replaced, const char* key) {
        if (replaced.node == nullptr) {
            return -1.f;
        }
        const auto it = replaced.node->attrs.find(key);
        if (it == replaced.node->attrs.end() || it->second.empty()) {
            return -1.f;
        }
        char* end = nullptr;
        const float v = std::strtof(it->second.c_str(), &end);
        if (end == it->second.c_str() || v < 0.f) {
            return -1.f;
        }
        return v;
    };
    if (width < 0.f) {
        const float attr = attr_px(box, "width");
        if (attr >= 0.f) {
            width = attr;
        }
    }
    if (height < 0.f) {
        const float attr = attr_px(box, "height");
        if (attr >= 0.f) {
            height = attr;
        }
    }
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
    } else if (measured.failed) {
        // A broken image with no CSS size and an empty alt does not keep the default object size.
        // Chrome gives that img no box; 300x150 here became the giant placeholder in the flow page.
        if (width < 0.f) {
            width = 0.f;
        }
        if (height < 0.f) {
            height = 0.f;
        }
    } else {
        if (width < 0.f) {
            width = measured.has_intrinsic_width ? measured.intrinsic_width : 300.f;
        }
        if (height < 0.f) {
            height = measured.has_intrinsic_height ? measured.intrinsic_height : 150.f;
        }
    }
    LogicalSize logical = physical_size_to_logical(style.writing_mode, width, height);
    // A flex or grid item is handed the size its container solved as a definite available size
    // (LAYOUT section 3), and a replaced element has to take it like any other item instead of
    // falling back to its intrinsic size. Ignoring it made an `<img>` in a row flex report its
    // intrinsic width while the container had solved a narrower target, which is exactly the
    // "Final came back different from the target" non-convergence L6 has been reporting.
    if (box.is_flex_item || box.is_grid_item) {
        if (self_space.available.inline_size.definite) {
            logical.inline_size =
                std::max(0.f, self_space.available.inline_size.px - edges.margin_inline_start -
                                  edges.margin_inline_end - chrome_inline(style, edges));
        }
        if (self_space.available.block_size.definite) {
            logical.block_size =
                std::max(0.f, self_space.available.block_size.px - edges.margin_block_start -
                                  edges.margin_block_end - chrome_block(style, edges));
        }
    }
    out.geometry.content_inline = std::max(0.f, logical.inline_size);
    out.geometry.content_block = std::max(0.f, logical.block_size);
    out.geometry.border_box_inline = out.geometry.content_inline + chrome_inline(style, edges);
    out.geometry.border_box_block = out.geometry.content_block + chrome_block(style, edges);
    // A replaced box has no line box. Chrome aligns its border-box bottom to the parent's
    // baseline (a checkbox then sits 4px into a 22px line, and the line stays 22px). Leaving
    // the baseline unset made the line treat the whole margin box as ascent.
    out.geometry.baselines.first = out.geometry.border_box_block;
    out.geometry.baselines.last = out.geometry.border_box_block;
}

}  // namespace flow_detail
}  // namespace layout
}  // namespace zui
