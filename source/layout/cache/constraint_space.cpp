#include "layout/cache/constraint_space.h"

#include "layout/cache/hash_mix.h"
#include "layout/cache/key_policy.h"
#include "layout/cache/logical_axes.h"
#include "layout/cache/quantum.h"
#include "layout/layout_style.h"

#include <array>
#include <cstddef>
#include <optional>
#include <vector>

namespace zui {
namespace layout {
namespace {

/// The tokens of a key, in order, written to whatever `Out` is. `hash()` streams them straight into
/// the mix, `equal()` writes one side into a `TokenBuffer` and compares the other as it arrives.
/// One generator, so the three can never disagree about what a key contains.
template <typename Out> void write_bit(Out& out, std::uint64_t value) { out.push_back(value); }

template <typename Out> void write_q(Out& out, float value) {
    out.push_back(static_cast<std::uint64_t>(quantum_key(value)));
}

template <typename Out> void write_axis(Out& out, const AxisSize& axis) {
    write_bit(out, axis.definite ? 1 : 0);
    if (axis.definite) {
        write_q(out, axis.px);
    }
}

/// A sink that folds every token into the hash as it arrives. `hash_tokens` starts from zero and
/// mixes in order, so this produces the value the vector version did, with no allocation.
struct HashSink {
    std::uint64_t hash = 0;
    void push_back(std::uint64_t value) { hash_mix(hash, value); }
};

/// The right-hand key's tokens, inline for the common case. A key is about forty tokens; the only
/// thing that can push past the inline capacity is a float-heavy exclusion space, and then the
/// buffer spills to a vector. Either way `equal()` no longer allocates for a plain key - it used to
/// build two vectors on every cache hit, because `KeyEq` calls it whenever the hash bucket matches.
class TokenBuffer {
public:
    void push_back(std::uint64_t value) {
        if (size_ < inline_.size()) {
            inline_[size_] = value;
            ++size_;
            return;
        }
        if (!spilled_) {
            spilled_.emplace();
            spilled_->assign(inline_.begin(), inline_.end());
        }
        spilled_->push_back(value);
        ++size_;
    }

    std::size_t size() const { return size_; }

    std::uint64_t at(std::size_t index) const {
        return index < inline_.size() ? inline_[index] : (*spilled_)[index];
    }

private:
    std::array<std::uint64_t, 64> inline_{};
    // Absent until a key exceeds the inline words. An empty `vector` member still allocated a
    // debug iterator proxy on every seal, and a normal key never spills.
    std::optional<std::vector<std::uint64_t>> spilled_;
    std::size_t size_ = 0;
};

/// Compares the left-hand key's tokens against the right-hand ones as they are generated. Every
/// token is consumed even after a mismatch, so a key that is a prefix of the other still fails.
struct CompareSink {
    const TokenBuffer& right;
    std::size_t index = 0;
    bool equal = true;

