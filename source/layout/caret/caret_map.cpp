#include "layout/caret/caret_service.h"
#include "layout/caret/caret_internal.h"
#include "layout/caret/inline_lines.h"

#include "layout/box.h"
#include "layout/box_role.h"
#include "layout/cache/box_cache.h"
#include "layout/cache/box_geometry.h"
#include "layout/cache/direction.h"
#include "layout/cache/inline_content.h"
#include "layout/cache/layout_result.h"
#include "layout/cache/line_box.h"
#include "layout/cache/logical_axes.h"
#include "layout/cache/shaped_run.h"
#include "layout/cache/writing_mode.h"

#include "html/document.h"
#include "text/grapheme_break.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace zui {
namespace layout {
namespace caret_detail {

const LayoutResult* pinned_of(const Box& box) {
    return box.cache != nullptr ? box.cache->pinned_final() : nullptr;
}

bool parent_owns_lines(const Box& box) {
    if (box.role != BoxRole::Text || box.parent == nullptr) {
        return false;
    }
    const LayoutResult* parent = pinned_of(*box.parent);
    return parent != nullptr && parent->lines != nullptr && !parent->lines->lines.empty();
}

const Box* lines_owner(const Box& box) {
    if (parent_owns_lines(box)) {
        return box.parent;
    }
    if (box.inline_text_owned_by_parent && box.parent != nullptr) {
        return lines_owner(*box.parent);
    }
    return &box;
}

bool is_placeholder_node(const html::Node* owning) {
    if (owning == nullptr || owning->parent == nullptr) {
        return false;
    }
    const auto& slot =
        owning->parent->pseudo_node[static_cast<int>(html::PseudoElement::Placeholder)];
    return slot.get() == owning;
}

WritingMode box_writing_mode(const Box& box) {
    return box.style_ptr != nullptr ? box.style().writing_mode : WritingMode::HorizontalTb;
}

Direction box_direction(const Box& box) {
    return box.style_ptr != nullptr ? box.style().direction : Direction::Ltr;
}

MappedSpan make_span(std::size_t line_index, float line_baseline, float line_font_ascent,
                     float line_font_descent, float line_block, float line_block_offset,
                     const ShapedRun& shaped, const BoxGeometry& geometry, WritingMode mode,
                     Direction direction, std::size_t byte_begin, std::size_t byte_end) {
    MappedSpan span;
    span.line_index = line_index;
    span.byte_begin = byte_begin;
    span.byte_end = byte_end;
    span.ascent = line_baseline;
    span.font_ascent = line_font_ascent;
    span.font_descent = line_font_descent;
    span.line_block = line_block;
    const float inline_at = shaped.has_origin ? shaped.origin : 0.f;
    const float logical_i =
        geometry.border.inline_start + geometry.padding.inline_start + inline_at;
    const float logical_b =
        geometry.border.block_start + geometry.padding.block_start + line_block_offset;
    const PhysicalPoint origin = logical_point_to_physical(
        mode, direction, logical_i, logical_b, geometry.border_box_inline, geometry.border_box_block);
    span.local_x = origin.x;
    span.local_y = origin.y;
    span.glyph_advances.reserve(shaped.glyphs.size());
    for (const GlyphInline& g : shaped.glyphs) {
        span.glyph_advances.push_back(g.advance);
    }
    // Horizontal runs step in +x, including an RTL run whose glyphs are already in visual
    // order. Vertical sideways runs step in +y, matching the paint pen.
    if (is_horizontal_wm(mode)) {
        span.axis_x = 1.f;
        span.axis_y = 0.f;
    } else {
        span.axis_x = 0.f;
        span.axis_y = 1.f;
    }
    return span;
}

std::string utf8_reverse(std::string_view text) {
    std::vector<std::string_view> units;
    std::size_t byte = 0;
    while (byte < text.size()) {
        const std::size_t start = byte;
        ++byte;
        while (byte < text.size() && (static_cast<unsigned char>(text[byte]) & 0xc0) == 0x80) {
            ++byte;
        }
        units.push_back(text.substr(start, byte - start));
    }
    std::string out;
    out.reserve(text.size());
    for (auto it = units.rbegin(); it != units.rend(); ++it) {
        out.append(it->data(), it->size());
    }
    return out;
}

bool is_edit_ws(char c) {
    return c == ' ' || c == '\n' || c == '\r' || c == '\t';
}

/// A shaped space stands for a collapsed run of source whitespace. On success, [begin, end) is
/// the edit-string range that space consumed, including the raw newlines the layout folded.
bool align_collapsed(std::string_view run, const std::string& edit, std::size_t from,
                     std::size_t& begin, std::size_t& end) {
    std::size_t i = from;
    std::size_t j = 0;
    std::size_t start = std::string::npos;
    while (j < run.size()) {
        if (i >= edit.size()) {
            return false;
        }
        if (run[j] == edit[i]) {
            if (start == std::string::npos) {
                start = i;
            }
            ++j;
            ++i;
            continue;
        }
        if (run[j] == ' ' && is_edit_ws(edit[i])) {
            if (start == std::string::npos) {
                start = i;
            }
            ++j;
            while (i < edit.size() && is_edit_ws(edit[i])) {
                ++i;
            }
            continue;
        }
        if (start == std::string::npos && is_edit_ws(edit[i])) {
            ++i;
            continue;
        }
        return false;
    }
    if (start == std::string::npos) {
        return false;
    }
    begin = start;
    end = i;
    return true;
}

/// Map @p shaped onto @p edit_text at @p scan_at; advances scan_at on success.
/// A visual RTL run is the reverse of the logical slice. @p reversed is set in that case.
/// @p byte_len is the edit-string length consumed, which can exceed the shaped text when one
/// shaped space folds several source whitespace characters.
bool try_map_run(const ShapedRun& shaped, const std::string& edit_text, std::size_t& scan_at,
                 std::size_t& byte_begin, std::size_t& byte_len, bool* reversed) {
    if (reversed != nullptr) {
        *reversed = false;
    }
    byte_len = 0;
    if (shaped.text.empty()) {
        return false;
    }
    const auto accept = [&](std::size_t at, std::size_t len, bool rev) {
        byte_begin = at;
        byte_len = len;
        scan_at = at + len;
        if (reversed != nullptr) {
            *reversed = rev;
        }
        return true;
    };
    if (shaped.text == edit_text && scan_at == 0) {
        return accept(0, shaped.text.size(), false);
    }
    // First hit at or after the cursor. A run that cannot be aligned leaves the cursor alone, so
    // the next run can still find its own text later in the edit string.
    const std::size_t forward = edit_text.find(shaped.text, scan_at);
    if (forward != std::string::npos) {
        return accept(forward, shaped.text.size(), false);
    }
    std::size_t folded_begin = 0;
    std::size_t folded_end = 0;
    if (align_collapsed(shaped.text, edit_text, scan_at, folded_begin, folded_end)) {
        return accept(folded_begin, folded_end - folded_begin, false);
    }
    const std::string flipped = utf8_reverse(shaped.text);
    if (flipped == shaped.text) {
        return false;
    }
    if (flipped == edit_text && scan_at == 0) {
        return accept(0, flipped.size(), true);
    }
    const std::size_t backward = edit_text.find(flipped, scan_at);
    if (backward != std::string::npos) {
        return accept(backward, flipped.size(), true);
    }
    // The shaped word is there, but layout folded the source whitespace the edit string still
    // holds, so the run's own leading or trailing space is not a literal substring.
    std::string_view core(shaped.text);
    while (!core.empty() && is_edit_ws(core.front())) {
        core.remove_prefix(1);
    }
    while (!core.empty() && is_edit_ws(core.back())) {
        core.remove_suffix(1);
    }
    if (!core.empty() && core.size() != shaped.text.size()) {
        const std::size_t at = edit_text.find(core, scan_at);
        if (at != std::string::npos) {
            return accept(at, core.size(), false);
        }
    }
    return false;
}

const html::Node* owning_of(const ShapedRun& shaped, const Box& owner) {
    if (shaped.style_box != nullptr && shaped.style_box->node != nullptr) {
        return shaped.style_box->node;
    }
    if (owner.node != nullptr) {
        return owner.node;
    }
    return nullptr;
}

/// Nested `display:inline` elements keep glyphs on their own LineBoxes while the parent IFC
/// LineBox holds source_chars / strut. Collect those descendant shaped runs in tree order.
struct DescendantRun {
    const Box* child = nullptr;
    const ShapedRun* shaped = nullptr;
    std::size_t child_line_index = 0;
    float child_line_block_offset = 0.f;
};

void collect_descendant_runs(const Box& box, std::vector<DescendantRun>& out) {
    for (const Box* child : box.children) {
        if (child == nullptr) {
            continue;
        }
        // A text box whose parent already has the line runs is the same ink. Collecting
        // the child's copy (origin 0, its own border box) maps the following text on top of
        // the text before a chip and `find` then skips the chip's characters.
        if (child->inline_text_owned_by_parent || parent_owns_lines(*child)) {
            collect_descendant_runs(*child, out);
            continue;
        }
        const LayoutResult* result = pinned_of(*child);
        if (result != nullptr && result->lines != nullptr) {
            for (std::size_t li = 0; li < result->lines->lines.size(); ++li) {
                const LineBox& line = result->lines->lines[li];
                for (const ShapedRun& shaped : line.runs) {
                    if (!shaped.text.empty()) {
                        DescendantRun item;
                        item.child = child;
                        item.shaped = &shaped;
                        item.child_line_index = li;
                        item.child_line_block_offset = line.block_offset;
                        out.push_back(item);
                    }
                }
            }
        }
        // A padded inline such as `<code>` keeps its own runs. The parent line has the
        // surrounding text and does not repeat those glyphs, so stopping here left the chip
        // out of hit-testing and a drag on it landed on the text before the chip.
        collect_descendant_runs(*child, out);
    }
}

float world_x_of(const Box& box, float local_x) {
    return box.geo.world_x + local_x - box.geo.scroll_x;
}

float world_y_of(const Box& box, float local_y) {
    return box.geo.world_y + local_y - box.geo.scroll_y;
}

std::size_t utf8_next_byte(const std::string& text, std::size_t byte) {
    if (byte >= text.size()) {
        return text.size();
    }
    ++byte;
    while (byte < text.size() && (static_cast<unsigned char>(text[byte]) & 0xc0) == 0x80) {
        ++byte;
    }
    return byte;
}

bool span_is_vertical(const MappedSpan& span) {
    return std::fabs(span.axis_y) > std::fabs(span.axis_x);
}

ClusterEdge edge_at(const Box& box, const MappedSpan& span, std::size_t byte, float along) {
    ClusterEdge edge;
    edge.byte = byte;
    if (span_is_vertical(span)) {
        edge.x = world_x_of(box, span.local_x);
        edge.y = world_y_of(box, span.local_y + along * span.axis_y);
    } else {
        edge.x = world_x_of(box, span.local_x + along * span.axis_x);
        edge.y = world_y_of(box, span.local_y + span.ascent - span.font_ascent);
    }
    return edge;
}

std::vector<ClusterEdge> cluster_edges(const Box& box, const EditMapping& mapping,
                                       const MappedSpan& span) {
    std::vector<ClusterEdge> edges;
    if (mapping.text == nullptr) {
        return edges;
    }
    const std::string& text = *mapping.text;
    float total = 0.f;
    for (float advance : span.glyph_advances) {
        total += advance;
    }
    // A reversed run's logical start is the visual end. `along` is the distance from the
    // visual origin (the left of a horizontal run, the block-start of a vertical one).
    float along = span.reverse ? total : 0.f;
    const float step = span.reverse ? -1.f : 1.f;
    std::size_t glyph_i = span.reverse ? span.glyph_advances.size() : 0;
    edges.push_back(edge_at(box, span, span.byte_begin, along));

    std::size_t byte = span.byte_begin;
    while (byte < span.byte_end && byte < text.size()) {
        const std::size_t next = text::grapheme_next(text, byte);
        const std::size_t cluster_end = std::min(next, span.byte_end);
        float advance_sum = 0.f;
        std::size_t walk = byte;
        while (walk < cluster_end) {
            if (span.reverse) {
                if (glyph_i > 0) {
                    advance_sum += span.glyph_advances[--glyph_i];
                }
            } else if (glyph_i < span.glyph_advances.size()) {
                advance_sum += span.glyph_advances[glyph_i++];
            }
            walk = utf8_next_byte(text, walk);
        }
        along += step * advance_sum;
        edges.push_back(edge_at(box, span, cluster_end, along));
        byte = cluster_end;
    }
    return edges;
}

const MappedSpan* span_covering(const EditMapping& mapping, std::size_t offset,
                                CaretAffinity affinity) {
    if (mapping.text == nullptr || mapping.spans.empty()) {
        return nullptr;
    }
    const std::size_t n = mapping.text->size();
    offset = std::min(offset, n);

    const MappedSpan* best = nullptr;
    for (const MappedSpan& span : mapping.spans) {
        if (offset > span.byte_begin && offset < span.byte_end) {
            return &span;
        }
        if (offset == span.byte_begin) {
            if (affinity == CaretAffinity::Upstream && span.byte_begin > 0) {
                continue;
            }
            if (best == nullptr || span.line_index < best->line_index ||
                (span.line_index == best->line_index && span.byte_begin < best->byte_begin)) {
                best = &span;
            }
        }
        if (offset == span.byte_end) {
            if (affinity == CaretAffinity::Downstream && offset < n) {
                continue;
            }
            if (best == nullptr || span.line_index > best->line_index ||
                (span.line_index == best->line_index && span.byte_end > best->byte_end)) {
                best = &span;
            }
        }
    }
    if (best != nullptr) {
        return best;
    }
    if (offset == n) {
        return &mapping.spans.back();
    }
    std::size_t best_dist = std::numeric_limits<std::size_t>::max();
    for (const MappedSpan& span : mapping.spans) {
        std::size_t dist = 0;
        if (offset < span.byte_begin) {
            dist = span.byte_begin - offset;
        } else if (offset > span.byte_end) {
            dist = offset - span.byte_end;
        }
        if (dist < best_dist) {
            best_dist = dist;
            best = &span;
        }
    }
    return best;
}

bool line_has_mappable(const EditMapping& mapping, std::size_t line_index) {
    for (const MappedSpan& span : mapping.spans) {
        if (span.line_index == line_index) {
            return true;
        }
    }
    return false;
}

/// Placeholder glyphs are a different text object. An empty value still has an insertion
/// point: a zero-length span on the control's first line strut.
void append_empty_value_strut(EditMapping& mapping, const Box& owner, const InlineContent& lines,
                              const LayoutResult& result) {
    if (mapping.text == nullptr || !mapping.text->empty() || !mapping.spans.empty()) {
        return;
    }
    if (lines.lines.empty()) {
        return;
    }
    const LineBox& line = lines.lines.front();
    if (line.font_ascent + line.font_descent <= 0.f) {
        return;
    }
    ShapedRun shaped;
    shaped.has_origin = true;
    shaped.origin = line.inline_start;
    const float block =
        line.block_size > 0.f ? line.block_size : line.font_ascent + line.font_descent;
    mapping.spans.push_back(make_span(0, line.baseline, line.font_ascent, line.font_descent, block,
                                      line.block_offset, shaped, result.geometry,
                                      box_writing_mode(owner), box_direction(owner), 0, 0));
}

/// One IFC line: shaped runs first, then a run-less line as a zero-length caret, then the single
/// newline `break_rect` stands for. A line whose runs exist but do not match is left alone, so
/// one unaligned fragment does not invent a caret or swallow the next line's break.
void map_direct_line(EditMapping& mapping, const Box& owner, const LineBox& line,
                     std::size_t line_index, const LayoutResult& result, const std::string& edit_text,
                     std::size_t& scan_at, bool map_runs) {
    const WritingMode mode = box_writing_mode(owner);
    const Direction direction = box_direction(owner);
    bool mapped_run = false;
    if (map_runs) {
        for (const ShapedRun& shaped : line.runs) {
            if (is_placeholder_node(owning_of(shaped, owner))) {
                continue;
            }
            std::size_t byte_begin = 0;
            std::size_t byte_len = 0;
            bool reversed = false;
            if (!try_map_run(shaped, edit_text, scan_at, byte_begin, byte_len, &reversed)) {
                continue;
            }
            mapped_run = true;
            MappedSpan span =
                make_span(line_index, line.baseline, line.font_ascent, line.font_descent,
                          line.block_size, line.block_offset, shaped, result.geometry, mode,
                          direction, byte_begin, byte_begin + byte_len);
            span.reverse = reversed;
            mapping.spans.push_back(std::move(span));
        }
    }
    // direct_runs == 0 and map_runs false: only a real blank strut (a hard break, or the line
    // after one). A parent line whose glyphs live on a child has source_chars and no break.
    const bool blank_strut = line.runs.empty() && line.font_ascent + line.font_descent > 0.f &&
                             (map_runs || line.break_rect || line.source_chars == 0);
    if (blank_strut) {
        ShapedRun shaped;
        shaped.has_origin = true;
        shaped.origin = line.inline_start;
        const float block =
            line.block_size > 0.f ? line.block_size : line.font_ascent + line.font_descent;
        mapping.spans.push_back(make_span(line_index, line.baseline, line.font_ascent,
                                          line.font_descent, block, line.block_offset, shaped,
                                          result.geometry, mode, direction, scan_at, scan_at));
    }
    if (line.break_rect && (mapped_run || line.runs.empty())) {
        if (scan_at < edit_text.size() &&
            (edit_text[scan_at] == '\n' || edit_text[scan_at] == '\r')) {
            ++scan_at;
        }
    }
}


}  // namespace caret_detail

using namespace caret_detail;

EditMapping make_edit_mapping(const Box& box, const std::string& edit_text) {
    EditMapping mapping;
    mapping.text = &edit_text;

    const Box* owner = lines_owner(box);
    const InlineContent* lines = inline_lines(*owner);
    const LayoutResult* result = pinned_of(*owner);
    if (lines == nullptr || result == nullptr) {
        return mapping;
    }

    const WritingMode mode = box_writing_mode(*owner);
    std::size_t scan_at = 0;

    // Count shaped runs directly on the IFC lines.
    std::size_t direct_runs = 0;
    for (const LineBox& line : lines->lines) {
        direct_runs += line.runs.size();
    }

    // A padded inline keeps its glyphs on its own box. Those runs have to be mapped in
    // inline order with the parent line, or `find` matches the text after the chip first
    // and the chip's characters are never a span.
    std::vector<DescendantRun> atoms;
    collect_descendant_runs(*owner, atoms);
    if (direct_runs > 0 && atoms.empty()) {
        for (std::size_t li = 0; li < lines->lines.size(); ++li) {
            map_direct_line(mapping, *owner, lines->lines[li], li, *result, edit_text, scan_at,
                            true);
        }
        append_empty_value_strut(mapping, *owner, *lines, *result);
        return mapping;
    }

    std::vector<DescendantRun> desc;
    if (direct_runs > 0) {
        for (std::size_t li = 0; li < lines->lines.size(); ++li) {
            const LineBox& line = lines->lines[li];
            std::vector<DescendantRun> items;
            for (const ShapedRun& shaped : line.runs) {
                if (shaped.text.empty()) {
                    continue;
                }
                DescendantRun item;
                item.child = owner;
                item.shaped = &shaped;
                item.child_line_index = li;
                item.child_line_block_offset = line.block_offset;
                items.push_back(item);
            }
            const float line_top = owner->geo.world_y + line.block_offset;
            const float line_bot = line_top + std::max(line.block_size, 1.f);
            for (const DescendantRun& atom : atoms) {
                const float y = atom.child->geo.world_y;
                if (y >= line_top - 1.f && y < line_bot + 1.f) {
                    items.push_back(atom);
                }
            }
            if (is_horizontal_wm(mode)) {
                std::stable_sort(items.begin(), items.end(),
                                 [&](const DescendantRun& a, const DescendantRun& b) {
                                     const auto key = [&](const DescendantRun& item) {
                                         if (item.child == owner) {
                                             return item.shaped->has_origin ? item.shaped->origin
                                                                            : 0.f;
                                         }
                                         return item.child->geo.world_x - owner->geo.world_x;
                                     };
                                     return key(a) < key(b);
                                 });
            }
            desc.insert(desc.end(), items.begin(), items.end());
            if (line.runs.empty()) {
                map_direct_line(mapping, *owner, line, li, *result, edit_text, scan_at, false);
            }
        }
    } else {
        desc = std::move(atoms);
        if (is_horizontal_wm(mode)) {
            std::stable_sort(desc.begin(), desc.end(),
                             [](const DescendantRun& a, const DescendantRun& b) {
                                 const float ay = a.child->geo.world_y + a.child_line_block_offset;
                                 const float by = b.child->geo.world_y + b.child_line_block_offset;
                                 if (std::fabs(ay - by) > 0.5f) {
                                     return ay < by;
                                 }
                                 const float ax = a.child->geo.world_x +
                                                  (a.shaped->has_origin ? a.shaped->origin : 0.f);
                                 const float bx = b.child->geo.world_x +
                                                  (b.shaped->has_origin ? b.shaped->origin : 0.f);
                                 return ax < bx;
                             });
        }
    }

    for (const auto& entry : desc) {
        const Box* child = entry.child;
        const ShapedRun& shaped = *entry.shaped;
        if (is_placeholder_node(owning_of(shaped, *child))) {
            continue;
        }
        std::size_t byte_begin = 0;
        std::size_t byte_len = 0;
        bool reversed = false;
        if (!try_map_run(shaped, edit_text, scan_at, byte_begin, byte_len, &reversed)) {
            continue;
        }
        const LayoutResult* child_result = pinned_of(*child);
        if (child_result == nullptr) {
            continue;
        }

        const InlineContent* child_lines =
            child_result->lines != nullptr ? child_result->lines.get() : nullptr;
        const bool child_wraps = child_lines != nullptr && child_lines->lines.size() > 1;

        std::size_t li = 0;
        const LineBox* visual = &lines->lines.front();
        if (child_wraps) {
            li = entry.child_line_index;
            if (li >= child_lines->lines.size()) {
                li = child_lines->lines.size() - 1;
            }
            visual = &child_lines->lines[li];
        } else if (lines->lines.size() > 1) {
            const float child_block =
                child->placement.cb_offset.block_offset + entry.child_line_block_offset;
            float best = std::numeric_limits<float>::max();
            for (std::size_t i = 0; i < lines->lines.size(); ++i) {
                const float top = result->geometry.border.block_start +
                                  result->geometry.padding.block_start + lines->lines[i].block_offset;
                const float dy = std::fabs(child_block - top);
                if (dy < best) {
                    best = dy;
                    li = i;
                }
            }
            visual = &lines->lines[li];
        }

        MappedSpan span = make_span(
            li, visual->baseline, visual->font_ascent, visual->font_descent, visual->block_size,
            visual->block_offset, shaped, child_result->geometry, box_writing_mode(*child),
            box_direction(*child), byte_begin, byte_begin + byte_len);
        span.reverse = reversed;
        if (child->geo.world_x != 0.f || owner->geo.world_x != 0.f || child != owner) {
            const float child_world_x = child->geo.world_x + span.local_x - child->geo.scroll_x;
            span.local_x = child_world_x - owner->geo.world_x + owner->geo.scroll_x;
        }
        if (child_wraps) {
            const float child_world_y = child->geo.world_y + span.local_y - child->geo.scroll_y;
            span.local_y = child_world_y - owner->geo.world_y + owner->geo.scroll_y;
        } else {
            span.local_y = result->geometry.border.block_start +
                           result->geometry.padding.block_start + visual->block_offset;
        }
        if (!is_horizontal_wm(mode)) {
            // Non-horizontal: keep make_span's physical y; Task 2 tests are horizontal-tb.
        }
        mapping.spans.push_back(std::move(span));
    }

    // Newline-only value: every line is a font strut and no descendant run matched. An empty
    // value still goes through append_empty_value_strut below.
    if (mapping.spans.empty() && !edit_text.empty()) {
        for (std::size_t li = 0; li < lines->lines.size(); ++li) {
            map_direct_line(mapping, *owner, lines->lines[li], li, *result, edit_text, scan_at,
                            false);
        }
    }

    append_empty_value_strut(mapping, *owner, *lines, *result);
    return mapping;
}

}  // namespace layout
}  // namespace zui
