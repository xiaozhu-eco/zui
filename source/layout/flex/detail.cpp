#include "layout/flex/detail.h"

#include "layout/cache/used_length.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace zui {
namespace layout {
namespace flex_detail {
const LayoutStyle& style_of(const Box& box) {
    static const LayoutStyle kEmpty;
    return box.style_ptr != nullptr ? box.style() : kEmpty;
}

bool row_direction(FlexDirection direction) {
    return direction == FlexDirection::Row || direction == FlexDirection::RowReverse;
}

bool reverse_main(FlexDirection direction) {
    return direction == FlexDirection::RowReverse || direction == FlexDirection::ColumnReverse;
}

float length_or_percent(float value, bool percent, float basis, bool basis_known) {
    if (!percent) {
        return value;
    }
    if (!basis_known) {
        return 0;
    }
    return basis * value / 100.f;
}

float border_from_specified(float specified, bool border_box, float chrome) {
    if (border_box) {
        return std::max(0.f, specified);
    }
    return std::max(0.f, specified + chrome);
}


float outer_main(const Item& item, float border) {
    return item.margin_main_start + border + item.margin_main_end;
}

namespace {

float sole_leading(FlexPack pack, float free) {
    switch (pack) {
        case FlexPack::End:
            return free;
        case FlexPack::Center:
        case FlexPack::SpaceAround:
        case FlexPack::SpaceEvenly:
            return free * 0.5f;
        case FlexPack::SpaceBetween:
        case FlexPack::Stretch:
        case FlexPack::Start:
            return 0.f;
    }
    return 0.f;
}

}  // namespace

LogicalPoint sole_abspos_static(const LayoutStyle& container, const LayoutStyle& child,
                                float inner_inline, bool inline_known, float inner_block,
                                bool block_known) {
    // Horizontal-tb: the main axis of a row is physical width. Vertical writing-mode still
    // reads physical margin-left / margin-top, matching the flex item measure.
    const bool row = row_direction(container.flex_direction);
    const bool main_known = row ? inline_known : block_known;
    const bool cross_known = row ? block_known : inline_known;
    const float inner_main = row ? inner_inline : inner_block;
    const float inner_cross = row ? inner_block : inner_inline;
    const AxisSize inline_basis{inline_known, inline_known ? inner_inline : 0.f};
    const AxisSize block_basis{block_known, block_known ? inner_block : 0.f};
    auto margin_of = [&](LengthProperty prop, bool is_auto) {
        return is_auto ? 0.f : used_edge(child, prop, inline_basis);
    };
    const float margin_left = margin_of(LengthProperty::MarginLeft, child.margin_left_auto);
    const float margin_right = margin_of(LengthProperty::MarginRight, child.margin_right_auto);
    const float margin_top = margin_of(LengthProperty::MarginTop, child.margin_top_auto);
    const float margin_bottom = margin_of(LengthProperty::MarginBottom, child.margin_bottom_auto);
    // A percentage size is definite only against a definite container. An auto size is not
    // aligned: end and center need the border box to know where the margin edge lands.
    auto definite_border = [&](bool horizontal) -> std::pair<bool, float> {
        const bool is_auto = horizontal ? child.width_auto : child.height_auto;
        const bool percent = horizontal ? child.width_percent : child.height_percent;
        const bool basis_known = horizontal ? inline_known : block_known;
        if (is_auto || (percent && !basis_known)) {
            return {false, 0.f};
        }
        const AxisSize basis = horizontal ? inline_basis : block_basis;
        bool as_auto = false;
        const float specified =
            horizontal ? used_main(child.width_auto, child.width_percent, child.width_px, basis,
                                   &as_auto)
                       : used_main(child.height_auto, child.height_percent, child.height_px, basis,
                                   &as_auto);
        if (as_auto) {
            return {false, 0.f};
        }
        const float pad = horizontal ? margin_of(LengthProperty::PaddingLeft, false) +
                                           margin_of(LengthProperty::PaddingRight, false)
                                     : margin_of(LengthProperty::PaddingTop, false) +
                                           margin_of(LengthProperty::PaddingBottom, false);
        const float border = horizontal ? child.border_left + child.border_right
                                        : child.border_top + child.border_bottom;
        return {true, border_from_specified(specified, child.border_box, pad + border)};
    };
    FlexPack main_pack = container.justify_content;
    if (main_pack == FlexPack::Stretch || main_pack == FlexPack::SpaceBetween) {
        main_pack = FlexPack::Start;
    }
    if (reverse_main(container.flex_direction)) {
        if (main_pack == FlexPack::Start) {
            main_pack = FlexPack::End;
        } else if (main_pack == FlexPack::End) {
            main_pack = FlexPack::Start;
        }
    }
    const auto main_size = definite_border(row);
    const auto cross_size = definite_border(!row);
    const float main_start = row ? margin_left : margin_top;
    const float main_end = row ? margin_right : margin_bottom;
    const float cross_start = row ? margin_top : margin_left;
    const float cross_end = row ? margin_bottom : margin_right;
    float main_pos = main_start;
    if (main_known && main_size.first) {
        const float free = inner_main - main_start - main_size.second - main_end;
        main_pos = sole_leading(main_pack, free) + main_start;
    }
    float cross_pos = cross_start;
    const FlexAlign cross_align = container.align_items;
    if (cross_known && cross_size.first &&
        (cross_align == FlexAlign::End || cross_align == FlexAlign::Center)) {
        const float free = inner_cross - cross_start - cross_size.second - cross_end;
        const FlexPack pack = cross_align == FlexAlign::End ? FlexPack::End : FlexPack::Center;
        cross_pos = sole_leading(pack, free) + cross_start;
    }
    LogicalPoint local;
    if (row) {
        local.inline_offset = main_pos;
        local.block_offset = cross_pos;
    } else {
        local.block_offset = main_pos;
        local.inline_offset = cross_pos;
    }
    return local;
}


Pack pack_of(FlexPack pack, float free, int count) {
    Pack out;
    if (count <= 0 || free <= 0.f) {
        return out;
    }
    switch (pack) {
        case FlexPack::End:
            out.leading = free;
            break;
        case FlexPack::Center:
            out.leading = free * 0.5f;
            break;
        case FlexPack::SpaceBetween:
            out.between = count > 1 ? free / static_cast<float>(count - 1) : 0.f;
            break;
        case FlexPack::SpaceAround: {
            const float unit = free / (2.f * static_cast<float>(count));
            out.leading = unit;
            out.between = 2.f * unit;
            break;
        }
        case FlexPack::SpaceEvenly: {
            const float unit = free / static_cast<float>(count + 1);
            out.leading = unit;
            out.between = unit;
            break;
        }
        case FlexPack::Stretch:
        case FlexPack::Start:
        default:
            break;
    }
    return out;
}

void distribute(std::vector<Item*>& line, float inner, float gap) {
    const int count = static_cast<int>(line.size());
    if (count == 0) {
        return;
    }
    for (Item* item : line) {
        item->frozen = false;
        item->target = item->hypo;
    }
    const float gaps = gap * static_cast<float>(std::max(0, count - 1));
    for (int iter = 0; iter < count + 2; ++iter) {
        float used = gaps;
        float grow_sum = 0;
        float shrink_scaled = 0;
        int unfrozen = 0;
        for (Item* item : line) {
            if (item->frozen) {
                used += outer_main(*item, item->target);
                continue;
            }
            used += outer_main(*item, item->hypo);
            ++unfrozen;
            grow_sum += std::max(0.f, item->grow);
            shrink_scaled += std::max(0.f, item->shrink) * std::max(0.f, item->base);
        }
        if (unfrozen == 0) {
            return;
        }
        const float free = inner - used;
        bool violation = false;
        if (free > 0.5f && grow_sum > 0.f) {
            for (Item* item : line) {
                if (item->frozen) {
                    continue;
                }
                const float next = item->hypo + free * std::max(0.f, item->grow) / grow_sum;
                if (item->has_max && next > item->max_size + 0.01f) {
                    item->target = item->max_size;
                    if (item->target < item->min_size) {
                        item->target = item->min_size;
                    }
                    item->frozen = true;
                    violation = true;
                } else {
                    item->target = std::max(item->min_size, next);
                    if (item->target < next - 0.01f) {
                        item->frozen = true;
                        violation = true;
                    }
                }
            }
        } else if (free < -0.5f && shrink_scaled > 0.f) {
            for (Item* item : line) {
                if (item->frozen) {
                    continue;
                }
                const float portion = std::max(0.f, item->shrink) * std::max(0.f, item->base);
                const float next =
                    portion > 0.f ? item->hypo + free * portion / shrink_scaled : item->hypo;
                if (next < item->min_size - 0.01f) {
                    item->target = item->min_size;
                    item->frozen = true;
                    violation = true;
                } else {
                    item->target = next;
                    if (item->has_max && item->target > item->max_size) {
                        item->target = item->max_size;
                        // A specified minimum larger than the maximum still wins. Leaving
                        // the target at max makes the item's own layout report min and the
                        // rest of the line keeps the space max freed.
                        if (item->target < item->min_size) {
                            item->target = item->min_size;
                        }
                        item->frozen = true;
                        violation = true;
                    }
                }
            }
        } else {
            for (Item* item : line) {
                if (!item->frozen) {
                    item->target = item->hypo;
                    item->frozen = true;
                }
            }
            return;
        }
        if (!violation) {
            for (Item* item : line) {
                item->frozen = true;
            }
            return;
        }
    }
}

FlexAlign used_align(const LayoutStyle& item, const LayoutStyle& container) {
    return item.align_self == FlexAlign::Auto ? container.align_items : item.align_self;
}

/// How many auto margins this line would have to share free space with (CSS Flexbox 9.5 step 12).
int auto_main_margins(const std::vector<Item*>& line) {
    int count = 0;
    for (const Item* item : line) {
        count += item->main_start_auto ? 1 : 0;
        count += item->main_end_auto ? 1 : 0;
    }
    return count;
}
}  // namespace flex_detail
}  // namespace layout
}  // namespace zui