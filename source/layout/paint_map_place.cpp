#include "perf/perf_budget.h"
#include "layout/paint_map.h"
#include "layout/paint_map_internal.h"
#include "log/log.h"

#include "layout/clip_path.h"
#include "layout/paint_transform.h"

#include "layout/cache/logical_axes.h"

#include "layout/box.h"
#include "layout/cache/box_cache.h"
#include "layout/cache/overflow.h"
#include "layout/cache/sticky.h"
#include "layout/overflow_mode.h"
#include "layout/stacking.h"

#include "html/document.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace zui {
namespace layout {
namespace paint_map_detail {

using namespace paint_detail;

/// `#text` children of a mixed inline, in tree order. An atomic inline (a padded `<code>`
/// chip) owns its own runs, so its text is not a candidate for a run on this line.
void collect_inline_text_nodes(const Box* box, std::vector<const html::Node*>& out) {
    if (box == nullptr) {
        return;
    }
    if (box->role == BoxRole::Text) {
        if (box->node != nullptr && !box->node->text.empty()) {
            out.push_back(box->node);
        }
        return;
    }
    for (const Box* child : box->children) {
        if (child == nullptr || child->atomic_inline) {
            continue;
        }
        collect_inline_text_nodes(child, out);
    }
}

void fill_geometry(Box& box, const LayoutResult* result) {
    box.geo.runs.clear();
    if (result == nullptr) {
        box.geo.width = 0;
        box.geo.height = 0;
        box.geo.content_width = 0;
        box.geo.content_height = 0;
        return;
    }
    const BoxGeometry& geometry = result->geometry;
    const LayoutStyle* style = box.style_ptr != nullptr ? &box.style() : nullptr;
    const WritingMode mode = style != nullptr ? style->writing_mode : WritingMode::HorizontalTb;
    const Direction direction = style != nullptr ? style->direction : Direction::Ltr;
    const PhysicalSize border_box =
        logical_size_to_physical(mode, geometry.border_box_inline, geometry.border_box_block);
    const PhysicalSize content_box =
        logical_size_to_physical(mode, geometry.content_inline, geometry.content_block);
    box.geo.width = border_box.width;
    box.geo.height = border_box.height;
    box.geo.content_width = content_box.width;
    box.geo.content_height = content_box.height;
    // The scrollable extent (browser `scrollWidth` / `scrollHeight`). LayoutResult carries it in
    // logical axes (LAYOUT.md section 4); a box the layout has not filled yet falls back to the
    // padding box, which is what a browser reports for content that does not overflow.
    {
        const Rect& scrollable = result->overflow.scrollable;
        // Fallback when the layout has not filled the overflow rect yet (flex / grid containers
        // today): the padding box, which is what a browser reports for content that fits. It is
        // only a fallback - a filled rect is authoritative even when it is smaller than the
        // padding box, which is exactly what a reserved scrollbar gutter produces.
        const float padding_inline =
            geometry.border_box_inline - geometry.border.inline_start - geometry.border.inline_end;
        const float padding_block =
            geometry.border_box_block - geometry.border.block_start - geometry.border.block_end;
        const float inline_extent = scrollable.w > 0.f ? scrollable.w : padding_inline;
        const float block_extent = scrollable.h > 0.f ? scrollable.h : padding_block;
        const PhysicalSize scroll_box = logical_size_to_physical(mode, inline_extent, block_extent);
        box.geo.scroll_width = scroll_box.width;
        box.geo.scroll_height = scroll_box.height;
        // A pure inline box has no box of its own in the inline formatting context, so a browser
        // reports a zero scroll extent for it (an inline-block, which does have one, keeps the
        // computed numbers). An abspos, fixed, or floated box is blockified and keeps the
        // scrollable overflow of the text it laid out: a 1px clip still reports the line.
        if (style != nullptr && style->outer == DisplayOuter::Inline && !box.atomic_inline &&
            !style->position_absolute && !style->position_fixed && !style->floating) {
            box.geo.scroll_width = 0;
            box.geo.scroll_height = 0;
        }
        // Collapsed table columns: border box is 0, but scrollHeight stays the track size.
        if (style != nullptr && style->visibility_collapse && style->table_column) {
            box.geo.scroll_width = 0;
            box.geo.scroll_height = box.geo.content_height;
        }
    }
    box.geo.margin = logical_edges_to_physical(mode, direction, geometry.margin);
    box.geo.border = logical_edges_to_physical(mode, direction, geometry.border);
    box.geo.padding = logical_edges_to_physical(mode, direction, geometry.padding);
    box.geo.baseline = geometry.baselines.first;
    if (parent_paints_text(box) || result->lines == nullptr || box.inline_text_owned_by_parent) {
        return;
    }
    // Line boxes stay logical (LAYOUT sec 12). Convert each run origin to physical inside this
    // border box. Painter rotates Latin sideways under vertical/sideways writing-mode; CJK
    // upright (text-orientation:upright / mixed CJK) remains deferred.
    // A textarea's lines are fragments of one `node->text`. Remember how far the scan has
    // consumed so each line's `byte_begin` is that fragment, not 0.
    const html::Node* scan_node = nullptr;
    std::size_t scan_at = 0;
    // Used only when a run is not a slice of the element it is stamped with. The cursor
    // walks that element's `#text` children in order, once per element, not once per run.
    const Box* child_home = nullptr;
    std::vector<const html::Node*> child_text;
    std::size_t child_i = 0;
    std::size_t child_at = 0;
    for (const LineBox& line : result->lines->lines) {
        for (const ShapedRun& shaped : line.runs) {
            if (shaped.text.empty()) {
                continue;
            }
            // Line offsets are inside the content box. This point is measured from the
            // border-box origin, so the border has to be added or the glyphs sit on the border.
            const float inline_at = shaped.has_origin ? shaped.origin : line.inline_start;
            const float logical_i =
                geometry.border.inline_start + geometry.padding.inline_start + inline_at;
            const float logical_b =
                geometry.border.block_start + geometry.padding.block_start + line.block_offset;
            const PhysicalPoint origin =
                logical_point_to_physical(mode, direction, logical_i, logical_b,
                                          geometry.border_box_inline, geometry.border_box_block);
            TextRun run;
            run.x = origin.x;
            run.y = origin.y;
            run.ascent = line.baseline;
            run.font_ascent = line.font_ascent;
            run.font_descent = line.font_descent;
            run.line_block = line.block_size;
            run.text = shaped.text;
            run.style_box = shaped.style_box;
            // Path (b): selection maps run bytes onto node->text only when the shaped string is
            // exactly that node's text (no whitespace fold / bidi reorder). Placeholder runs are
            // never mappable. Empty glyph_advances tells paint to skip.
            const html::Node* owning = nullptr;
            if (shaped.style_box != nullptr && shaped.style_box->node != nullptr) {
                owning = shaped.style_box->node;
            } else if (box.node != nullptr) {
                owning = box.node;
            }
            bool placeholder = false;
            if (owning != nullptr && owning->parent != nullptr) {
                const auto& slot =
                    owning->parent->pseudo_node[static_cast<int>(html::PseudoElement::Placeholder)];
                if (slot.get() == owning) {
                    placeholder = true;
                }
            }
            // Every run carries its per-glyph advances: paint renders from them (letter-spacing,
            // pair kerning and `text-align: justify` are already baked in, and a renderer that
            // re-derives them from the font cannot see the justification).
            run.byte_begin = 0;
            bool fragment = false;
            if (!placeholder && owning != nullptr) {
                if (owning != scan_node) {
                    scan_node = owning;
                    scan_at = 0;
                }
                const std::string& full = owning->text;
                if (full == run.text) {
                    scan_at = full.size();
                } else {
                    while (scan_at < full.size() &&
                           (full[scan_at] == '\n' || full[scan_at] == '\r')) {
                        if (!run.text.empty() && run.text[0] == full[scan_at]) {
                            break;
                        }
                        ++scan_at;
                    }
                    if (scan_at + run.text.size() <= full.size() &&
                        full.compare(scan_at, run.text.size(), run.text) == 0) {
                        run.byte_begin = scan_at;
                        scan_at += run.text.size();
                        fragment = true;
                    }
                }
            }
            run.glyph_advances.reserve(shaped.glyphs.size());
            bool shaped_glyph = false;
            for (const GlyphInline& g : shaped.glyphs) {
                run.glyph_advances.push_back(g.advance);
                if (g.paint_glyph != 0) {
                    shaped_glyph = true;
                }
            }
            // Only a ligature needs ids. A run of cmap glyphs leaves this empty so paint keeps
            // loading by codepoint and the raster stays on the path every green page already uses.
            if (shaped_glyph) {
                run.glyph_ids.reserve(shaped.glyphs.size());
                for (const GlyphInline& g : shaped.glyphs) {
                    run.glyph_ids.push_back(g.paint_glyph);
                }
            }
            // Selection's byte→x mapping needs the shaped string to be that node's own text.
            // A whitespace fold or a bidi reorder fails the compare and stays unmappable.
            // A mixed inline stamps the run with the element, whose `text` is empty; the
            // characters live on the `#text` children. Bind the run to the child that holds
            // this slice so the highlight can cover the glyphs on either side of a chip.
            if (!placeholder && !fragment &&
                (owning == nullptr || shaped.text != owning->text)) {
                const Box* home = shaped.style_box != nullptr ? shaped.style_box : &box;
                if (home != child_home) {
                    child_home = home;
                    child_text.clear();
                    child_i = 0;
                    child_at = 0;
                    collect_inline_text_nodes(home, child_text);
                }
                for (std::size_t i = child_i; i < child_text.size(); ++i) {
                    const html::Node* node = child_text[i];
                    if (node == nullptr || node == owning) {
                        continue;
                    }
                    std::size_t at = i == child_i ? child_at : 0;
                    while (at < node->text.size() &&
                           (node->text[at] == '\n' || node->text[at] == '\r')) {
                        if (!run.text.empty() && run.text[0] == node->text[at]) {
                            break;
                        }
                        ++at;
                    }
                    if (at + run.text.size() <= node->text.size() &&
                        node->text.compare(at, run.text.size(), run.text) == 0) {
                        run.byte_begin = at;
                        run.text_node = node;
                        child_i = i;
                        child_at = at + run.text.size();
                        break;
                    }
                }
            }
            run.mappable = !placeholder &&
                           (run.text_node != nullptr ||
                            (owning != nullptr && (fragment || shaped.text == owning->text)));
            box.geo.runs.push_back(std::move(run));
        }
    }
}

/// Resolve the fragments a parent recorded on this box (LAYOUT.md section 12) into world
/// coordinates. Fills `geo.fragments` with every fragment in order and `geo.union_*` with their
/// border-box union - the rect a browser reports for `getBoundingClientRect` on an element it
/// split across fragmentainers. `geo.world_*` / `geo.width / geo.height` stay the *first*
/// fragment's, which is what paint and hit testing walk from.
void map_fragments(Box& box, const LayoutResult* result, const Aff2& parent_to_world,
                   float parent_border_inline_start, float parent_border_block_start,
                   float parent_border_inline, float parent_border_block, WritingMode parent_mode,
                   Direction parent_direction, float scroll_inline, float scroll_block) {
    box.geo.fragments.clear();
    box.geo.union_x = 1.0e9f;
    box.geo.union_y = 1.0e9f;
    box.geo.union_w = 0;
    box.geo.union_h = 0;
    if (result == nullptr || box.placement.fragments.size() < 2) {
        return;
    }
    const WritingMode self_mode =
        box.style_ptr != nullptr ? box.style().writing_mode : WritingMode::HorizontalTb;
    const Direction self_dir = box.style_ptr != nullptr ? box.style().direction : Direction::Ltr;
    const float child_bi = result->geometry.border_box_inline;
    const float box_block = result->geometry.border_box_block;
    for (const FragmentPlacement& fragment : box.placement.fragments) {
        // Visual height is clipped to the box's own content (Chrome's client rect for an empty
        // trailing fragment opened only for a taller float is 0). Coverage keeps the full band so
        // descendants still slice against the column.
        float visual_block = fragment.block_size;
        if (fragment.block_offset >= box_block - 0.01f) {
            visual_block = 0.f;
        } else if (fragment.block_offset + fragment.block_size > box_block + 0.01f) {
            visual_block = std::max(0.f, box_block - fragment.block_offset);
        }
        const float logical_i = parent_border_inline_start + fragment.cb_offset.inline_offset +
                                box.placement.sticky_offset.inline_offset - scroll_inline;
        const float logical_b = parent_border_block_start + fragment.cb_offset.block_offset +
                                box.placement.sticky_offset.block_offset - scroll_block;
        const PhysicalPoint tl = logical_start_to_physical_tl(
            parent_mode, parent_direction, logical_i, logical_b, parent_border_inline,
            parent_border_block, child_bi, visual_block);
        Aff2 to_world = parent_to_world.then(translated(tl.x, tl.y));
        float wx = 0;
        float wy = 0;
        to_world.apply(0, 0, wx, wy);
        const PhysicalSize size = logical_size_to_physical(self_mode, child_bi, visual_block);
        // Where the box's own origin has to be while painting this fragment: the fragment's origin
        // minus how far into the box's own block axis the slice starts.
        const PhysicalPoint slice = logical_point_to_physical(
            self_mode, self_dir, 0.f, fragment.block_offset, child_bi, box_block);
        FragmentBox out;
        out.world_x = wx;
        out.world_y = wy;
        out.width = size.width;
        out.height = size.height;
        out.coverage_height =
            logical_size_to_physical(self_mode, child_bi, fragment.block_size).height;
        out.paint_dx = wx - box.geo.world_x - slice.x;
        out.paint_dy = wy - box.geo.world_y - slice.y;
        out.block_offset = fragment.block_offset;
        box.geo.fragments.push_back(out);
    }
    // The union needs both ends before it can be measured: a later fragment can sit above an
    // earlier one (the second column of a multicol), and the width and height have to be taken
    // against the final top-left corner. Zero-area fragments stay in client_rects but do not grow
    // getBoundingClientRect (Chrome drops them from the union).
    for (const FragmentBox& fragment : box.geo.fragments) {
        if (fragment.width <= 0.01f || fragment.height <= 0.01f) {
            continue;
        }
        box.geo.union_x = std::min(box.geo.union_x, fragment.world_x);
        box.geo.union_y = std::min(box.geo.union_y, fragment.world_y);
    }
    for (const FragmentBox& fragment : box.geo.fragments) {
        if (fragment.width <= 0.01f || fragment.height <= 0.01f) {
            continue;
        }
        box.geo.union_w =
            std::max(box.geo.union_w, fragment.world_x + fragment.width - box.geo.union_x);
        box.geo.union_h =
            std::max(box.geo.union_h, fragment.world_y + fragment.height - box.geo.union_y);
    }
}

/// The fragment that holds content starting at `block_start` in a fragmented box's own block
/// axis: the last fragment that starts at or above it. It is the rule the per-line positions in

void map_box(Box& box, const Aff2& parent_to_world, float parent_border_inline_start,
             float parent_border_block_start, float parent_border_inline, float parent_border_block,
             WritingMode parent_mode, Direction parent_direction, float scroll_inline,
             float scroll_block, bool descend, bool descend_all) {
    perf::note_world_box();
    // This visit is what consumes a placement mark: the box is being mapped right now, and its
    // whole subtree follows below (or is culled whole, which drops their marks too).
    // A marked box moved, and every box below it moves with it: the children of a moved box are not
    // marked individually, so the walk carries "the subtree is being re-placed" down with it.
    const bool re_place_below = descend_all || box.place_dirty;
    box.place_dirty = false;
    box.place_below = false;
    if (descend) {
        // This walk is about to visit everything below this box itself, so nothing under it is
        // parked any more - and a cull inside the loop raises the bit again along this chain.
        box.parked_below = false;
    }
    box.place_seen = g_place_gen;
    // This walk publishes the box's own origin. Any lag parked for a later scroll is stale.
    box.descendant_lag_x = 0;
    box.descendant_lag_y = 0;
    const LayoutResult* result = result_of(box);
    ++g_stats.mapped;
    fill_geometry(box, result);
    const float child_bi = result != nullptr ? result->geometry.border_box_inline : 0.f;
    const float child_bb = result != nullptr ? result->geometry.border_box_block : 0.f;
    const float logical_i = parent_border_inline_start + box.placement.cb_offset.inline_offset +
                            box.placement.sticky_offset.inline_offset - scroll_inline;
    const float logical_b = parent_border_block_start + box.placement.cb_offset.block_offset +
                            box.placement.sticky_offset.block_offset - scroll_block;
    const PhysicalPoint tl =
        logical_start_to_physical_tl(parent_mode, parent_direction, logical_i, logical_b,
                                     parent_border_inline, parent_border_block, child_bi, child_bb);
    const Xform xform = xform_of(box, box.geo.width, box.geo.height);
    // Remember whether the transform the box's world origin came from was the identity; a later
    // paint-only style change that drops or replaces it has no other way to know the box moved
    // (docs/PERF_REFACTOR.md stage P).
    box.paint_xform_moved = !matrix_is_identity(xform.m);
    // `A.then(B)` applies `A` first, so the local transform goes on the inside: the box's own
    // coordinate space is rotated about its origin *before* it is placed at `tl` in the parent's.
    // The reverse order agreed with this one for a plain translate - which is why it survived - and
    // disagreed for a rotation or a scale (CSS Transforms 1 section 4).
    const Aff2 to_world = local_transform(box.geo.width, box.geo.height, xform)
                              .then(translated(tl.x, tl.y))
                              .then(parent_to_world);
    // docs/PERF.md I6: this is the only place that still holds the box's *previous* world origin,
    // so the damage a move leaves behind (the old pixels) is unioned here with the new rectangle.
    const float prev_world_x = box.geo.world_x;
    const float prev_world_y = box.geo.world_y;
    to_world.apply(0, 0, box.geo.world_x, box.geo.world_y);
    if (prev_world_x != box.geo.world_x || prev_world_y != box.geo.world_y) {
        float min_x = std::min(prev_world_x, box.geo.world_x);
        float min_y = std::min(prev_world_y, box.geo.world_y);
        float max_x = std::max(prev_world_x + box.geo.width, box.geo.world_x + box.geo.width);
        float max_y = std::max(prev_world_y + box.geo.height, box.geo.world_y + box.geo.height);
        html::Node* damage_node = box.node;
        if (damage_node == nullptr && box.parent != nullptr) {
            damage_node = box.parent->node;
        }
        html::add_paint_damage(damage_node,
                               html::PaintRect{min_x, min_y, max_x - min_x, max_y - min_y});
    }
    // Transformed border-box AABB (getBoundingClientRect). Pure translate keeps this equal to
    // (world_x, world_y, width, height); rotate/scale grow it to the corner hull.
    {
        float xs[4];
        float ys[4];
        to_world.apply(0.f, 0.f, xs[0], ys[0]);
        to_world.apply(box.geo.width, 0.f, xs[1], ys[1]);
        to_world.apply(box.geo.width, box.geo.height, xs[2], ys[2]);
        to_world.apply(0.f, box.geo.height, xs[3], ys[3]);
        float min_x = xs[0];
        float max_x = xs[0];
        float min_y = ys[0];
        float max_y = ys[0];
        for (int i = 1; i < 4; ++i) {
            min_x = std::min(min_x, xs[i]);
            max_x = std::max(max_x, xs[i]);
            min_y = std::min(min_y, ys[i]);
            max_y = std::max(max_y, ys[i]);
        }
        box.geo.aabb_x = min_x;
        box.geo.aabb_y = min_y;
        box.geo.aabb_w = std::max(0.f, max_x - min_x);
        box.geo.aabb_h = std::max(0.f, max_y - min_y);
    }
    box.paint_a = to_world.a;
    box.paint_b = to_world.b;
    box.paint_c = to_world.c;
    box.paint_d = to_world.d;
    box.paint_e = to_world.e;
    box.paint_f = to_world.f;
    box.paint_transformed = std::fabs(to_world.a - 1.f) > 1e-5f ||
                            std::fabs(to_world.e - 1.f) > 1e-5f || std::fabs(to_world.b) > 1e-5f ||
                            std::fabs(to_world.d) > 1e-5f;
    // Local position inside the parent border box (physical), without the transform.
    box.geo.x = tl.x;
    box.geo.y = tl.y;
    if (box.node != nullptr) {
        box.geo.scroll_x = box.node->scroll_x;
        box.geo.scroll_y = box.node->scroll_y;
    }
    map_fragments(box, result, parent_to_world, parent_border_inline_start,
                  parent_border_block_start, parent_border_inline, parent_border_block, parent_mode,
                  parent_direction, scroll_inline, scroll_block);
    float child_scroll_inline = 0;
    float child_scroll_block = 0;
    // The document element scrolls with the viewport even when its own overflow is visible.
    // An inner scroll container still uses its overflow mode. Without the root case, a
    // document scroll moved only the root's reported border box and left the body behind.
    const bool viewport_scroll = box.parent == nullptr && box.node != nullptr;
    if ((scrolls(box) || viewport_scroll) && box.node != nullptr) {
        // scrollLeft/Top stay physical; map onto the scroller's logical inline/block axes.
        const WritingMode scroll_mode =
            box.style_ptr != nullptr ? box.style().writing_mode : WritingMode::HorizontalTb;
        child_scroll_inline =
            is_horizontal_wm(scroll_mode) ? box.node->scroll_x : box.node->scroll_y;
        child_scroll_block =
            is_horizontal_wm(scroll_mode) ? box.node->scroll_y : box.node->scroll_x;
        // direction:rtl + horizontal overflow: scrollLeft is in [-extent, 0] with 0 showing the
        // inline-start (right) edge. Content is still placed from the physical left, so shift by
        // the extent up front; negative scroll then reveals earlier (leftward) content.
        if (is_horizontal_wm(scroll_mode) && box.style_ptr != nullptr &&
            box.style().direction == Direction::Rtl) {
            const float view = std::max(0.f, box.geo.width - box.geo.border.horizontal());
            const float extent = std::max(0.f, box.geo.scroll_width - view);
            child_scroll_inline = box.node->scroll_x + extent;
        }
    }
    const WritingMode self_mode =
        box.style_ptr != nullptr ? box.style().writing_mode : WritingMode::HorizontalTb;
    const Direction self_dir = box.style_ptr != nullptr ? box.style().direction : Direction::Ltr;
    const float border_inline_start =
        result != nullptr ? result->geometry.border.inline_start : 0.f;
    const float border_block_start = result != nullptr ? result->geometry.border.block_start : 0.f;
    Frame& self_frame = frames()[&box];
    self_frame.to_world = to_world;
    self_frame.border_inline_start = border_inline_start;
    self_frame.border_block_start = border_block_start;
    self_frame.border_inline = child_bi;
    self_frame.border_block = child_bb;
    self_frame.mode = self_mode;
    self_frame.direction = self_dir;
    self_frame.scroll_inline = child_scroll_inline;
    self_frame.scroll_block = child_scroll_block;
    // Sticky math is logical; pass the same remapped scroll the frame uses for children.
    refresh_sticky(box, child_scroll_inline, child_scroll_block);
    box.geo.culled = false;
    if (!descend) {
        return;
    }
    for (Box* child : box.children) {
        if (child == nullptr) {
            continue;
        }
        // docs/PERF_REFACTOR.md stage P: on a partial walk a child whose own placement inputs did
        // not move and that holds no marked descendant keeps the world rectangle the last walk
        // published for it, together with everything below it. Skipping here is what keeps a
        // scrolled container's cost proportional to that container instead of the document.
        if (g_partial && !re_place_below && !child->place_dirty && !child->place_below) {
            continue;
        }
        const Frame* containing =
            child->style_ptr != nullptr &&
                    (child->style().position_absolute || child->style().position_fixed)
                ? containing_frame(*child)
                : nullptr;
        // By value: the recursive call below may rehash `frames()` (references survive a rehash,
        // but a copy is what the tests after the call need to be provably about this child).
        const Frame child_frame = containing != nullptr ? *containing : self_frame;
        // `position:fixed` against the viewport must not see document scroll: the containing
        // frame is the root, whose scroll is the viewport scroll. A fixed box whose containing
        // block is a transformed ancestor still uses that frame's local scroll.
        float child_scroll_inline = child_frame.scroll_inline;
        float child_scroll_block = child_frame.scroll_block;
        Frame reach_frame = child_frame;
        if (child->style_ptr != nullptr && child->style().position_fixed &&
            (child->abs_containing_block == nullptr ||
             child->abs_containing_block->parent == nullptr)) {
            child_scroll_inline = 0.f;
            child_scroll_block = 0.f;
            reach_frame.scroll_inline = 0.f;
            reach_frame.scroll_block = 0.f;
        }
        // LAYOUT.md section 9 step B: the decision is made here, before the child is visited, so a
        // subtree that cannot reach the window costs one test instead of a walk. The window is the
        // visible rectangle grown by `kRetainedMargin` - a listing kept for step D covers the same
        // margin, so the geometry it needs has to exist.
        const LayoutResult* child_result = result_of(*child);
        const bool provable = child_result != nullptr && cullable(*child, *child_result);
        // A pure scroll slides a subtree whose layout did not change, on screen or off. Mapping
        // every cell again is what made a 60x60 grid scroll walk more than a thousand boxes.
        const bool relaid = child->place_dirty || child->place_below;
        // `parked_below` is what keeps the pure-scroll shortcut honest: `descendants_published`
        // only reads the direct children, while a parked grandchild sits at the sentinel with a
        // current `place_seen` stamp - sliding it would leave it parked forever.
        if (g_partial && provable && !relaid && !child->parked_below &&
            descendants_published(*child) && !self_needs_remap(*child)) {
            const float prev_x = child->geo.world_x;
            const float prev_y = child->geo.world_y;
            const float old_lag_x = child->descendant_lag_x;
            const float old_lag_y = child->descendant_lag_y;
            map_box(*child, child_frame.to_world, child_frame.border_inline_start,
                    child_frame.border_block_start, child_frame.border_inline,
                    child_frame.border_block, child_frame.mode, child_frame.direction,
                    child_scroll_inline, child_scroll_block, false, re_place_below);
            const float push_x = (child->geo.world_x - prev_x) + old_lag_x;
            const float push_y = (child->geo.world_y - prev_y) + old_lag_y;
            const bool offscreen = g_cull && !subtree_reaches(*child, reach_frame, g_visible);
            child->geo.culled = offscreen;
            if (offscreen) {
                child->descendant_lag_x = push_x;
                child->descendant_lag_y = push_y;
                ++g_stats.culled;
            } else {
                for (Box* grand : child->children) {
                    if (grand == nullptr) {
                        continue;
                    }
                    shift_origin(*grand, push_x, push_y);
                    redistribute_lag(*grand, push_x, push_y);
                }
            }
            continue;
        }
        if (g_cull && provable && !subtree_reaches(*child, reach_frame, g_window)) {
            // The child keeps its own rectangle - the app reads it for a caret and the trace
            // reads it for element rects - but nothing below it is visited, and the flag is what
            // keeps the painter and the hit tester out of that subtree.
            const float prev_x = child->geo.world_x;
            const float prev_y = child->geo.world_y;
            const float old_lag_x = child->descendant_lag_x;
            const float old_lag_y = child->descendant_lag_y;
            // Captured before `map_box` clears the bits. A layout change inside this subtree
            // invalidates the relative offsets a pure translation would keep.
            const bool relaid = child->place_dirty || child->place_below;
            const bool published = descendants_published(*child);
            map_box(*child, child_frame.to_world, child_frame.border_inline_start,
                    child_frame.border_block_start, child_frame.border_inline,
                    child_frame.border_block, child_frame.mode, child_frame.direction,
                    child_scroll_inline, child_scroll_block, false, re_place_below);
            ++g_stats.culled;
            if (published && !relaid) {
                // Descendants already have rectangles in this box's old frame. Park the origin
                // delta as lag instead of visiting them. Paint stops at `culled`.
                child->descendant_lag_x = old_lag_x + (child->geo.world_x - prev_x);
                child->descendant_lag_y = old_lag_y + (child->geo.world_y - prev_y);
                child->geo.culled = true;
            } else {
                // Never mapped, or the layout inside moved. Park descendants so a stale world_y
                // cannot paint a ghost, and tell observation it still owes them a rectangle.
                mark_subtree_culled(*child);
            }
            continue;
        }
        map_box(*child, child_frame.to_world, child_frame.border_inline_start,
                child_frame.border_block_start, child_frame.border_inline, child_frame.border_block,
                child_frame.mode, child_frame.direction, child_scroll_inline, child_scroll_block,
                true, re_place_below);
        // Section 9 step B's flag is measured against the *visible* rectangle, not the window the
        // walk is allowed to visit: a box only the retained margin can see keeps its geometry (a
        // listing is recorded from it) and still reports itself unseen, so neither a paint nor a
        // hit picks it up.
        child->geo.culled = g_cull && provable && !subtree_reaches(*child, reach_frame, g_visible);
        // Transforms do not affect layout size, but Chrome's scrollWidth/scrollHeight grow to the
        // transformed border-box AABB. Only fold when the child has a non-translation paint
        // transform; pure-translate AABBs match the border box and must not add float noise.
        if (!child->geo.culled && child->paint_transformed) {
            const float right = std::max(child->geo.aabb_x + child->geo.aabb_w,
                                         child->geo.world_x + child->geo.scroll_width) -
                                box.geo.world_x;
            const float bottom = std::max(child->geo.aabb_y + child->geo.aabb_h,
                                          child->geo.world_y + child->geo.scroll_height) -
                                 box.geo.world_y;
            box.geo.scroll_width = std::max(box.geo.scroll_width, right);
            box.geo.scroll_height = std::max(box.geo.scroll_height, bottom);
        }
    }
}

}  // namespace paint_map_detail
}  // namespace layout
}  // namespace zui
