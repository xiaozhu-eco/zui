#ifndef ZUI_LAYOUT_CACHE_LOGICAL_AXES_H
#define ZUI_LAYOUT_CACHE_LOGICAL_AXES_H

#include "layout/cache/available_size.h"
#include "layout/cache/axis_size.h"
#include "layout/cache/containing_block.h"
#include "layout/cache/direction.h"
#include "layout/cache/edges.h"
#include "layout/cache/logical_point.h"
#include "layout/cache/logical_size.h"
#include "layout/cache/percentage_basis.h"
#include "layout/cache/writing_mode.h"
#include "layout/physical_edges.h"

#include <utility>

namespace zui {
namespace layout {

inline bool is_horizontal_wm(WritingMode mode) {
    return mode == WritingMode::HorizontalTb;
}

inline bool is_flipped_blocks_wm(WritingMode mode) {
    return mode == WritingMode::VerticalRl || mode == WritingMode::SidewaysRl;
}

inline bool axes_orthogonal(WritingMode a, WritingMode b) {
    return is_horizontal_wm(a) != is_horizontal_wm(b);
}

// CSS width/height are physical. Under horizontal-tb, width is inline; under vertical-* /
// sideways-*, width is block and height is inline.
inline LogicalSize physical_size_to_logical(WritingMode mode, float width, float height) {
    if (is_horizontal_wm(mode)) {
        return {width, height};
    }
    return {height, width};
}

inline void physical_size_to_logical(WritingMode mode, float width, float height, bool width_auto,
                                     bool height_auto, float* inline_size, float* block_size,
                                     bool* inline_auto, bool* block_auto) {
    if (is_horizontal_wm(mode)) {
        if (inline_size != nullptr) {
            *inline_size = width;
        }
        if (block_size != nullptr) {
            *block_size = height;
        }
        if (inline_auto != nullptr) {
            *inline_auto = width_auto;
        }
        if (block_auto != nullptr) {
            *block_auto = height_auto;
        }
        return;
    }
    if (inline_size != nullptr) {
        *inline_size = height;
    }
    if (block_size != nullptr) {
        *block_size = width;
    }
    if (inline_auto != nullptr) {
        *inline_auto = height_auto;
    }
    if (block_auto != nullptr) {
        *block_auto = width_auto;
    }
}

struct PhysicalSize {
    float width = 0;
    float height = 0;
};

inline PhysicalSize logical_size_to_physical(WritingMode mode, float inline_size, float block_size) {
    if (is_horizontal_wm(mode)) {
        return {inline_size, block_size};
    }
    return {block_size, inline_size};
}

inline AxisSize physical_width_basis(WritingMode mode, const PercentageBasis& percentage) {
    return is_horizontal_wm(mode) ? percentage.inline_size : percentage.block_size;
}

inline AxisSize physical_height_basis(WritingMode mode, const PercentageBasis& percentage) {
    return is_horizontal_wm(mode) ? percentage.block_size : percentage.inline_size;
}

inline AxisSize physical_width_available(WritingMode mode, const AvailableSize& available) {
    return is_horizontal_wm(mode) ? available.inline_size : available.block_size;
}

inline AxisSize physical_height_available(WritingMode mode, const AvailableSize& available) {
    return is_horizontal_wm(mode) ? available.block_size : available.inline_size;
}

// Map a physical side's used length into logical Edges for the given writing mode + direction.
inline void set_logical_edge(Edges& edges, WritingMode mode, Direction direction, float top,
                             float right, float bottom, float left) {
    if (is_horizontal_wm(mode)) {
        if (direction == Direction::Rtl) {
            edges.inline_start = right;
            edges.inline_end = left;
        } else {
            edges.inline_start = left;
            edges.inline_end = right;
        }
        edges.block_start = top;
        edges.block_end = bottom;
        return;
    }
    // vertical-rl / sideways-rl / vertical-lr / sideways-lr
    if (direction == Direction::Rtl) {
        edges.inline_start = bottom;
        edges.inline_end = top;
    } else {
        edges.inline_start = top;
        edges.inline_end = bottom;
    }
    if (is_flipped_blocks_wm(mode)) {
        edges.block_start = right;
        edges.block_end = left;
    } else {
        edges.block_start = left;
        edges.block_end = right;
    }
}

inline Edges physical_edges_to_logical(WritingMode mode, Direction direction, float top, float right,
                                       float bottom, float left) {
    Edges edges;
    set_logical_edge(edges, mode, direction, top, right, bottom, left);
    return edges;
}

inline PhysicalEdges logical_edges_to_physical(WritingMode mode, Direction direction,
                                               const Edges& logical) {
    PhysicalEdges out;
    if (is_horizontal_wm(mode)) {
        out.top = logical.block_start;
        out.bottom = logical.block_end;
        if (direction == Direction::Rtl) {
            out.right = logical.inline_start;
            out.left = logical.inline_end;
        } else {
            out.left = logical.inline_start;
            out.right = logical.inline_end;
        }
        return out;
    }
    if (direction == Direction::Rtl) {
        out.bottom = logical.inline_start;
        out.top = logical.inline_end;
    } else {
        out.top = logical.inline_start;
        out.bottom = logical.inline_end;
    }
    if (is_flipped_blocks_wm(mode)) {
        out.right = logical.block_start;
        out.left = logical.block_end;
    } else {
        out.left = logical.block_start;
        out.right = logical.block_end;
    }
    return out;
}

// Which physical overflow axis consumes space on the logical inline / block axis.
inline bool overflow_eats_inline(WritingMode mode, bool overflow_x_not_visible,
                                 bool overflow_y_not_visible) {
    if (is_horizontal_wm(mode)) {
        return overflow_y_not_visible;
    }
    return overflow_x_not_visible;
}

inline bool overflow_eats_block(WritingMode mode, bool overflow_x_not_visible,
                                bool overflow_y_not_visible) {
    if (is_horizontal_wm(mode)) {
        return overflow_x_not_visible;
    }
    return overflow_y_not_visible;
}

// Transpose available / percentage / containing-block / icb / bfc origin when the child's
// writing mode is orthogonal to the containing block's constraint axes (LAYOUT sec 3).
template <typename Space>
inline void transpose_constraint_axes(Space& space) {
    std::swap(space.available.inline_size, space.available.block_size);
    std::swap(space.percentage.inline_size, space.percentage.block_size);
    std::swap(space.containing_block.inline_size, space.containing_block.block_size);
    std::swap(space.containing_block.static_position.inline_offset,
              space.containing_block.static_position.block_offset);
    std::swap(space.icb.inline_size, space.icb.block_size);
    std::swap(space.bfc_origin.inline_offset, space.bfc_origin.block_offset);
}

template <typename Space>
inline Space space_in_box_axes(const Space& cb_space, WritingMode self_mode, Direction self_direction) {
    Space out = cb_space;
    if (axes_orthogonal(cb_space.writing_mode, self_mode)) {
        transpose_constraint_axes(out);
    }
    out.writing_mode = self_mode;
    out.direction = self_direction;
    return out;
}


// Flex row uses the container inline axis as main; column uses block. That maps to the physical
// horizontal axis for row+horizontal-tb and column+vertical-*, otherwise to physical vertical.
inline bool flex_main_is_physical_horizontal(WritingMode mode, bool row) {
    return row ? is_horizontal_wm(mode) : !is_horizontal_wm(mode);
}

struct PhysicalPoint {
    float x = 0;
    float y = 0;
};

// Physical top-left of a child border box whose logical-start corner sits at (start_inline,
// start_block) from the parent's border-box logical-start corner (LAYOUT sec 12).
inline PhysicalPoint logical_start_to_physical_tl(WritingMode mode, Direction direction,
                                                 float start_inline, float start_block,
                                                 float parent_border_inline,
                                                 float parent_border_block, float child_border_inline,
                                                 float child_border_block) {
    if (is_horizontal_wm(mode)) {
        // Keep the historical LTR embedding (inline->x, block->y). RTL absolute placement already
        // stores offsets from the left via place_axis End anchoring.
        (void)direction;
        (void)parent_border_inline;
        (void)parent_border_block;
        (void)child_border_inline;
        (void)child_border_block;
        return {start_inline, start_block};
    }
    const float parent_w = parent_border_block;
    const float parent_h = parent_border_inline;
    const float child_w = child_border_block;
    const float child_h = child_border_inline;
    float x = 0;
    float y = 0;
    if (direction == Direction::Rtl) {
        y = parent_h - start_inline - child_h;
    } else {
        y = start_inline;
    }
    if (is_flipped_blocks_wm(mode)) {
        x = parent_w - start_block - child_w;
    } else {
        x = start_block;
    }
    return {x, y};
}

// Logical point -> physical offset from the border-box top-left (no child size; for line runs).
inline PhysicalPoint logical_point_to_physical(WritingMode mode, Direction direction,
                                              float inline_offset, float block_offset,
                                              float border_box_inline, float border_box_block) {
    if (is_horizontal_wm(mode)) {
        (void)direction;
        (void)border_box_inline;
        (void)border_box_block;
        return {inline_offset, block_offset};
    }
    const float parent_w = border_box_block;
    const float parent_h = border_box_inline;
    float x = 0;
    float y = 0;
    if (direction == Direction::Rtl) {
        y = parent_h - inline_offset;
    } else {
        y = inline_offset;
    }
    if (is_flipped_blocks_wm(mode)) {
        x = parent_w - block_offset;
    } else {
        x = block_offset;
    }
    return {x, y};
}

// Map a physical viewport into logical ICB / available sizes for the root writing mode.
inline void viewport_to_logical(WritingMode mode, float width, float height, LogicalSize* icb,
                                AvailableSize* available) {
    if (icb != nullptr) {
        *icb = physical_size_to_logical(mode, width, height);
    }
    if (available != nullptr) {
        if (is_horizontal_wm(mode)) {
            available->inline_size = {true, width};
            available->block_size = {true, height};
        } else {
            available->inline_size = {true, height};
            available->block_size = {true, width};
        }
    }
}

}  // namespace layout
}  // namespace zui

#endif

