#ifndef ZUI_SOURCE_LAYOUT_GRID_DETAIL_H
#define ZUI_SOURCE_LAYOUT_GRID_DETAIL_H

#include "layout/box.h"
#include "layout/cache/box_cache.h"
#include "layout/cache/break_token.h"
#include "layout/cache/fragment_cut.h"
#include "layout/cache/layout_context.h"
#include "layout/cache/logical_axes.h"
#include "layout/cache/sticky.h"
#include "layout/cache/used_length.h"
#include "layout/overflow_mode.h"
#include "layout/platform_metrics.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <vector>


namespace zui {
namespace layout {
namespace grid_detail {

inline constexpr float kInf = std::numeric_limits<float>::infinity();

const LayoutStyle& style_of(const Box& box);

float length_or_percent(float value, bool is_percent, float basis, bool basis_known);

// A specified size is the content box unless box-sizing says otherwise.
float border_from_specified(float specified, bool border_box, float chrome);

float content_from_specified(float specified, bool border_box, float chrome);

Edges logical_margin(WritingMode mode, Direction direction, const LayoutStyle& style,
                     const AxisSize& inline_basis);

Edges padding_edges(WritingMode mode, Direction direction, const LayoutStyle& style,
                    const AxisSize& inline_basis);

Edges border_edges(WritingMode mode, Direction direction, const LayoutStyle& style);

bool scrolls(const LayoutStyle& style);

bool out_of_flow(const Box& box);



// ---- Track sizing functions ---------------------------------------------------------------

// The two sizing functions of one track, after minmax() and the single-function shorthands have
// been normalized. fr only ever appears on the max side (CSS Grid section 7.2.1).
struct TrackSizing {
    TrackKind min_kind = TrackKind::Auto;
    bool min_percent = false;
    float min_px = 0;
    TrackKind max_kind = TrackKind::Auto;
    bool max_percent = false;
    float max_px = 0;
    float fr = 1;
    // fit-content(<length>): an upper bound on the growth limit (section 7.2.3).
    bool has_cap = false;
    bool cap_percent = false;
    float cap_px = 0;
};

TrackSizing sizing_of(const TrackSize& track);



// One track while the sizing algorithm runs. Base size and growth limit stay in solve locals.
struct Track {
    float base = 0;
    float limit = kInf;
    bool is_fr = false;
    float fr = 1;
    // The base follows the min-content contribution of the items that span this track.
    bool min_intrinsic = false;
    // The growth limit follows the max-content contribution.
    bool max_intrinsic = false;
    bool max_from_min_content = false;
    // The max sizing function is auto: these are the tracks that align-content / justify-content
    // at stretch share their leftover space with (CSS Grid section 12.8).
    bool auto_max = false;
    // fit-content(): the growth limit never goes above this.
    float limit_cap = kInf;
};

Track make_track(const TrackSizing& sizing, float basis, bool basis_known);



// ---- Items --------------------------------------------------------------------------------

struct GridItem {
    Box* box = nullptr;
    int col = 0;
    int col_span = 1;
    int row = 0;
    int row_span = 1;
    bool col_auto = true;
    bool row_auto = true;
    Edges margin;
    // Contributions on the container's inline axis, border box plus margins.
    float min_inline = 0;
    float max_inline = 0;
    // The block size seen in the area round, border box plus margins.
    float natural_block = 0;
    // The item's minimum contribution in the block axis, border box plus margins.
    float min_block = 0;
    float used_inline = 0;
    float used_block = 0;
    // The resolved alignment: justify-self / align-self, falling back to the container's
    // justify-items / align-items when they are auto.
    FlexAlign align_inline = FlexAlign::Stretch;
    FlexAlign align_block = FlexAlign::Stretch;
    bool stretch_inline = true;
    bool stretch_block = true;
    // Round two was skipped: the row sizes do not read this item, and round three asks again
    // at the definite area. `final_result` stays null until that ask.
    bool area_deferred = false;
    const LayoutResult* final_result = nullptr;
};

// ---- Track contributions (CSS Grid section 11.5) ------------------------------------------

// How much of a span the tracks already cover: their current base sizes (or growth limits) plus
// the gaps between them. An item's contribution only moves the tracks that can still grow, and
// only by what the span is still short of. Taking the whole span, not just the fixed tracks, is
// what makes two items in one track satisfy the larger contribution instead of adding up.
float used_extent(const std::vector<Track>& tracks, int start, int span, float gap,
                  bool limit_side);

int growable_count(const std::vector<Track>& tracks, int start, int span, bool limit_side);

// Section 11.5.1: the growth limits take the max-content contributions, after the tracks that
// already have a definite size and the gaps have been paid for.
void contribute_limits(std::vector<Track>& tracks, int start, int span, float contribution,
                       float gap);

// Section 11.5.1: the base sizes take the min-content contributions. The extra space is shared in
// proportion to how much each track can still grow, so a track that has reached its growth limit
// stops taking space and the remaining tracks take the rest.
void contribute_bases(std::vector<Track>& tracks, int start, int span, float contribution,
                      float gap);

// ---- Free space and distribution ----------------------------------------------------------

// Section 11.7: the free space goes to the fr tracks. A track whose contribution is larger than
// the share it would get freezes at that size and the rest of the fr tracks share what is left.
// With no definite available size the fr tracks keep their max-content contribution, which is what
// a shrink-to-fit container needs.
void solve_fr(std::vector<Track>& tracks, bool available_known, float available, float gap);



// Where the track list starts and how much extra goes between tracks, for one content
// distribution value (CSS Box Alignment).
struct Pack {
    float leading = 0;
    float between = 0;
};

Pack pack_of(FlexPack pack, float free, int count);

// Section 12.8: at stretch (which is what `normal` means here) the leftover space is shared by the
// auto-sized tracks. Without an auto-sized track the leftover stays at the end.
void stretch_auto_tracks(std::vector<Track>& tracks, bool available_known, float available,
                         float gap, FlexPack pack);

float sum_tracks(const std::vector<Track>& tracks, float gap);

std::vector<float> track_offsets(const std::vector<Track>& tracks, float gap, const Pack& pack);

float area_extent(const std::vector<Track>& tracks, int start, int span, float gap);



// ---- Placement ----------------------------------------------------------------------------

struct Occupancy {
    std::unordered_set<long long> cells;

