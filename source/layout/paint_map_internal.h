#ifndef ZUI_SOURCE_LAYOUT_PAINT_MAP_INTERNAL_H
#define ZUI_SOURCE_LAYOUT_PAINT_MAP_INTERNAL_H

// Mapping state shared by paint_map.cpp and hit_test.cpp.

#include "perf/perf_budget.h"
#include "layout/paint_map.h"

#include "layout/clip_path.h"
#include "layout/paint_transform.h"

#include "layout/cache/logical_axes.h"

#include "layout/box.h"
#include "layout/cache/box_cache.h"
#include "layout/cache/overflow.h"
#include "layout/cache/sticky.h"
#include "layout/overflow_mode.h"
#include "layout/stacking.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace zui {
namespace layout {
namespace paint_map_detail {

using namespace paint_detail;

struct Frame {
    Aff2 to_world;
    float border_inline_start = 0;
    float border_block_start = 0;
    float border_inline = 0;
    float border_block = 0;
    WritingMode mode = WritingMode::HorizontalTb;
    Direction direction = Direction::Ltr;
    float scroll_inline = 0;
    float scroll_block = 0;
};

std::unordered_map<const Box*, Frame>& frames();
const Frame* containing_frame(const Box& child);
const LayoutResult* result_of(const Box& box);
bool scrolls(const Box& box);
void refresh_sticky(Box& box, float scroll_x, float scroll_y);
/// Drops the containment entries of boxes the box tree just freed. The table normally survives a
/// frame (`map_paint_dirty` reads the entries of the boxes it skipped), so a freed box's entry has
/// to go with it: a later box can be allocated at the same address.
void forget_frames(const std::unordered_set<const Box*>& gone);
/// Drops every containment entry: the box tree was thrown away whole (`BoxTree::reset`), so no
/// entry can still name a live box.
void clear_frames();


extern PaintMapSession* active_paint_map;
extern PaintMapSession* published_paint_map;

#define g_visible (active_paint_map->visible)
#define g_window (active_paint_map->window)
#define g_cull (active_paint_map->cull)
#define g_stats (active_paint_map->stats)
#define g_partial (active_paint_map->partial)
#define g_place_gen (active_paint_map->place_gen)
#define g_cull_wiped (active_paint_map->cull_wiped)

bool parent_paints_text(const Box& box);
void fill_geometry(Box& box, const LayoutResult* result);
void map_fragments(Box& box, const LayoutResult* result, const Aff2& parent_to_world,
                   float parent_border_inline_start, float parent_border_block_start,
                   float parent_border_inline, float parent_border_block, WritingMode parent_mode,
                   Direction parent_direction, float scroll_inline, float scroll_block);
void map_box(Box& box, const Aff2& parent_to_world, float parent_border_inline_start,
             float parent_border_block_start, float parent_border_inline, float parent_border_block,
             WritingMode parent_mode, Direction parent_direction, float scroll_inline,
             float scroll_block, bool descend, bool descend_all);
bool cullable(const Box& box, const LayoutResult& result);
bool descendants_published(const Box& box);
bool self_needs_remap(const Box& box);
bool subtree_reaches(const Box& child, const Frame& frame, const Rect& rect);
void shift_origin(Box& box, float dx, float dy);
void redistribute_lag(Box& box, float incoming_x, float incoming_y);
void mark_subtree_culled(Box& box);

}  // namespace paint_map_detail
}  // namespace layout
}  // namespace zui

#endif
