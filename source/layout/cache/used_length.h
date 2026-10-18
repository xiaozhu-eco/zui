#ifndef ZUI_LAYOUT_CACHE_USED_LENGTH_H
#define ZUI_LAYOUT_CACHE_USED_LENGTH_H

#include "css/values.h"
#include "html/document.h"
#include "layout/cache/axis_size.h"
#include "layout/layout_style.h"

namespace zui {
namespace layout {

/// Prefer a live width/height transition overlay over the interned style used value.
inline void apply_size_overlays(const html::Node* node, float& width, bool& width_auto, float& height,
                                bool& height_auto) {
    if (node == nullptr) {
        return;
    }
    if (node->used.flags & html::VisualUsed::kWidth) {
        width = node->used.width;
        width_auto = false;
    }
    if (node->used.flags & html::VisualUsed::kHeight) {
        height = node->used.height;
        height_auto = false;
    }
}

inline float edge_with_overlay(const html::Node* node, std::uint32_t flag, float overlay_px,
                               float style_px) {
    if (node != nullptr && (node->used.flags & flag) != 0) {
        return overlay_px;
    }
    return style_px;
}

// Percentage of an indefinite basis becomes auto for a main size, and 0 for an edge.
inline float used_main(bool is_auto, bool is_percent, float value, const AxisSize& basis, bool* as_auto) {
    if (is_auto || (is_percent && !basis.definite)) {
        if (as_auto != nullptr) {
            *as_auto = true;
        }
        return 0;
    }
    if (as_auto != nullptr) {
        *as_auto = false;
    }
    if (is_percent) {
        return basis.px * value / 100.f;
    }
    return value;
}

// Margin and padding percentages resolve against the containing block's inline size.
inline float used_edge(float value, bool is_percent, const AxisSize& inline_basis) {
    if (!is_percent) {
        return value;
    }
    if (!inline_basis.definite) {
        return 0;
    }
    return inline_basis.px * value / 100.f;
}

/// One `<length-percentage>` property as the interned style holds it.
struct StyleLength {
    bool is_auto = true;
    bool is_percent = false;
    float value = 0;
};

/// The fields of @p prop. Every property the used-value pass reads goes through here, so the
/// property and the fields it reads cannot drift apart.
inline StyleLength style_length(const LayoutStyle& style, LengthProperty prop) {
    switch (prop) {
        case LengthProperty::PaddingTop:
            return {false, style.padding_top_percent, style.padding_top};
        case LengthProperty::PaddingRight:
            return {false, style.padding_right_percent, style.padding_right};
        case LengthProperty::PaddingBottom:
            return {false, style.padding_bottom_percent, style.padding_bottom};
        case LengthProperty::PaddingLeft:
            return {false, style.padding_left_percent, style.padding_left};
        case LengthProperty::MarginTop:
            return {style.margin_top_auto, style.margin_top_percent, style.margin_top};
        case LengthProperty::MarginRight:
            return {style.margin_right_auto, style.margin_right_percent, style.margin_right};
        case LengthProperty::MarginBottom:
            return {style.margin_bottom_auto, style.margin_bottom_percent, style.margin_bottom};
        case LengthProperty::MarginLeft:
            return {style.margin_left_auto, style.margin_left_percent, style.margin_left};
        case LengthProperty::MinWidth:
            return {style.min_width_auto, style.min_width_percent, style.min_width_px};
        case LengthProperty::MinHeight:
            return {style.min_height_auto, style.min_height_percent, style.min_height_px};
        case LengthProperty::MaxWidth:
            return {style.max_width_none, style.max_width_percent, style.max_width_px};
        case LengthProperty::MaxHeight:
            return {style.max_height_none, style.max_height_percent, style.max_height_px};
        case LengthProperty::Top:
            return {style.top_auto, style.top_percent, style.top_px};
        case LengthProperty::Right:
            return {style.right_auto, style.right_percent, style.right_px};
        case LengthProperty::Bottom:
            return {style.bottom_auto, style.bottom_percent, style.bottom_px};
        case LengthProperty::Left:
            return {style.left_auto, style.left_percent, style.left_px};
        case LengthProperty::ColumnGap:
            return {false, style.column_gap_percent, style.column_gap};
        case LengthProperty::RowGap:
            return {false, style.row_gap_percent, style.row_gap};
        case LengthProperty::FlexBasis:
            return {style.flex_basis_auto, style.flex_basis_percent, style.flex_basis_px};
        case LengthProperty::Count:
            break;
    }
    return {};
}

/// The declaration text the interner kept for @p prop, or nullptr when the property did not need
/// the containing block. Almost every style has none.
inline const std::string* kept_math(const LayoutStyle& style, LengthProperty prop) {
    for (const MathLength& kept : style.math_lengths) {
        if (kept.prop == prop) {
            return &kept.text;
        }
    }
    return nullptr;
}

/// A kept declaration resolved against @p basis. Returns false when it cannot be a length even
/// then, which leaves the property at its initial value rather than at zero.
inline bool resolve_kept(const std::string& text, const AxisSize& basis, float* out) {
    const css::ResolvedLength resolved = css::resolve_length_percent(text, basis.px);
    if (!resolved.valid || resolved.is_percent) {
        return false;
    }
    *out = resolved.px;
    return true;
}

/// The used value of one `<length-percentage>` edge - padding or margin. A percentage of an
/// indefinite basis is 0, and a kept declaration needs that basis just as much (CSS Sizing 5.2);
/// padding and margin are the two families whose percentages always use the *inline* basis, top
/// and bottom included (CSS Box 4 section 4).
inline float used_edge(const LayoutStyle& style, LengthProperty prop,
                       const AxisSize& inline_basis) {
    const StyleLength length = style_length(style, prop);
    const std::string* kept = kept_math(style, prop);
    if (kept != nullptr) {
        float resolved = 0;
        if (!inline_basis.definite || !resolve_kept(*kept, inline_basis, &resolved)) {
            return 0;
        }
        return resolved;
    }
    if (length.is_auto) {
        return 0;
    }
    return used_edge(length.value, length.is_percent, inline_basis);
}

/// The used value of one `<length-percentage>` main size - `min-width` / `max-height` and friends.
/// A percentage of an indefinite basis behaves as the property's initial value, which `as_auto`
/// reports; a kept declaration is resolved against @p basis the same way.
inline float used_main(const LayoutStyle& style, LengthProperty prop, const AxisSize& basis,
                       bool* as_auto) {
    const StyleLength length = style_length(style, prop);
    const std::string* kept = kept_math(style, prop);
    // A kept declaration is a length that only lacked the basis, so it outranks the property's
    // initial value: the kept text is what says `auto` was not written, not the style flag.
    if (kept != nullptr) {
        if (!basis.definite) {
            if (as_auto != nullptr) {
                *as_auto = true;
            }
            return 0;
        }
        float resolved = 0;
        if (!resolve_kept(*kept, basis, &resolved)) {
            // The declaration cannot be a length even with the basis: the initial value stands.
            if (as_auto != nullptr) {
                *as_auto = true;
            }
            return 0;
        }
        if (as_auto != nullptr) {
            *as_auto = false;
        }
        return resolved;
    }
    if (length.is_auto || (length.is_percent && !basis.definite)) {
        if (as_auto != nullptr) {
            *as_auto = true;
        }
        return 0;
    }
    return used_main(length.is_auto, length.is_percent, length.value, basis, as_auto);
}

}  // namespace layout
}  // namespace zui

#endif
