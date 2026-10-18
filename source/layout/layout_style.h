#ifndef ZUI_LAYOUT_LAYOUT_STYLE_H
#define ZUI_LAYOUT_LAYOUT_STYLE_H

#include "layout/cache/direction.h"
#include "layout/cache/writing_mode.h"
#include "layout/display.h"
#include "layout/overflow_mode.h"

#include <cstdint>
#include <string>
#include <vector>

namespace zui {
namespace layout {

enum class ClearSide { None, Left, Right, Both };
enum class WhiteSpace { Normal, Nowrap, Pre, PreWrap, PreLine, BreakSpaces };
/// CSS Text 3 `word-break`.
enum class WordBreak { Normal, BreakAll, KeepAll };
/// CSS Text 3 `overflow-wrap` (`word-wrap` alias).
enum class OverflowWrap { Normal, BreakWord, Anywhere };
enum class VerticalAlign { Baseline, Sub, Super, Top, Middle, Bottom, TextTop, TextBottom, Length };
enum class LineHeightKind { Normal, Length, Percent, Number };
enum class TextAlign { Start, End, Left, Right, Center, Justify };
/// CSS Text 3 section 2.1. The full-width / full-size-kana forms are not implemented and fall back
/// to `none`.
enum class TextTransform { None, Uppercase, Lowercase, Capitalize };
/// CSS Overflow 3 / UI 4: `text-overflow`. Only `clip` and `ellipsis` in this cut.
enum class TextOverflow { Clip, Ellipsis };
enum class ScrollbarGutter { Auto, Stable, BothEdges };

/// CSS Scrollbars: which bar a scroll container shows. `None` reserves no gutter. `Thin` is
/// Chrome's classic thin gutter, 10px, measured with the pinned 15px bar (overlay disabled).
enum class ScrollbarWidth { Auto, Thin, None };
enum class FlexDirection { Row, RowReverse, Column, ColumnReverse };
enum class FlexWrap { NoWrap, Wrap, WrapReverse };
/// `baseline` / `first baseline` share the line's first baseline; `last baseline` shares the last
/// (CSS Box Alignment section 9). Flex resolves both in the cross-axis place walk; grid resolves
/// first and last baseline on the block axis in `place` (inline-axis first → start, last → end
/// under horizontal-tb, matching Chrome).
enum class FlexAlign { Auto, Stretch, Start, End, Center, Baseline, LastBaseline };
// Content distribution. `normal` is not stored as its own value: CSS gives it a different meaning
// per formatting context (flex-start in Flexbox, stretch in Grid), so the default here is Stretch
// and the Flexbox path reads anything it does not know as its start.
enum class FlexPack { Start, End, Center, SpaceBetween, SpaceAround, SpaceEvenly, Stretch };
enum class ContainerType { Normal, InlineSize, Size };
/// CSS Fragmentation 3 / CSS Multi-column: `break-before` / `break-after` subset.
enum class BreakBetween : std::uint8_t { Auto, Avoid, Always, Column };
/// CSS Fragmentation 3: `break-inside` subset.
enum class BreakInside : std::uint8_t { Auto, Avoid };

/// CSS border-style (Backgrounds 3 / CSS 2.1 §8.5.3). Used by collapsed-border conflict
/// resolution (CSS 2.1 §17.6.2.1): `hidden` suppresses the edge; equal widths rank
/// double > solid > dashed > dotted > ridge > outset > groove > inset; `none` loses.
enum class BorderLineStyle : std::uint8_t {
    None,
    Hidden,
    Dotted,
    Dashed,
    Solid,
    Double,
    Groove,
    Ridge,
    Inset,
    Outset
};

/// `min-content` / `max-content` / `fit-content` on a sizing property (CSS Sizing section 5).
enum class IntrinsicSize { None, MinContent, MaxContent, FitContent };

/// The `<length-percentage>` properties that can need the containing block, because their
/// declaration's math mixes `%` with lengths and the style system cannot resolve that on its own
/// (CSS Values section 10). One entry per property that `intern_style` may keep the text of, and
/// the key `used_edge` / `used_main` / the inset and gap readers look it up by.
enum class LengthProperty : std::uint8_t {
    PaddingTop,
    PaddingRight,
    PaddingBottom,
    PaddingLeft,
    MarginTop,
    MarginRight,
    MarginBottom,
    MarginLeft,
    MinWidth,
    MinHeight,
    MaxWidth,
    MaxHeight,
    Top,
    Right,
    Bottom,
    Left,
    ColumnGap,
    RowGap,
    FlexBasis,
    Count
};

/// A declaration the interner could not evaluate: the text of a math function that mentions `%`,
/// with its relative units already baked into px (see `bake_relative_units`), so the used-value
/// pass only has to supply the percentage basis.
struct MathLength {
    LengthProperty prop = LengthProperty::Count;
    std::string text;
};

// `AutoFill` / `AutoFit` are not tracks: they are the marker a `repeat(auto-fill|auto-fit, ...)`
// leaves in the template, with the repeated list in `repeat`. The layout expands them once it
// knows the container's size, because how many repetitions fit depends on that size.
enum class TrackKind {
    Auto,
    Length,
    Fr,
    MinContent,
    MaxContent,
    FitContent,
    MinMax,
    AutoFill,
    AutoFit,
    // `grid-template-columns/rows: subgrid`. Not a track size: the axis adopts the parent
    // grid's spanned tracks. One sentinel in the template vector, so the style table keeps
    // hashing `kind` and does not need a separate flag.
    Subgrid
};

// One grid track sizing function. Lengths are px or percent (of the grid content box).
struct TrackSize {
    TrackKind kind = TrackKind::Auto;
    // AutoFill / AutoFit only: the track list the repetition stands for.
    std::vector<TrackSize> repeat;
    // A `repeat(auto-fill|auto-fit, ...)` marker only: the line names of the repeated list, one
    // entry per line (tracks + 1). They have to be expanded next to the tracks once the repetition
    // count is known, otherwise `[name]` groups inside an auto repetition would be lost.
    std::vector<std::vector<std::string>> repeat_names;
    bool length_percent = false;
    float length_px = 0;
    float fr = 0;
    // MinMax only: min and max may be Auto / Length / MinContent / MaxContent / Fr.
    TrackKind min_kind = TrackKind::Auto;
    bool min_percent = false;
    float min_px = 0;
    float min_fr = 0;
    TrackKind max_kind = TrackKind::Auto;
    bool max_percent = false;
    float max_px = 0;
    float max_fr = 0;
};

struct GridLine {
    bool is_auto = true;
    // `span <n|name>`: the reference is a span, not a line.
    bool is_span = false;
    int line = 0;
    int span = 1;
    // Non-empty when the line is referenced by name (`grid-column: header` or `span header`).
    std::string name;
};

struct LayoutStyle {
    DisplayOuter outer = DisplayOuter::Block;
    DisplayInner inner = DisplayInner::Flow;
    bool position_relative = false;
    bool position_absolute = false;
    bool position_fixed = false;
    bool floating = false;
    bool float_right = false;
    /// CSS Lists `list-style-position: outside` (initial). Inside markers join the principal
    /// box IFC; outside markers hang with their inline-end at the content-box start and do not
    /// contribute to intrinsic size or scrollable overflow.
    bool list_style_outside = true;
    ClearSide clear_side = ClearSide::None;
    float width_px = -1;
    float height_px = -1;
    bool width_auto = true;
    bool height_auto = true;
    // `width: min-content | max-content | fit-content`: the used size is the box's own intrinsic
    // size rather than the available space, so `width_auto` stays true and the layout resolves it.
    IntrinsicSize width_intrinsic = IntrinsicSize::None;
    IntrinsicSize height_intrinsic = IntrinsicSize::None;
    // When set, width_px / height_px is a percentage of the constraint's percentage basis.
    bool width_percent = false;
    bool height_percent = false;
    float margin_top = 0;
    float margin_right = 0;
    float margin_bottom = 0;
    float margin_left = 0;
    bool margin_top_percent = false;
    bool margin_right_percent = false;
    bool margin_bottom_percent = false;
    bool margin_left_percent = false;
    bool margin_top_auto = false;
    bool margin_right_auto = false;
    bool margin_bottom_auto = false;
    bool margin_left_auto = false;
    bool min_width_auto = true;
    bool min_width_percent = false;
    float min_width_px = 0;
    bool min_height_auto = true;
    bool min_height_percent = false;
    float min_height_px = 0;
    bool max_width_none = true;
    bool max_width_percent = false;
    float max_width_px = 0;
    bool max_height_none = true;
    bool max_height_percent = false;
    float max_height_px = 0;
    bool flow_root = false;
    bool border_box = false;
    WhiteSpace white_space = WhiteSpace::Normal;
    WordBreak word_break = WordBreak::Normal;
    OverflowWrap overflow_wrap = OverflowWrap::Normal;
    VerticalAlign vertical_align = VerticalAlign::Baseline;
    bool vertical_align_percent = false;
    float vertical_align_px = 0;
    LineHeightKind line_height_kind = LineHeightKind::Normal;
    float line_height_value = 0;
    TextAlign text_align = TextAlign::Start;
    TextTransform text_transform = TextTransform::None;
    TextOverflow text_overflow = TextOverflow::Clip;
    bool text_indent_percent = false;
    float text_indent_px = 0;
    float letter_spacing = 0;
    /// Extra advance on word-separator characters (CSS Text 3 §8.2); `normal` is 0.
    float word_spacing = 0;
    /// width / height, or 0 for `auto` / absent. CSS Sizing 4 section 5.1.
    float aspect_ratio = 0;
    ScrollbarGutter scrollbar_gutter = ScrollbarGutter::Auto;
    ScrollbarWidth scrollbar_width = ScrollbarWidth::Auto;
    float padding_top = 0;
    float padding_right = 0;
    float padding_bottom = 0;
    float padding_left = 0;
    bool padding_top_percent = false;
    bool padding_right_percent = false;
    bool padding_bottom_percent = false;
    bool padding_left_percent = false;
    /// The declarations of this style whose used value still needs the containing block. Empty for
    /// almost every style; see `MathLength`.
    std::vector<MathLength> math_lengths;
    float border_top = 0;
    float border_right = 0;
    float border_bottom = 0;
    float border_left = 0;
    BorderLineStyle border_top_style = BorderLineStyle::None;
    BorderLineStyle border_right_style = BorderLineStyle::None;
    BorderLineStyle border_bottom_style = BorderLineStyle::None;
    BorderLineStyle border_left_style = BorderLineStyle::None;
    /// CSS UI 4 outline: a ring drawn outside the border box, `outline_offset` away from it, of
    /// `outline_width`. Widths follow the `thin`/`medium`/`thick` keywords (1/3/5px) and the offset
    /// may be negative, which puts the ring inside the border box.
    BorderLineStyle outline_style = BorderLineStyle::None;
    float outline_width = 0;
    float outline_offset = 0;
    /// Specified border-radius per corner (CSS Backgrounds 3). Existing fields are the horizontal
    /// radii; `_y` is the vertical axis. Percentages stay as the number from the declaration
    /// (50 for `50%`) until paint resolves them against the border box (x→width, y→height).
    float border_top_left_radius = 0;
    float border_top_right_radius = 0;
    float border_bottom_right_radius = 0;
    float border_bottom_left_radius = 0;
    bool border_top_left_radius_percent = false;
    bool border_top_right_radius_percent = false;
    bool border_bottom_right_radius_percent = false;
    bool border_bottom_left_radius_percent = false;
    float border_top_left_radius_y = 0;
    float border_top_right_radius_y = 0;
    float border_bottom_right_radius_y = 0;
    float border_bottom_left_radius_y = 0;
    bool border_top_left_radius_y_percent = false;
    bool border_top_right_radius_y_percent = false;
    bool border_bottom_right_radius_y_percent = false;
    bool border_bottom_left_radius_y_percent = false;
    /// Max of the eight specified corner numbers; used for cheap "any radius?" checks before the
    /// used-value pass. Percent corners contribute their raw percentage number here too.
    float border_radius = 0;
    OverflowMode overflow_x = OverflowMode::Visible;
    OverflowMode overflow_y = OverflowMode::Visible;
    float font_size = 16;
    // CSS font-weight numeric; 400 = normal, 700 = bold.
    int font_weight = 400;
    bool font_italic = false;
    // CSS `font-kerning: none`. The initial `auto` and the explicit `normal` both leave the face's
    // `kern` feature on, which is what Chrome does for every script and language: `lang` is not a
    // kerning switch (a zh-CN document kerns Latin runs exactly like an en one).
    bool font_kerning_none = false;
    std::string font_family;
    std::string lang;
    WritingMode writing_mode = WritingMode::HorizontalTb;
    Direction direction = Direction::Ltr;
    bool cb_for_absolute = false;
    bool cb_for_fixed = false;
    // `contain: paint` (also implied by `strict` / `content`). It clips this box's descendants to
    // the padding box and makes the box a containing block for its out-of-flow descendants; the
    // layout records it on the result as `Overflow::clipped_by_paint_contain`. Also turned on by
    // `content-visibility: auto` / `hidden`, which change the used value of `contain` (CSS
    // Containment section 4) without touching the computed one.
    bool contain_paint = false;
    // `contain: layout` (also implied by `strict` / `content`). It does not change geometry here,
    // but it makes the box a stacking context (LAYOUT.md section 11) and, like `contain: paint`,
    // a containing block for out-of-flow descendants (CSS Containment section 3.3 effect 4).
    bool contain_layout = false;
    bool contain_inline_size = false;
    bool contain_block_size = false;
    bool content_visibility_hidden = false;
    bool content_visibility_auto = false;
    // Negative means the axis has no contain-intrinsic size.
    float contain_intrinsic_inline = -1;
    float contain_intrinsic_block = -1;
    bool visibility_collapse = false;
    bool table_row = false;
    bool table_column = false;
    // CSS 2.1 section 17: the part this box plays, the cell's span, and the table's own knobs.
    TablePart table_part = TablePart::None;
    int table_colspan = 1;
    int table_rowspan = 1;
    /// `span` on a `<col>` / `<colgroup>`: how many columns the box covers (CSS 2.1 section 17.3).
    int table_span = 1;
    bool border_collapse = false;
    float border_spacing_inline = 0;
    float border_spacing_block = 0;
    bool table_fixed = false;
    bool caption_below = false;
    bool position_sticky = false;
    bool top_auto = true;
    bool top_percent = false;
    float top_px = 0;
    bool left_auto = true;
    bool left_percent = false;
    float left_px = 0;
    bool right_auto = true;
    bool right_percent = false;
    float right_px = 0;
    bool bottom_auto = true;
    bool bottom_percent = false;
    float bottom_px = 0;
    FlexDirection flex_direction = FlexDirection::Row;
    FlexWrap flex_wrap = FlexWrap::NoWrap;
    float flex_grow = 0;
    float flex_shrink = 1;
    bool flex_basis_auto = true;
    bool flex_basis_percent = false;
    bool flex_basis_content = false;
    float flex_basis_px = 0;
    float column_gap = 0;
    float row_gap = 0;
    bool column_gap_percent = false;
    // CSS Multi-column Layout section 3: `column-count: auto` is 0 and `column-width: auto` is the
    // flag. A container is multi-column when either of them is not auto.
    int column_count = 0;
    bool column_width_auto = true;
    float column_width = 0;
    // `column-fill: auto` fills the columns in order instead of balancing them.
    bool column_fill_auto = false;
    // CSS Multi-column: `column-span: all` takes the box out of the column flow and spans the
    // full multicol content width (a spanner). Nested spanners beyond a direct child are deferred.
    bool column_span_all = false;
    bool row_gap_percent = false;
    int flex_order = 0;
    FlexAlign align_items = FlexAlign::Stretch;
    FlexAlign align_self = FlexAlign::Auto;
    FlexPack justify_content = FlexPack::Stretch;
    FlexPack align_content = FlexPack::Stretch;
    FlexAlign justify_items = FlexAlign::Stretch;
    // justify-self: auto defers to the container's justify-items.
    FlexAlign justify_self = FlexAlign::Auto;
    std::vector<TrackSize> grid_template_columns;
    std::vector<TrackSize> grid_template_rows;
    std::vector<TrackSize> grid_auto_columns;
    std::vector<TrackSize> grid_auto_rows;
    GridLine grid_column_start;
    GridLine grid_column_end;
    GridLine grid_row_start;
    GridLine grid_row_end;
    // One entry per line of the explicit grid (tracks + 1), in order; a line may carry several
    // names, straight from the `[name a b]` groups of the corresponding `grid-template-*`.
    std::vector<std::vector<std::string>> grid_column_line_names;
    std::vector<std::vector<std::string>> grid_row_line_names;
    // `grid-template-areas` as a row-major grid of area names; an empty cell is a `null` cell
    // (`.`). The named areas also contribute `<name>-start` / `<name>-end` line names.
    std::vector<std::vector<std::string>> grid_template_areas;
    bool grid_auto_flow_column = false;
    bool grid_auto_flow_dense = false;
    ContainerType container_type = ContainerType::Normal;
    /// CSS Fragmentation 3 / Multi-column: forced or avoided breaks around / inside this box.
    BreakBetween break_before = BreakBetween::Auto;
    BreakBetween break_after = BreakBetween::Auto;
    BreakInside break_inside = BreakInside::Auto;
    /// CSS Fragmentation 3 / CSS 2.1: minimum lines left at the bottom / top of a fragment.
    int orphans = 2;
    int widows = 2;
};

}  // namespace layout
}  // namespace zui

#endif