    static long long cell_key(int row, int col) {
        return (static_cast<long long>(row) << 32) ^ static_cast<unsigned int>(col);
    }

    bool free(int row, int col, int row_span, int col_span) const {
        for (int r = row; r < row + row_span; ++r) {
            for (int c = col; c < col + col_span; ++c) {
                if (cells.count(cell_key(r, c)) != 0) {
                    return false;
                }
            }
        }
        return true;
    }

    void mark(int row, int col, int row_span, int col_span) {
        for (int r = row; r < row + row_span; ++r) {
            for (int c = col; c < col + col_span; ++c) {
                cells.insert(cell_key(r, c));
            }
        }
    }
};



// Explicit line numbers are 1-based. A negative number counts back from the end of the explicit
// grid, where -1 is its last line (CSS Grid section 8.3); a number that lands before the first
// line names an implicit track *before* the explicit grid, so the index comes back negative and
// the caller lifts the whole grid by the smallest one (section 7.5). A line number of 0 is the
// auto value. A named line resolves to the first explicit line carrying that name
// (`grid-column: header`).
struct LineIndex {
    bool found = false;
    int index = 0;
};

LineIndex line_to_index(const GridLine& line, int explicit_count,
                        const std::vector<std::vector<std::string>>& names,
                        bool end_edge = false);



// One axis of a grid area, from the two lines of the style.
struct AxisArea {
    int index = 0;
    int span = 1;
    bool is_auto = true;
};

AxisArea area_of(const GridLine& start, const GridLine& end, int explicit_count,
                 const std::vector<std::vector<std::string>>& names);



struct PlacementResult {
    std::vector<GridItem> items;
    int column_count = 1;
    int row_count = 1;
};

// Section 8.5, sparse unless dense is asked for, running along rows unless grid-auto-flow says
// columns. Items with both lines given occupy their areas first: an auto item that comes earlier
// in the document must not take a cell away from an explicitly placed one. Auto items are then
// placed in order-modified document order (section 6.1).
PlacementResult place_items(const std::vector<GridItem>& source, const LayoutStyle& style,
                            int template_columns, int template_rows);



// ---- Track lists --------------------------------------------------------------------------

// A `repeat(auto-fill|auto-fit, ...)` marker expands into as many repetitions as fit in the
// container, so it has to be resolved once the container's size on that axis is known and before
// placement (the repetition count is what the explicit line numbers count against). The sum used
// for "does another repetition fit" is the tracks' fixed sizes plus the gaps, per CSS Grid
// section 7.2.3.1; intrinsic tracks count as their zero base size here and are resolved later.
struct TemplateExpansion {
    std::vector<TrackSize> tracks;
    // The line names of the expanded list, one entry per line (tracks + 1), so a `[name]` group
    // that sits next to (or inside) the repetition still reaches the line it belongs to.
    std::vector<std::vector<std::string>> line_names;
    // Where the repetition landed, so `auto-fit` can collapse the repetitions no item spans.
    int repeat_begin = -1;
    int repeat_tracks = 0;
    int repetitions = 0;
    bool auto_fit = false;
};

TemplateExpansion expand_auto_repeat(const std::vector<TrackSize>& template_tracks,
                                     const std::vector<std::vector<std::string>>& template_names,
                                     float available, float gap, bool known);

// The `grid-auto-*` size for an implicit track. The list repeats, and a track before the explicit
// grid walks it backwards, so the last entry is the one next to the explicit grid
// (CSS Grid section 7.5). `offset` is the distance from the explicit grid: -1 for the track just
// before it, 0 for the first one after it.
const TrackSize* auto_track_for(const std::vector<TrackSize>& list, int offset);

std::vector<Track> make_columns(const LayoutStyle& style, const std::vector<TrackSize>& template_tracks,
                                int count, float basis, bool known, int origin);

std::vector<Track> make_rows(const LayoutStyle& style, const std::vector<TrackSize>& template_tracks,
                             int count, float basis, bool known, int origin);

// Solving restarts from the sizing functions every time, so running it twice in one compute (once
// inside measure for the area round, once in solve) gives the same answer instead of folding the
// contributions in twice.
// A `min-content` maximum takes the min-content contribution. Using the max-content
// contribution here made `grid-template-columns: max-content min-content` give both tracks the
// full phrase, so "hello world" never wrapped in the second column.
float limit_contribution(const std::vector<Track>& tracks, int start, int span, float min_size,
                         float max_size);

void size_columns(std::vector<Track>& columns, const std::vector<Track>& preset,
                  const std::vector<GridItem>& items, float gap, bool available_known,
                  float available, FlexPack pack, bool maximize_to_limits);

void size_rows(std::vector<Track>& rows, const std::vector<Track>& preset,
               const std::vector<GridItem>& items, float gap, bool available_known,
               float available, FlexPack pack, bool maximize_to_limits);

inline bool axis_is_subgrid(const std::vector<TrackSize>& tracks) {
    return tracks.size() == 1 && tracks[0].kind == TrackKind::Subgrid;
}

void publish_subgrid_span(GridItem& item, const std::vector<Track>& columns, float column_gap,
                          const std::vector<Track>* rows, float row_gap);

}  // namespace grid_detail

using namespace grid_detail;

// The grid algorithm's intermediate state. `measure.cpp` fills Contributions, `grid.cpp`
// solves them into a Plan, and `geometry.cpp` writes the Plan's used sizes and offsets back
// onto the boxes. Keeping both structs here is what lets those three phases live in their own
// files without a fourth header.
struct Grid {
    struct Contributions {
        std::vector<GridItem> items;
        std::vector<Track> columns;
        std::vector<Track> rows;
        // The un-solved track lists, straight from the sizing functions.
        std::vector<Track> column_template;
        std::vector<Track> row_template;
        int column_count = 1;
        int row_count = 1;
        // The explicit line count after `repeat(auto-fill|auto-fit, ...)` was expanded; implicit
        // tracks may extend beyond it.
        int explicit_columns = 1;
        int explicit_rows = 1;
        // The line names of the expanded template, one entry per line (explicit tracks + 1).
        std::vector<std::vector<std::string>> column_line_names;
        std::vector<std::vector<std::string>> row_line_names;
        // Implicit tracks before the explicit grid, from a line number that counts back past its
        // first line. Track lookups and line names stay in the explicit space; the items are
        // lifted by this much.
        int column_origin = 0;
        int row_origin = 0;
        float column_gap = 0;
        float row_gap = 0;
        float column_space = 0;
        float content_inline = 0;
        bool inline_known = false;
        float content_block = 0;
        bool block_known = false;
        float percent_basis = 0;
        bool percent_known = false;
        Edges margin;
        Edges border;
        Edges padding;
        WritingMode mode = WritingMode::HorizontalTb;
        Direction direction = Direction::Ltr;
        bool horizontal = true;
        const LayoutStyle* style = nullptr;
        ConstraintSpace base_space;
    };

