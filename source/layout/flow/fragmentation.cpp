#include "layout/flow/detail.h"

namespace zui {
namespace layout {
namespace flow_detail {

/// Fragmented block walk (LAYOUT.md section 12).
///
/// A block container inside a fragmentainer places its in-flow children until one of them does not
/// fit, then reports where the next fragment resumes. Floats are placed beside the content that
/// follows them (CSS 2.1 section 9.5) and `clear` drops a box to their bottom edge; neither a float
/// nor a clearance gap advances the block cursor, so the container's balancing is not inflated by
/// them.
///
/// Subset, deliberately: in-flow children collapse their adjacent margins as usual; a child that
/// breaks between its own lines - or deeper, at a nested block child boundary - keeps the part that
/// fits in this fragment and resumes in the next one, with the child's resume state carried in this
/// walk's token (`line_index` / `child_block_offset` / `child_inner_*`); a child that cannot place
/// anything in the room that is left - and any child that meets an empty fragment, where a break
/// would make no progress - moves whole and overflows; a float does not divide across fragmentainers
/// (CSS Multi-column Layout section 7) and opens the next fragment when it does not fit the rest of
/// this one; out-of-flow children are placed in the fragment they start in.
Walk layout_blocks_fragmented(Box& box, const LayoutKey& key, LayoutContext& cx,
                              const UsedEdges& edges, float content_inline, bool inline_known,
                              float scrollbar, const AxisSize& block_percent,
                              const FragmentationContext& frag)
{
    Walk walk;
    const LayoutStyle& style = style_of(box);
    const float flow_inline =
        inline_known ? std::max(0.f, content_inline - scrollbar) : content_inline;
    // Floats (CSS 2.1 section 9.5): the bands are in this box's content frame, so the ones the
    // enclosing formatting context already placed are shifted in. A float does not advance the
    // cursor; the content that follows it in this fragment is shortened through `child_space`, and
    // a float that does not fit the rest of the fragment opens the next one.
    const bool bfc = establishes_bfc(box, style);
    const LogicalPoint box_origin = bfc ? LogicalPoint{} : key.space.bfc_origin;
    std::shared_ptr<const ExclusionSpace> exclusion =
        shift_exclusion(key.space.exclusion, -box_origin.inline_offset, -box_origin.block_offset);
    float float_bottom = 0;
    const float chrome_start = border_edges(style).block_start + edges.padding_block_start;
    const float frag_limit = std::max(0.f, frag.remaining_block);
    const float own_start = std::max(0.f, frag.incoming.block_offset);
    const bool first_fragment = own_start <= 0.01f;
    // Where this fragment's content starts inside the box's own block axis, and how far it may
    // reach: a continuation fragment has no block-start border or padding of its own.
    const float content_own_start = first_fragment ? chrome_start : own_start;
    const float content_own_limit = own_start + frag_limit;
    const int resume_child = frag.incoming.child_index;
    // Resume state for the child at `resume_child` (LAYOUT.md section 12): its line index, and when
    // the break was deeper than its own lines, its nested child boundary (`child_inner_*`).
    const int resume_line = frag.incoming.line_index;
    const int resume_inner_child = frag.incoming.child_inner_index;
    const float resume_inner_block = frag.incoming.child_inner_block;
    float cursor = 0.f;
    Adjoining pending_margin;
    bool seen_border = false;
    bool content_done = true;
    int index = -1;
    for (Box* child : box.children) {
        if (child == nullptr) {
            continue;
        }
        ++index;
        if (resume_child >= 0 && index < resume_child) {
            continue;  // placed by an earlier fragment
        }
        const bool resuming_child = resume_child >= 0 && index == resume_child;
        const LayoutStyle& child_style = style_of(*child);
        // break-before: column|always forces a break before this child when the fragment already
        // holds content (CSS Fragmentation 3 / Multi-column). avoid is handled by not inventing
        // breaks; forced breaks still honor the empty-fragment progress rule.
        if (!resuming_child && (seen_border || !frag.at_fragment_start) &&
            (child_style.break_before == BreakBetween::Always ||
             child_style.break_before == BreakBetween::Column)) {
            walk.fragment_full = true;
            walk.fragment_child = index;
            walk.fragment_block = content_own_start + cursor;
            content_done = false;
            break;
        }
        const AxisSize child_basis =
            inline_known ? AxisSize{true, std::max(0.f, content_inline)} : AxisSize{false, 0};
        const float child_margin_block_start =
            used_edge(child_style.margin_top, child_style.margin_top_percent, child_basis);
        const float child_margin_block_end =
            used_edge(child_style.margin_bottom, child_style.margin_bottom_percent, child_basis);
        const float child_margin_inline =
            used_edge(child_style.margin_left, child_style.margin_left_percent, child_basis) +
            used_edge(child_style.margin_right, child_style.margin_right_percent, child_basis);
        if (out_of_flow(child_style)) {
            LogicalPoint local;
            local.block_offset = chrome_start + cursor;
            child->placement = {};
            child->has_static_local = true;
            child->static_local = local;
            cx.note_out_of_flow(*child, local);
            continue;
        }
        // Clearance (CSS 2.1 section 9.5): a `clear` box drops to the bottom edge of the floats on
        // that side. The float it clears belongs to this fragment (a column is a formatting context
        // of its own), so the floor is the exclusion space the walk built here.
        const float clear_floor = cleared_block(*exclusion, child_style.clear_side);
        Adjoining gap_margin = pending_margin;
        gap_margin.add(child_margin_block_start);
        const float hypothetical = cursor + gap_margin.used();
        const float float_y = std::max(cursor + pending_margin.used(), clear_floor);
        if (child_style.floating) {
            // A float is asked without a fragmentainer of its own: it is not split across
            // fragmentainers (CSS Multi-column Layout section 7), so what it needs is its whole
            // border box. The fit test uses the same "keep the first content of an empty fragment"
            // rule the in-flow children below use.
            ConstraintSpace space = child_space(key, edges, content_inline, inline_known,
                                                inline_known ? AxisSize{true, std::max(0.f, flow_inline)}
                                                             : AxisSize{false, 0},
                                                shared_exclusion(nullptr), AxisSize{false, 0},
                                                block_percent, {}, style.writing_mode,
                                                style.direction);
            const LayoutResult* result = ask_child(cx, *child, space, walk, nullptr);
            if (result == nullptr) {
                continue;
            }
            const float estimate = pending_margin.used() > 0.01f;
            FloatRecord record = place_float(
                *result, child_style.float_right ? FloatSide::Right : FloatSide::Left, float_y,
                std::max(0.f, flow_inline), *exclusion, estimate);
            {
                const std::string* shape =
                    child->node != nullptr ? child->node->style("shape-outside") : nullptr;
                const std::string* margin =
                    child->node != nullptr ? child->node->style("shape-margin") : nullptr;
                attach_shape_outside(record, shape != nullptr ? *shape : std::string_view{},
                                     margin != nullptr ? *margin : std::string_view{});
            }
            if (cursor > 0.01f && record.margin_box.y + record.margin_box.h > frag_limit + 0.01f) {
                // Does not fit the rest of this fragment: it opens the next one (none of it was
                // placed here), which is what the container's break token asks for.
                walk.fragment_full = true;
                walk.fragment_child = index;
                walk.fragment_block = content_own_start + cursor;
                walk.fragment_shortage =
                    std::max(walk.fragment_shortage,
                             record.margin_box.y + record.margin_box.h - frag_limit);
                content_done = false;
                break;
            }
            child->placement.cb_offset.inline_offset =
                edges.padding_inline_start + record.margin_box.x +
                result->geometry.margin.inline_start;
            child->placement.cb_offset.block_offset = edges.padding_block_start +
                                                      record.margin_box.y +
                                                      result->geometry.margin.block_start;
            child->placement.sticky_offset = {};
            apply_relative(*child, inline_known ? content_inline : 0.f, inline_known, block_percent);
            remember_place(walk, child);
            note_child_ink(walk, style, *child, *result, border_edges(style));
            exclusion = ExclusionSpace::append(exclusion, record);
            walk.placed.push_back(record);
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
            float_bottom = std::max(float_bottom, record.margin_box.y + record.margin_box.h);
            walk.has_float = true;
            walk.float_bottom = float_bottom;
            if (!bfc) {
                FloatRecord stored = record;
                stored.margin_box.x += box_origin.inline_offset;
                stored.margin_box.y += box_origin.block_offset;
                walk.floats.push_back(stored);
            }
            continue;
        }
        Adjoining gap = pending_margin;
        if (!resuming_child) {
            // The part of a child that already fitted carried its block-start margin; a continuation
            // fragment gets none (CSS Fragmentation section 3.1).
            gap.add(child_margin_block_start);
        }
        const float border_y = std::max(hypothetical, clear_floor);
        const float space_left = content_own_limit - (content_own_start + border_y);
        // A child that is not a block formatting context of its own sees this box's floats: the
        // bands travel in the enclosing formatting context's frame, which is why the child's own
        // origin goes with them (the child shifts them back into its content frame the same way
        // this box did). A BFC child does not - its box is the one that has to avoid the floats.
        const bool child_bfc = establishes_bfc(*child, child_style);
        std::shared_ptr<const ExclusionSpace> child_exclusion = shared_exclusion(nullptr);
        LogicalPoint child_bfc_origin;
        if (!child_bfc) {
            const Edges child_border = border_edges(child_style);
            const float child_pad_inline =
                used_edge(child_style, LengthProperty::PaddingLeft, child_basis);
            const float child_pad_block =
                used_edge(child_style, LengthProperty::PaddingTop, child_basis);
            child_exclusion =
                shift_exclusion(exclusion, box_origin.inline_offset, box_origin.block_offset);
            child_bfc_origin.inline_offset = box_origin.inline_offset + child_margin_inline +
                                             child_border.inline_start + child_pad_inline;
            child_bfc_origin.block_offset = box_origin.block_offset + border_y +
                                            child_border.block_start + child_pad_block;
        }
        AxisSize available = inline_known ? AxisSize{true, flow_inline} : AxisSize{false, 0};
        // break-inside:avoid keeps the child whole across fragmentainers (CSS Fragmentation 3).
        const bool child_avoid_inside = child_style.break_inside == BreakInside::Avoid;
        auto ask = [&](bool with_fragment) {
            ConstraintSpace space = child_space(key, edges, content_inline, inline_known, available,
                                                child_exclusion, AxisSize{false, 0},
                                                block_percent, child_bfc_origin, style.writing_mode,
                                                style.direction);
            if (with_fragment && !child_avoid_inside) {
                space.fragmentation.active = true;
                space.fragmentation.remaining_block = std::max(0.f, space_left);
                space.fragmentation.incoming = BreakToken{};
                if (resuming_child) {
                    // Hand the child its own resume state back. This walk is the one that cut it:
                    // `line_index` / `child_block_offset` are the child's frame, and when the child
                    // broke at a nested boundary, `child_inner_*` become that child's
                    // `child_index` / `child_block_offset`.
                    space.fragmentation.incoming.child_index = resume_inner_child;
                    space.fragmentation.incoming.line_index = resume_line;
                    space.fragmentation.incoming.block_offset =
                        frag.incoming.child_block_offset;
                    space.fragmentation.incoming.child_block_offset = resume_inner_block;
                }
            }
            return ask_child(cx, *child, space, walk, nullptr);
        };
        const LayoutResult* result = ask(true);
        if (result == nullptr) {
            continue;
        }
        bool child_continues = !result->break_token.done();
        // True when this fragment keeps the part of the child that fits and the child resumes in
        // the next one (it broke between its own lines).
        bool child_split = false;
        if (child_continues) {
            // A child only ever continues across fragments when the fragmentainer asked it directly
            // (see the subset note above). When something is already placed here, the part of the
            // child that fits stays and the child resumes in the next fragment; a child that could
            // not place anything, and a fragment that is still empty (a break there would make no
            // progress), move the child whole instead.
            const bool room_for_part = seen_border || !frag.at_fragment_start;
            // The child placed some content here and names where it resumes - either between its
            // own lines (`line_index > 0`, `child_index < 0`) or deeper at a nested block boundary
            // (`child_index >= 0`). Keep that part; the token carries both the line resume and the
            // nested child boundary (`child_inner_*`). Keeping the child whole is only for content
            // that could not go anywhere else - a break would then leave the fragment empty.
            const bool part_resumable =
                result->geometry.border_box_block > 0.01f &&
                (result->break_token.line_index > 0 || result->break_token.child_index >= 0 ||
                 result->break_token.block_offset > 0.01f);
            if (part_resumable) {
                child_split = true;
            } else if (room_for_part) {
                cx.keep_latest_ask(*child);
                cx.reset_ask_fuse(*child);
                walk.fragment_full = true;
                walk.fragment_child = index;
                walk.fragment_block = content_own_start + cursor;
                // The child's own layout reported how much taller the fragment would have to be for
                // its first unplaced content; that is the amount the container balances by.
                walk.fragment_shortage =
                    std::max(walk.fragment_shortage, result->space_shortage);
                content_done = false;
                break;
            }
            if (!child_split) {
                const LayoutResult* whole = ask(false);
                if (whole == nullptr) {
                    continue;
                }
                result = whole;
                child_continues = false;
            }
        }
        const float child_block = result->geometry.border_box_block;
        // A child that starts where the fragment has no room left opens the next fragment whole.
        // The child's own layout cannot see this: it is asked with zero remaining space, and the
        // "keep the first content" rule answers that a fragment which is *empty* must still make
        // progress - so it reports its whole box and no break of its own. Only the walk knows that
        // something was already placed above it in this fragment.
        if ((seen_border || !frag.at_fragment_start) && space_left <= 0.01f) {
            cx.keep_latest_ask(*child);
            cx.reset_ask_fuse(*child);
            walk.fragment_full = true;
            walk.fragment_child = index;
            walk.fragment_block = content_own_start + cursor;
            walk.fragment_shortage =
                std::max(walk.fragment_shortage, border_y + child_block - frag_limit);
            content_done = false;
            break;
        }
        // The child's whole border box, overflowing the fragment when nothing else fits there.
        // The placement is measured inside the box's own layout - the frame its parent's slices are
        // cut from - not from the top of this fragment, so painting a fragment of the box puts the
        // child where it belongs in the strip.
        const float strip_y = (content_own_start - chrome_start) + border_y;
        child->placement.cb_offset.inline_offset = edges.padding_inline_start;
        child->placement.cb_offset.block_offset = edges.padding_block_start + strip_y;
        child->placement.sticky_offset = {};
        apply_relative(*child, inline_known ? content_inline : 0.f, inline_known, block_percent);
        remember_place(walk, child);
        note_child_ink(walk, style, *child, *result, border_edges(style));
        cursor = border_y + child_block;
        seen_border = true;
        walk.all_through = false;
        walk.content_inline =
            std::max(walk.content_inline,
                     child_margin_inline + result->geometry.border_box_inline);
        pending_margin = {};
        pending_margin.add(child_margin_block_end);
        if (child_split) {
            // The child resumes at the position its own token names; this walk's token carries that
            // resume state (line and/or nested child boundary). The fragment ends here and the
            // block-end margin stays with the next one (CSS Fragmentation section 3.1).
            walk.fragment_full = true;
            walk.fragment_child = index;
            walk.fragment_line = result->break_token.line_index;
            walk.fragment_block = content_own_start + cursor;
            walk.fragment_child_block = result->break_token.block_offset;
            walk.fragment_inner_child = result->break_token.child_index;
            walk.fragment_inner_child_block = result->break_token.child_block_offset;
            walk.fragment_shortage = std::max(walk.fragment_shortage, result->space_shortage);
            content_done = false;
            pending_margin = {};
            break;
        }
        if (child_style.break_after == BreakBetween::Always ||
            child_style.break_after == BreakBetween::Column) {
            // Forced break after this child: open the next fragment before the following sibling.
            walk.fragment_full = true;
            walk.fragment_child = index + 1;
            walk.fragment_block = content_own_start + cursor;
            content_done = false;
            pending_margin = {};
            break;
        }
    }
    if (content_done) {
        walk.content_block = cursor;
        walk.all_through = cursor <= 0.01f;
    } else {
        // The fragment ends where the content it did place ends; the block-end margin of the last
        // child stays with the next fragment (CSS Fragmentation section 3.1).
        walk.content_block = cursor;
    }
    // A block formatting context grows to hold its own floats (CSS 2.1 section 10.6.7).
    if (bfc) {
        walk.content_block = std::max(walk.content_block, float_bottom);
    }
    walk.trailing = pending_margin;
    return walk;
}

}  // namespace flow_detail
}  // namespace layout
}  // namespace zui
