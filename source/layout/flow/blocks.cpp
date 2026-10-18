#include "layout/flow/detail.h"

namespace zui {
namespace layout {
namespace flow_detail {

namespace {

/// Section 9 step C, the half the frame driver cannot do: decide a `content-visibility: auto` box
/// that has never been placed. The driver's pre-pass tests the previous frame's world rectangles,
/// so a list built in this frame has none and every row would be laid out before the next frame
/// could skip it. The block flow knows the child's offset inside its scrollport this pass, and the
/// scrollport's own origin does not depend on its contents, so the same test is answerable here.
///
/// `child_offset_in_content` is the child's block offset from this box's content-box top, before
/// the scroll offset. The child's own placeholder size (`contain-intrinsic-size`, or its previous
/// size) stands in for the box the pass has not computed yet, which is what the skip would use.
/// Returns true when the flag moved and the tree was invalidated for it.
bool decide_relevance_in_pass(LayoutContext& cx, const Box& box, const LayoutStyle& box_style,
                              const UsedEdges& edges, Box& child, const LayoutStyle& child_style,
                              float child_offset_in_content) {
    if (!cx.relevance_valid()) {
        return false;
    }
    const float content_world_x = box.geo.world_x + border_edges(box_style).inline_start +
                                  edges.padding_inline_start - box.geo.scroll_x;
    const float content_world_y = box.geo.world_y + border_edges(box_style).block_start +
                                  edges.padding_block_start - box.geo.scroll_y;
    const float child_world_y = content_world_y + child_offset_in_content;
    // The last border box is the size the overlap test should use. A skipped item is already
    // its placeholder (contain-intrinsic-size plus padding); a laid-out item is its real border
    // box. Preferring the content-only intrinsic under-counts the padding and leaves a box that
    // still crosses the viewport marked irrelevant. A box with no rectangle yet falls back to
    // the intrinsic.
    const float est_inline = child.geo.width > 0.5f
                                 ? child.geo.width
                                 : (child_style.contain_intrinsic_inline >= 0
                                        ? child_style.contain_intrinsic_inline
                                        : 0.f);
    const float est_block = child.geo.height > 0.5f
                                ? child.geo.height
                                : (child_style.contain_intrinsic_block >= 0
                                       ? child_style.contain_intrinsic_block
                                       : 0.f);
    if (child.content_visibility_rendered) {
        return false;
    }
    const bool relevant = content_world_x + est_inline > cx.relevance_left() &&
                          content_world_x < cx.relevance_right() &&
                          child_world_y + est_block > cx.relevance_top() &&
                          child_world_y < cx.relevance_bottom();
    // Same lock as the frame pre-pass: a real border box that intersects the band stays laid
    // out. The first placement has no rectangle yet, so it must not lock a guess the next frame
    // still has to skip.
    if (relevant && child.place_seen != 0 && child.geo.width > 0.5f && child.geo.height > 0.5f) {
        child.content_visibility_rendered = true;
    }
    if (relevant == !child.contents_skipped) {
        return false;
    }
    child.contents_skipped = !relevant;
    // A box that has never been placed has to dirty its ancestors: this pass may be the first
    // one, and the next frame is what used to be the only chance to skip it. A box that already
    // has a rectangle is being laid out by that ancestor right now. Marking it here would drop
    // the ancestor's slots in the middle of the walk.
    if (child.place_seen == 0) {
        cx.content_visibility_changed(child);
    }
    return true;
}

}  // namespace

Walk layout_blocks(Box& box, const LayoutKey& key, LayoutContext& cx, const UsedEdges& edges,
                   float content_inline, bool inline_known, bool inside_top,
                   const std::shared_ptr<const ExclusionSpace>& incoming, bool pending,
                   const std::vector<BfcAsk>* prior, float scrollbar,
                   const AxisSize& block_available, const AxisSize& block_percent,
                   const std::vector<Box*>* reask) {
    note_block_walk();
    Walk walk;
    const LayoutStyle& style = style_of(box);
    const float flow_inline =
        inline_known ? std::max(0.f, content_inline - scrollbar) : content_inline;
    const bool bfc = establishes_bfc(box, style);
    // In-flow children measure their percentages against this box's content box (CSS 2.1 section
    // 10.1). A classic scrollbar is inside that box, so `width: 100%` is the content width
    // after the bar (Chrome: the album row is 180, not the 195 of the border box minus padding).
    // The padding box is the containing block only for absolutely positioned children.
    const float cb_inline =
        inline_known ? std::max(0.f, content_inline - scrollbar) : content_inline;
    const LogicalPoint box_origin = bfc ? LogicalPoint{} : key.space.bfc_origin;
    std::shared_ptr<const ExclusionSpace> exclusion =
        shift_exclusion(incoming, -box_origin.inline_offset, -box_origin.block_offset);
    Adjoining pending_margin;
    float cursor = 0;
    // Whether a block-start border or padding has been crossed: from there on a child's start
    // margin no longer collapses out of this box.
    bool seen_border = false;
    std::size_t bfc_index = 0;
    Box* outside_marker = nullptr;
    const LayoutResult* outside_marker_result = nullptr;
    for (Box* child : box.children) {
        if (child == nullptr) {
            continue;
        }
        // The open list is position:fixed. Source newlines between options are not part of the
        // button: they must not add a line and must not cover the popup.
        if (box.node != nullptr && html::is_select(box.node) && select_source_whitespace(*child)) {
            continue;
        }
        const LayoutStyle& child_style = style_of(*child);
        // A table row's flow used to ask every cell again at the row's content width. The grid
        // algorithm has already stored the cell's Final and written its column offset; that second
        // ask replaced both. Keep the pin and the offset.
        if (child_style.table_part == TablePart::Cell &&
            (style.table_part == TablePart::Row || style.table_part == TablePart::RowGroup ||
             style.table_part == TablePart::HeaderGroup ||
             style.table_part == TablePart::FooterGroup) &&
            child->cache != nullptr && child->cache->pinned_final() != nullptr) {
            note_child_ink(walk, style, *child, *child->cache->pinned_final(), border_edges(style));
            remember_place(walk, child);
            continue;
        }
        if (is_outside_marker(*child, style)) {
            const AxisSize available =
                inline_known ? AxisSize{true, std::max(0.f, flow_inline)} : AxisSize{false, 0};
            {
                outside_marker_result =
                    ask_child(cx, *child,
                              child_space(key, edges, cb_inline, inline_known, available,
                                          shared_exclusion(nullptr), block_available, block_percent,
                                          {}, style.writing_mode, style.direction),
                              walk, reask);
            }
            outside_marker = child;
            continue;
        }
        const AxisSize child_basis =
            inline_known ? AxisSize{true, std::max(0.f, cb_inline)} : AxisSize{false, 0};
        const float child_pad_inline =
            used_edge(child_style, LengthProperty::PaddingLeft, child_basis);
        const float child_pad_block =
            used_edge(child_style, LengthProperty::PaddingTop, child_basis);
        // CSS 2.1 §10.3.3 margin:auto is physical left/right. The child's own layout already
        // resolves used margins via resolve_edges; this early value only seeds abspos static
        // positions before the ask. An out-of-flow child seeds its own below, with `auto` as 0.
        float child_margin_inline = used_edge(child_style, LengthProperty::MarginLeft, child_basis);
        if (child_style.margin_left_auto && inline_known && !child_style.width_auto &&
            !child_style.floating) {
            bool width_as_auto = false;
            const float width = used_main(child_style.width_auto, child_style.width_percent,
                                          child_style.width_px, child_basis, &width_as_auto);
            const float pad_end = used_edge(child_style, LengthProperty::PaddingRight, child_basis);
            const float end_margin =
                child_style.margin_right_auto
                    ? 0.f
                    : used_edge(child_style, LengthProperty::MarginRight, child_basis);
            const float chrome = border_edges(child_style).inline_start +
                                 border_edges(child_style).inline_end + child_pad_inline + pad_end;
            const float border_size = child_style.border_box ? width : width + chrome;
            float leftover = flow_inline - border_size - end_margin;
            const int autos = 1 + (child_style.margin_right_auto ? 1 : 0);
            if (leftover > 0.f) {
                child_margin_inline = leftover / static_cast<float>(autos);
            }
        }
        if (out_of_flow(child_style)) {
            // The line/block cursor is where the box's **margin box** would have sat, and the
            // offsets place the *border* box (CSS 2.1 sections 10.3.7 and 10.6.4), so the box's
            // own used margins move it away from the cursor. `auto` resolves to 0: the inset is
            // `auto` too, and an auto inset means "use the static position", not "centre inside
            // the containing block" - the centre reading put a `margin: 0 auto` box at 380px where
            // Chrome has 0 (`build/scratch/abs-static2.html`).
            const float margin_inline =
                child_style.margin_left_auto
                    ? 0.f
                    : used_edge(child_style, LengthProperty::MarginLeft, child_basis);
            const float margin_block =
                child_style.margin_top_auto
                    ? 0.f
                    : used_edge(child_style, LengthProperty::MarginTop, child_basis);
            // Content-edge cursor only. `placed_static` adds this box's padding once.
            // Baking it here as well doubled it on the layout difftest publishes: the
            // flex shell never pushes a content origin, so `origin_at_padding_edge` stays
            // true, and the second pass still has the previous pin (`span.sr` +12,+12).
            LogicalPoint local;
            local.block_offset = cursor + margin_block;
            local.inline_offset = margin_inline;
            child->placement = {};
            child->has_static_local = true;
            child->static_local = local;
            cx.note_out_of_flow(*child, local);
            continue;
        }

        const bool child_bfc = establishes_bfc(*child, child_style) || child_style.floating;
        float left = 0;
        float right = std::max(0.f, flow_inline);
        float probe_y = cursor;
        if (!inside_top && !seen_border) {
            probe_y = 0;
        } else {
            probe_y = cursor + pending_margin.used();
        }
        float probe_height = 0.01f;
        if (prior != nullptr && bfc_index < prior->size() && !child_style.floating) {
            probe_height = std::max(0.01f, (*prior)[bfc_index].height);
        }
        if (child_bfc && inline_known && !child_style.floating) {
            float_limits(*exclusion, probe_y, probe_y + probe_height, flow_inline, left, right);
        }
        const float gap = std::max(0.f, right - left);
        AxisSize available =
            inline_known ? AxisSize{true, child_bfc ? gap : flow_inline} : AxisSize{false, 0};
        if (child_style.floating && inline_known) {
            available = {true, std::max(0.f, flow_inline)};
        }
        std::shared_ptr<const ExclusionSpace> child_exclusion = shared_exclusion(nullptr);
        LogicalPoint child_bfc_origin;
        if (!child_bfc) {
            child_exclusion =
                shift_exclusion(exclusion, box_origin.inline_offset, box_origin.block_offset);
            child_bfc_origin.inline_offset = box_origin.inline_offset + child_margin_inline +
                                             border_edges(child_style).inline_start +
                                             child_pad_inline;
            child_bfc_origin.block_offset = box_origin.block_offset + probe_y +
                                            border_edges(child_style).block_start + child_pad_block;
        }
        LogicalPoint content_origin;
        content_origin.inline_offset = edges.padding_inline_start + child_margin_inline +
                                       border_edges(child_style).inline_start + child_pad_inline;
        content_origin.block_offset = edges.padding_block_start + probe_y +
                                      border_edges(child_style).block_start + child_pad_block;
        if (cx.origin_at_padding_edge()) {
            content_origin.inline_offset += border_edges(style).inline_start;
            content_origin.block_offset += border_edges(style).block_start;
        }
        cx.push_content_origin(content_origin);
        ConstraintSpace space = child_space(key, edges, cb_inline, inline_known, available,
                                            child_exclusion, block_available, block_percent,
                                            child_bfc_origin, style.writing_mode, style.direction);
        const float float_floor = cleared_block(*exclusion, child_style.clear_side);
        if (child_style.clear_side != ClearSide::None && float_floor > probe_y + 0.01f) {
            space.clearance.apply = true;
            space.clearance.block_offset = float_floor;
        }
        // `content-visibility: auto` is decided from this pass's cursor, before the child is
        // asked, so a box that scrolled into view takes its real size in the same layout that
        // sees the new scroll offset. Two guards: the pass has to be the real one (`Final`, and
        // an inline size that is already known - a provisional offset could decide the wrong way),
        // and the scrollport has to be placed already, so its origin is the real one. A brand-new
        // nested scrollport has no origin yet; the next frame decides its rows. The document
        // element is its own scrollport (`parent == nullptr`) and can decide on the first pass.
        if (inline_known && key.space.request.kind == RequestKind::Final &&
            child_style.content_visibility_auto &&
            (box.place_seen != 0 || box.parent == nullptr)) {
            decide_relevance_in_pass(cx, box, style, edges, *child, child_style,
                                     probe_y + border_edges(child_style).block_start);
        }
        const LayoutResult* result = nullptr;
        {
            result = ask_child(cx, *child, space, walk, reask);
        }
        // A cache hit does not build lines again. Absolute descendants keep the static
        // position they recorded against this same frame, so replay it now that the frame
        // is the final one rather than a measure pass that happened to share the key.
        for (Box* nested : child->children) {
            if (nested != nullptr && nested->has_static_local) {
                cx.note_out_of_flow(*nested, nested->static_local);
            }
        }
        cx.pop_content_origin();
        if (result == nullptr) {
            continue;
        }

        if (child_style.floating) {
            const float clear_y = cleared_block(*exclusion, child_style.clear_side);
            const float float_y = std::max(probe_y, clear_y);
            const bool estimate = pending && pending_margin.used() > 0.01f;
            FloatRecord record =
                place_float(*result, child_style.float_right ? FloatSide::Right : FloatSide::Left,
                            float_y, std::max(0.f, flow_inline), *exclusion, estimate);
            {
                const std::string* shape =
                    child->node != nullptr ? child->node->style("shape-outside") : nullptr;
                const std::string* margin =
                    child->node != nullptr ? child->node->style("shape-margin") : nullptr;
                attach_shape_outside(record, shape != nullptr ? *shape : std::string_view{},
                                     margin != nullptr ? *margin : std::string_view{});
            }
            // margin_box.x is the margin box's physical left in a horizontal container.
            // The border edge adds the physical left margin. Under rtl that is inline-end;
            // adding inline-start would shift the float by margin-right.
            float margin_before_border = result->geometry.margin.inline_start;
            if (is_horizontal_wm(style.writing_mode)) {
                margin_before_border = logical_edges_to_physical(
                    child_style.writing_mode, child_style.direction, result->geometry.margin).left;
            }
            child->placement.cb_offset.inline_offset =
                content_inline_origin(style.writing_mode, style.direction, edges) +
                record.margin_box.x + margin_before_border;
            child->placement.cb_offset.block_offset = edges.padding_block_start +
                                                      record.margin_box.y +
                                                      result->geometry.margin.block_start;
            child->placement.sticky_offset = {};
            apply_relative(*child, cb_inline, inline_known, block_percent);
            remember_place(walk, child);
            note_child_ink(walk, style, *child, *result, border_edges(style));
            exclusion = ExclusionSpace::append(exclusion, record);
            walk.placed.push_back(record);
            // Sibling notes are read only when this box is a BFC that placed a float
            // (flow.cpp, the second-pass overlap). A non-BFC forwards floats and never
            // looks at the vector.
            if (bfc) {
                Walk::SiblingNote sibling;
                sibling.box = child;
                sibling.is_float = true;
                sibling.order = static_cast<int>(walk.siblings.size());
                sibling.x = record.margin_box.x;
                sibling.y = record.margin_box.y;
                sibling.w = record.margin_box.w;
                sibling.h = record.margin_box.h;
                walk.siblings.push_back(sibling);
            }
            if (!bfc) {
                FloatRecord stored = record;
                stored.margin_box.x += box_origin.inline_offset;
                stored.margin_box.y += box_origin.block_offset;
                walk.floats.push_back(stored);
            }
            walk.float_bottom =
                std::max(walk.float_bottom, record.margin_box.y + record.margin_box.h);
            walk.has_float = true;
            continue;
        }

        // The child reports its geometry in its own logical axes. When they are orthogonal to the
        // parent's (LAYOUT section 3), the parent reads the contribution through a physical round
        // trip: the child's inline extent is the parent's block extent and the other way round,
        // and the same holds for the margins. For a same-mode pair this is the identity, so the
        // mapping is unconditional and the common path is untouched.
        const PhysicalSize child_physical =
            logical_size_to_physical(child_style.writing_mode, result->geometry.border_box_inline,
                                     result->geometry.border_box_block);
        const LogicalSize child_box = physical_size_to_logical(
            style.writing_mode, child_physical.width, child_physical.height);
        const PhysicalEdges child_margin_physical = logical_edges_to_physical(
            child_style.writing_mode, child_style.direction, result->geometry.margin);
        const Edges child_margin = physical_edges_to_logical(
            style.writing_mode, style.direction, child_margin_physical.top,
            child_margin_physical.right, child_margin_physical.bottom, child_margin_physical.left);
        const bool child_orthogonal = axes_orthogonal(style.writing_mode, child_style.writing_mode);
        // The margin strut describes the child's own block axis, which is not the parent's when
        // the flows are orthogonal; the mapped margins are the honest answer there, without
        // collapse-through semantics the strut would have carried.
        const float strut_start =
            child_orthogonal ? child_margin.block_start : result->margin_strut.start;
        const float strut_end =
            child_orthogonal ? child_margin.block_end : result->margin_strut.end;
        const bool strut_adjoining = !child_orthogonal && result->margin_strut.adjoining;
        // Horizontal-tb places on the physical left margin (CSS 2.1 §10.3.3). Under direction:rtl
        // that is logical inline-end, so using inline_start would pin margin-left:auto boxes to x=0.
        const float used_margin = is_horizontal_wm(style.writing_mode) ? child_margin_physical.left
                                                                       : child_margin.inline_start;
        const bool through = strut_adjoining && !result->margin_strut.blocks_margin_collapse &&
                             !result->margin_strut.clearance &&
                             child_style.clear_side == ClearSide::None;
        // A block formatting context keeps its children's margins inside itself. Its own
        // margin still collapses with the parent and with siblings (CSS 2.1 section 8.3.1).
        // Treating `blocks_margin_collapse` as a separation put an overflow box's margin
        // inside the parent: `#pane { margin:20px; overflow:auto }` then made the body 20px
        // too tall and shifted the pane down inside it.
        const bool separate = child_orthogonal || result->margin_strut.clearance;
        if (through) {
            // An empty block's top and bottom margins collapse through it, so the box itself sits
            // where a non-empty sibling with the same margins would land: after its start margin
            // has collapsed with everything before it, and before its end margin is added to the
            // run the next sibling sees. Placing it at the *end* of the run moved every empty
            // block down by its own bottom margin (contacts' `#editor-title` is 14px low) and made
            // a trailing one sit below its collapsed margin (measured: `build/empty_block_probe.html`
            // covers the first, middle, trailing, nested and margin-10/14 shapes).
            Adjoining box_gap = pending_margin;
            if (!separate) {
                // The child's *own* start margin, not `strut_start`: an empty block's strut
                // reports the whole collapsed run (max of both margins), and taking that as the
                // start margin is what moved the box onto the run's far edge.
                box_gap.add(child_margin.block_start);
            }
            const bool empty_collapse_out = !bfc && !inside_top && !seen_border && !separate;
            // A box that is empty because a *child* collapsed through it keeps the whole run
            // above its border box (CSS 2.1 8.3.1 asks where a non-zero bottom border would have
            // put it): its own start margin and the child's collapsed run are one run, and the
            // box sits at its far edge. A box with no in-flow child at all sits on the near edge,
            // after just its own start margin (`build/empty_block_probe.html` pins both shapes).
            bool has_in_flow_child = false;
            for (const Box* kid : child->children) {
                if (kid != nullptr && kid->generated == html::PseudoElement::Count &&
                    !out_of_flow(style_of(*kid))) {
                    has_in_flow_child = true;
                    break;
                }
            }
            const float through_y =
                empty_collapse_out ? 0.f
                                   : cursor + (has_in_flow_child ? pending_margin.used() + strut_start
                                                                 : box_gap.used());
            if (empty_collapse_out) {
                walk.leading.add(box_gap.used());
            }
            pending_margin.add(strut_start);
            pending_margin.add(strut_end);
            child->placement.cb_offset.inline_offset =
                content_inline_origin(style.writing_mode, style.direction, edges) + used_margin;
            child->placement.cb_offset.block_offset = edges.padding_block_start + through_y;
            child->placement.sticky_offset = {};
            apply_relative(*child, cb_inline, inline_known, block_percent);
            remember_place(walk, child);
            if (!result->floats.empty()) {
                for (FloatRecord band : result->floats) {
                    band.pending = pending;
                    FloatRecord local = band;
                    local.margin_box.x -= box_origin.inline_offset;
                    local.margin_box.y -= box_origin.block_offset;
                    exclusion = ExclusionSpace::append(exclusion, local);
                    walk.placed.push_back(local);
                    walk.has_float = true;
                    walk.float_bottom =
                        std::max(walk.float_bottom, local.margin_box.y + local.margin_box.h);
                    if (!bfc) {
                        walk.floats.push_back(band);
                    }
                }
            }
            continue;
        }

        Adjoining gap_margin = pending_margin;
        if (!separate) {
            gap_margin.add(strut_start);
        }
        float border_y = 0;
        // A BFC keeps the first in-flow child's block-start margin inside itself (CSS 2.1
        // §8.3.1 / Containment layout). Collapsing that margin into `walk.leading` is only for
        // non-BFC parents that have no block-start border or padding.
        const bool collapse_out = !bfc && !inside_top && !seen_border && !separate;
        const float hypothetical =
            collapse_out ? 0.f
                         : cursor + (separate ? pending_margin.used() + child_margin.block_start
                                              : gap_margin.used());
        const float clear_floor = cleared_block(*exclusion, child_style.clear_side);
        const bool cut =
            child_style.clear_side != ClearSide::None && hypothetical < clear_floor - 0.01f;
        if (!cut && collapse_out) {
            walk.leading.add(gap_margin.used());
            border_y = 0;
        } else if (!cut) {
            border_y = hypothetical;
        } else {
            border_y = clear_floor;
        }
        float border_x = used_margin;
        if (child_bfc && inline_known) {
            float range_left = 0;
            float range_right = content_inline;
            const float range_height = std::max(0.01f, child_box.block_size);
            float_limits(*exclusion, border_y, border_y + range_height, flow_inline, range_left,
                         range_right);
            border_x = range_left + used_margin;
            BfcAsk note;
            note.box = child;
            note.y = border_y;
            note.height = range_height;
            note.available = available.definite ? available.px : content_inline;
            if (prior != nullptr && bfc_index < prior->size() &&
                std::abs(range_height - (*prior)[bfc_index].height) > 0.5f) {
                float again_left = 0;
                float again_right = content_inline;
                float_limits(*exclusion, border_y, border_y + range_height, flow_inline, again_left,
                             again_right);
                if (std::abs((again_right - again_left) - note.available) > 0.5f) {
                    walk.unstable = true;
                }
            }
            walk.bfc_asks.push_back(note);
            ++bfc_index;
        }
        // An element's own text becomes an anonymous box beside its block-level children, and CSS
        // gives that box the container's width so `text-align` lines the text up inside it. Here the
        // box shrinks to its text, so the alignment has to move the box: the reader's chapter title
        // (`<h2>` with a `::after` rule) and its "下一章" button both sat at the left edge with
        // their headings centred in the browser.
        if (child->role == BoxRole::Text && child->node == nullptr && inline_known &&
            style.text_align != TextAlign::Justify) {
            const bool rtl = style.direction == Direction::Rtl;
            const bool pack_center = style.text_align == TextAlign::Center;
            const bool pack_right = style.text_align == TextAlign::Right ||
                                    (style.text_align == TextAlign::End && !rtl) ||
                                    (style.text_align == TextAlign::Start && rtl);
            const float free =
                flow_inline - used_margin - child_margin.inline_end - child_box.inline_size;
            if (free > 0.01f) {
                if (pack_center) {
                    border_x += free * 0.5f;
                } else if (pack_right) {
                    border_x += free;
                }
            }
        }
        child->placement.cb_offset.inline_offset =
            content_inline_origin(style.writing_mode, style.direction, edges) + border_x;
        child->placement.cb_offset.block_offset = edges.padding_block_start + border_y;
        child->placement.sticky_offset = {};
        // The origin frame this child was asked in used `probe_y`. The border edge can
        // land lower once adjoining margins are known. Absolute static positions were
        // recorded in the earlier frame, so move them by the same amount as the child.
        const float place_shift = border_y - probe_y;
        if (std::fabs(place_shift) > 0.01f) {
            std::vector<Box*> pending_shift;
            pending_shift.push_back(child);
            while (!pending_shift.empty()) {
                Box* current = pending_shift.back();
                pending_shift.pop_back();
                if (current == nullptr) {
                    continue;
                }
                if (current->has_static_local) {
                    cx.nudge_static_position(*current, place_shift);
                }
                if (current != child && current->style_ptr != nullptr &&
                    current->style().cb_for_absolute) {
                    continue;
                }
                for (Box* nested : current->children) {
                    pending_shift.push_back(nested);
                }
            }
        }
        // The baselines are measured from the border box, so they must not follow the relative
        // offset that `apply_relative` adds below.
        if (!std::isnan(result->geometry.baselines.first) && std::isnan(walk.baselines.first)) {
            walk.baselines.first = border_edges(style).block_start +
                                   child->placement.cb_offset.block_offset +
                                   result->geometry.baselines.first;
        }
        if (!std::isnan(result->geometry.baselines.last)) {
            walk.baselines.last = border_edges(style).block_start +
                                  child->placement.cb_offset.block_offset +
                                  result->geometry.baselines.last;
        }
        // `remember_place` snapshots `cb_offset` for the frame-end pin walk, and `store` replays
        // that snapshot. Recording it before `apply_relative` replayed the pre-relative position
        // and silently undid the relative offset, so the snapshot has to be taken after it.
        // LAYOUT.md section 4: a relative offset is part of the placement, not of the key.
        apply_relative(*child, cb_inline, inline_known, block_percent);
        remember_place(walk, child);
        note_child_ink(walk, style, *child, *result, border_edges(style));
        cursor = border_y + child_box.block_size;
        // Scrollable overflow is inherited: a descendant that reaches past its own box - a line's
        // content area, an overflowing paragraph, a scroll container's content - also reaches past
        // every ancestor, which is why a browser's `scrollHeight` on the page root reports the
        // deepest ink even when nothing on the way is a scroll container (LAYOUT section 4).
        if (result->overflow.scrollable.h > 0.f &&
            child_style.overflow_x == OverflowMode::Visible &&
            child_style.overflow_y == OverflowMode::Visible) {
            // A child in another writing mode (an orthogonal flow) reports its extent in its own
            // logical axes, so go through physical to land it on this box's block axis.
            const PhysicalSize child_physical =
                logical_size_to_physical(child_style.writing_mode, result->overflow.scrollable.w,
                                         result->overflow.scrollable.h);
            const LogicalSize child_in_this_box = physical_size_to_logical(
                style.writing_mode, child_physical.width, child_physical.height);
            walk.ink_block = std::max(walk.ink_block, border_y + child_in_this_box.block_size);
        }
        // The same rule on the inline axis: a child that overflows sideways - a multi-column
        // container whose columns run past its own box, say - reaches past every ancestor, so the
        // ancestor's `scrollWidth` has to grow too.
        if (result->overflow.scrollable.w > 0.f &&
            child_style.overflow_x == OverflowMode::Visible &&
            child_style.overflow_y == OverflowMode::Visible) {
            const PhysicalSize child_physical =
                logical_size_to_physical(child_style.writing_mode, result->overflow.scrollable.w,
                                         result->overflow.scrollable.h);
            const LogicalSize child_in_this_box = physical_size_to_logical(
                style.writing_mode, child_physical.width, child_physical.height);
            walk.content_inline =
                std::max(walk.content_inline, border_x + child_in_this_box.inline_size);
        }
        pending_margin = {};
        if (!separate) {
            pending_margin.add(strut_end);
        } else {
            pending_margin.add(child_margin.block_end);
        }
        seen_border = true;
        walk.all_through = false;
        walk.content_inline = std::max(walk.content_inline,
                                       border_x + child_box.inline_size + child_margin.inline_end);
        if (bfc) {
            Walk::SiblingNote sibling;
            sibling.box = child;
            sibling.is_bfc = child_bfc;
            sibling.has_lines = result->lines != nullptr && !result->lines->lines.empty();
            sibling.has_floats = !result->floats.empty();
            sibling.order = static_cast<int>(walk.siblings.size());
            sibling.x = border_x - used_margin;
            sibling.y = border_y - child_margin.block_start;
            sibling.w = child_margin.inline_start + child_box.inline_size + child_margin.inline_end;
            sibling.h = child_margin.block_start + child_box.block_size + child_margin.block_end;
            if (result->lines != nullptr) {
                const float line_origin =
                    border_y + border_edges(child_style).block_start + child_pad_block;
                for (const LineBox& line : result->lines->lines) {
                    Walk::SiblingNote::LineSpan span;
                    span.y = line_origin + line.block_offset;
                    span.h = std::max(line.block_size, 0.01f);
                    sibling.line_spans.push_back(span);
                }
            }
            walk.siblings.push_back(sibling);
        }
        if (!result->floats.empty() && !child_bfc) {
            for (FloatRecord band : result->floats) {
                const bool origin_moved = std::abs(border_y - probe_y) > 0.5f;
                band.pending = pending && (band.pending || origin_moved);
                FloatRecord local = band;
                local.margin_box.x -= box_origin.inline_offset;
                local.margin_box.y -= box_origin.block_offset;
                exclusion = ExclusionSpace::append(exclusion, local);
                walk.placed.push_back(local);
                walk.has_float = true;
                walk.float_bottom =
                    std::max(walk.float_bottom, local.margin_box.y + local.margin_box.h);
                if (!bfc) {
                    walk.floats.push_back(band);
                }
            }
        }
    }
    if (!seen_border) {
        walk.leading.add(pending_margin.used());
        walk.all_through = true;
        walk.content_block = 0;
    } else {
        walk.trailing = pending_margin;
        walk.content_block = cursor;
        walk.all_through = false;
    }
    if (bfc) {
        walk.content_block = std::max(walk.content_block, walk.float_bottom);
        walk.content_block += walk.leading.used();
        walk.content_block += walk.trailing.used();
        walk.leading = {};
        walk.trailing = {};
        walk.floats.clear();
        if (seen_border || walk.content_block > 0.f) {
            walk.all_through = false;
        }
    }
    if (outside_marker != nullptr) {
        place_outside_marker(*outside_marker, outside_marker_result, edges, 0.f, walk);
    }
    return walk;
}

}  // namespace flow_detail
}  // namespace layout
}  // namespace zui