    struct Plan {
        std::vector<Track> columns;
        std::vector<Track> rows;
        std::vector<GridItem> items;
        // The explicit line count after `repeat(auto-fill|auto-fit, ...)` was expanded.
        int explicit_columns = 1;
        int explicit_rows = 1;
        // The line names of the expanded template, one entry per line (explicit tracks + 1).
        std::vector<std::vector<std::string>> column_line_names;
        std::vector<std::vector<std::string>> row_line_names;
        // Implicit tracks in front of the explicit grid; the items and the track lists are in the
        // lifted space, the line numbers and line names are not.
        int column_origin = 0;
        int row_origin = 0;
        std::vector<float> column_offsets;
        std::vector<float> row_offsets;
        float content_inline = 0;
        float content_block = 0;
        // The block size the container was given, as opposed to the one its rows add up to: a
        // fragment of a definite container keeps that size, a content-sized one ends with its rows.
        bool block_definite = false;
        float column_gap = 0;
        float row_gap = 0;
        Edges margin;
        Edges border;
        Edges padding;
        bool horizontal = true;
        BreakToken incoming;
    };

    static Contributions measure(const Box& box, const LayoutKey& key, LayoutContext& cx) {
        return measure(box, key, cx, false);
    }

    static Contributions measure(const Box& box, const LayoutKey& key, LayoutContext& cx,
                                 bool reserve_block_bar);
    static Plan solve(const Contributions& contributions, const LayoutKey& key);
    static LayoutResult place(Box& box, const Plan& plan, LayoutContext& cx,
                              const FragmentationContext& frag, bool reserve_block_bar);
};

void measure_item_areas(Grid::Contributions& out, const LayoutKey& key, LayoutContext& cx,
                        WritingMode mode, Direction direction);

}  // namespace layout
}  // namespace zui

#endif