    void push_back(std::uint64_t value) {
        if (index >= right.size() || right.at(index) != value) {
            equal = false;
        }
        ++index;
    }
};

bool shrink_inline(const LayoutStyle& style) {
    const bool abspos = style.position_absolute || style.position_fixed;
    const bool inline_auto =
        is_horizontal_wm(style.writing_mode) ? style.width_auto : style.height_auto;
    return inline_auto && (abspos || style.floating);
}

bool shrink_block(const LayoutStyle& style) {
    const bool abspos = style.position_absolute || style.position_fixed;
    const bool block_auto =
        is_horizontal_wm(style.writing_mode) ? style.height_auto : style.width_auto;
    return block_auto && abspos;
}

bool forwards_floats(const LayoutStyle& style) {
    if (style.floating || style.position_absolute || style.position_fixed || style.flow_root) {
        return false;
    }
    if (style.overflow_x != OverflowMode::Visible || style.overflow_y != OverflowMode::Visible) {
        return false;
    }
    return style.outer != DisplayOuter::Inline;
}

/// Section 9 F: a box's key carries the containing block's *inline* percentage basis only when the
/// box's own style can read it. The readers of `ConstraintSpace::percentage.inline_size` are a
/// closed list, and each one is a property on the box's own style:
///
///   `resolve_edges` / `logical_margin` / `padding_edges`  margin-* / padding-* (all four sides:
///       a percentage padding and margin resolve against the *inline* size on every side)
///   `used_main(...)` for width / height and min / max  width_percent, min_width_percent,
///       max_width_percent in a horizontal writing mode and the height ones in a vertical one
///   `used_indent`  text_indent_percent
///   a flex item's basis  flex_basis_percent
///   gap resolution  row_gap_percent / column_gap_percent
///   an out-of-flow box's insets  top / left / right / bottom_percent
///   shrink-to-fit  an absolutely positioned or floating box with an `auto` inline size: its
///       available space *is* the basis minus its margins, so the basis moves its used size even
///       with no percentage anywhere in the style
///
/// A style that holds no percentage at all and is not shrink-to-fit cannot see the basis, so
/// leaving it out of that box's key costs nothing and lets a box whose own available space did not
/// move keep its result across a resize.
///
/// `calc()` / `min()` / `max()` / `clamp()` on `width` or `height` never sets a percent flag.
/// That path reads the raw declaration (`flow.cpp`) and the viewport, so `note_icb_length` keeps
/// the basis in every key of such a document. The same math on margin, padding, or the inline-axis
/// min/max size is kept in `math_lengths` and resolved by `used_edge` / `used_main` against this
/// basis, without setting the percent flag. A flex item's `max-width: min(72%, 520px)` is that
/// case: an earlier shrink-to-fit ask and the stretched row's final ask share an available size
/// and differ only in the percentage basis, and dropping the basis reused the narrow result.
///
/// Gap and flex-basis math are resolved against the flex container's own inner size, which is
/// already covered by `available` or by a percentage on the container's width. Insets are folded
/// into the available size before the child is asked. Those kept texts do not read this token.
bool kept_math_reads_inline_percentage(const LayoutStyle& style) {
    if (style.math_lengths.empty()) {
        return false;
    }
    const bool horizontal = is_horizontal_wm(style.writing_mode);
    for (const MathLength& kept : style.math_lengths) {
        switch (kept.prop) {
        case LengthProperty::PaddingTop:
        case LengthProperty::PaddingRight:
        case LengthProperty::PaddingBottom:
        case LengthProperty::PaddingLeft:
        case LengthProperty::MarginTop:
        case LengthProperty::MarginRight:
        case LengthProperty::MarginBottom:
        case LengthProperty::MarginLeft:
            return true;
        case LengthProperty::MinWidth:
        case LengthProperty::MaxWidth:
            if (horizontal) {
                return true;
            }
            break;
        case LengthProperty::MinHeight:
        case LengthProperty::MaxHeight:
            if (!horizontal) {
                return true;
            }
            break;
        default:
            break;
        }
    }
    return false;
}

bool kept_math_reads_block_percentage(const LayoutStyle& style) {
    if (style.math_lengths.empty()) {
        return false;
    }
    for (const MathLength& kept : style.math_lengths) {
        switch (kept.prop) {
        case LengthProperty::MinHeight:
        case LengthProperty::MaxHeight:
        case LengthProperty::Top:
        case LengthProperty::Bottom:
            return true;
        default:
            break;
        }
    }
    return false;
}

/// `percentage.block_size` is what a used height, min-height, max-height, or a vertical inset
/// resolves against. Padding and margin percentages resolve against the inline size, and a
/// flex-basis percentage is resolved by the flex container before it asks the item. A box with
/// none of these keeps its result when only that basis moves.
bool reads_block_percentage(const LayoutStyle& style) {
    return style.height_percent || style.min_height_percent || style.max_height_percent ||
           style.top_percent || style.bottom_percent || kept_math_reads_block_percentage(style);
}

bool reads_inline_percentage(const LayoutStyle& style) {
    const bool horizontal = is_horizontal_wm(style.writing_mode);
    const bool own_inline_size =
        horizontal ? (style.width_percent || style.min_width_percent || style.max_width_percent)
                   : (style.height_percent || style.min_height_percent || style.max_height_percent);
    const bool edges = style.margin_top_percent || style.margin_right_percent ||
                       style.margin_bottom_percent || style.margin_left_percent ||
                       style.padding_top_percent || style.padding_right_percent ||
                       style.padding_bottom_percent || style.padding_left_percent;
    const bool insets =
        style.top_percent || style.left_percent || style.right_percent || style.bottom_percent;
    const bool inline_auto = horizontal ? style.width_auto : style.height_auto;
    const bool shrink_to_fit =
        inline_auto && (style.position_absolute || style.position_fixed || style.floating);
    return own_inline_size || edges || insets || style.text_indent_percent ||
           style.flex_basis_percent || style.column_gap_percent || style.row_gap_percent ||
           shrink_to_fit || kept_math_reads_inline_percentage(style);
}

template <typename Out> void write_exclusion(Out& out, const ExclusionSpace* exclusion) {
    if (exclusion == nullptr || exclusion->bands().empty()) {
        write_bit(out, 0);
        return;
    }
    write_bit(out, exclusion->bands().size());
    for (const FloatRecord& band : exclusion->bands()) {
        write_bit(out, static_cast<std::uint64_t>(band.side));
        write_q(out, band.margin_box.x);
        write_q(out, band.margin_box.y);
        write_q(out, band.margin_box.w);
        write_q(out, band.margin_box.h);
        write_bit(out, band.pending ? 1 : 0);
        write_bit(out, static_cast<std::uint64_t>(band.shape.kind));
        write_q(out, band.shape.inset_t);
        write_q(out, band.shape.inset_r);
        write_q(out, band.shape.inset_b);
        write_q(out, band.shape.inset_l);
        write_q(out, band.shape.inset_round);
        write_q(out, band.shape.cx);
        write_q(out, band.shape.cy);
        write_q(out, band.shape.rx);
        write_q(out, band.shape.ry);
        write_q(out, band.shape_margin);
    }
}

template <typename Out>
void tokenize(const ConstraintSpace& space, const LayoutStyle& style, Out& out) {
    write_axis(out, space.available.inline_size);
    write_axis(out, space.available.block_size);
    // Section 9 F: per box, not per document - a box that cannot read the inline basis keeps its
    // result when the basis moves but its own available space does not.
    if (icb_in_layout_keys() || reads_inline_percentage(style)) {
        write_axis(out, space.percentage.inline_size);
    }
    // Same rule as the inline basis, on the block axis. Writing it for every box made a grid's
    // second measure miss every item: the min-content ask does not read the row size, but the
    // container's content height had moved and the token moved with it.
    if (reads_block_percentage(style) || space.honor_block_basis) {
        write_axis(out, space.percentage.block_size);
    }
    write_bit(out, static_cast<std::uint64_t>(space.request.kind));
    write_bit(out, static_cast<std::uint64_t>(space.request.axis));
    write_bit(out, space.request.needs_baseline ? 1 : 0);
    write_bit(out, space.stretch_block ? 1 : 0);
    write_bit(out, space.stretch_inline ? 1 : 0);
    write_bit(out, static_cast<std::uint64_t>(space.writing_mode));
    write_bit(out, static_cast<std::uint64_t>(space.direction));
    write_bit(out, space.clearance.apply ? 1 : 0);
    if (space.clearance.apply) {
        write_q(out, space.clearance.block_offset);
    }
    write_exclusion(out, space.exclusion.get());
    write_bit(out, space.fragmentation.active ? 1 : 0);
    write_q(out, space.fragmentation.remaining_block);
    write_bit(out, static_cast<std::uint64_t>(space.fragmentation.fragment_index));
    write_bit(out, space.fragmentation.at_fragment_start ? 1 : 0);
    write_bit(out, static_cast<std::uint64_t>(space.fragmentation.incoming.child_index));
    write_bit(out, static_cast<std::uint64_t>(space.fragmentation.incoming.line_index));
    write_bit(out, static_cast<std::uint64_t>(space.fragmentation.incoming.text_offset));
    write_q(out, space.fragmentation.incoming.block_offset);
    write_q(out, space.fragmentation.incoming.child_block_offset);
    write_bit(out, static_cast<std::uint64_t>(space.fragmentation.incoming.child_inner_index));
    write_q(out, space.fragmentation.incoming.child_inner_block);
    write_bit(out, space.allow_break ? 1 : 0);
    // Section 9 step C: the `content-visibility: auto` decision is part of the key, so a box whose
    // skip state flips cannot be answered from the other state's slot. The token is written only
    // when the flag is set, so every key that does not use `content-visibility: auto` keeps the
    // exact token stream it had before the flag existed.
    if (space.contents_skipped) {
        write_bit(out, 1);
    }
    // `containing_block.inline_size` / `block_size` are written by the parent for its children and
    // read by no algorithm in `source/layout` (the used sizes always come from `percentage`), so
    // they are not part of any key. Hashing them made every box miss its cache whenever a parent
    // passed a new containing block, for an input nothing consumed.
    write_bit(out, space.containing_block.fixed_is_viewport ? 1 : 0);
    if (shrink_inline(style)) {
        write_q(out, space.containing_block.static_position.inline_offset);
    }
    if (shrink_block(style)) {
        write_q(out, space.containing_block.static_position.block_offset);
    }
    if (forwards_floats(style)) {
        write_q(out, space.bfc_origin.inline_offset);
        write_q(out, space.bfc_origin.block_offset);
    }
    // The viewport is part of a key only for the documents that can read it while laying out; see
    // `key_policy.h`. Leaving it out unconditionally made every box miss its cache on a resize.
    if (icb_in_layout_keys()) {
        write_q(out, space.icb.inline_size);
        write_q(out, space.icb.block_size);
    }
}

}  // namespace

bool ConstraintSpace::equal(const ConstraintSpace& other, const LayoutStyle& style) const {
    TokenBuffer right;
    tokenize(other, style, right);
    CompareSink left{right};
    tokenize(*this, style, left);
    return left.equal && left.index == right.size();
}

std::uint64_t ConstraintSpace::hash(const LayoutStyle& style) const {
    HashSink sink;
    tokenize(*this, style, sink);
    const std::uint64_t exclusion_hash = exclusion ? exclusion->hash() : 0;
    hash_mix(sink.hash, exclusion_hash);
    return sink.hash;
}

bool ConstraintSpace::export_tokens(const LayoutStyle& style, std::uint64_t* words, int cap,
                                    int& n) const {
    TokenBuffer buf;
    tokenize(*this, style, buf);
    n = static_cast<int>(buf.size());
    if (words == nullptr || n <= 0 || n > cap) {
        return false;
    }
    for (int i = 0; i < n; ++i) {
        words[i] = buf.at(static_cast<std::size_t>(i));
    }
    return true;
}

}  // namespace layout
}  // namespace zui
