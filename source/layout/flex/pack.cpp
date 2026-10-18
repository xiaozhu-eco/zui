// Cross-axis packing of the flex lines: each line's cross size and the `align-content`
// distribution of the leftover cross space (CSS Flexbox section 8.4).

#include "layout/flex/pass.h"
#include "layout/flex/detail.h"

#include "layout/baseline.h"
#include "layout/box.h"
#include "layout/cache/fragment_cut.h"
#include "layout/cache/layout_context.h"
#include "layout/cache/logical_axes.h"
#include "layout/cache/used_length.h"
#include "layout/overflow_mode.h"
#include "layout/platform_metrics.h"
#include "layout/scrollbar_v.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>
#include <vector>

namespace zui {
namespace layout {

using namespace flex_detail;

void FlexPass::pack_lines(State& state) {
    const LayoutStyle& style = *state.style;
    const bool row = state.row;
    const bool cross_known = state.cross_known;
    const float content_inline = state.content_inline;
    const float content_block = state.content_block;
    const float cross_gap = state.cross_gap;
    const std::vector<std::vector<Item*>>& lines = state.lines;
    // Cross size of each flex line before align-content packing. Multi-line containers with a
    // definite cross size share leftover space through `align-content` (CSS Flexbox §8.4).
    std::vector<float> line_crosses(lines.size(), 0.f);
    float lines_cross_sum = 0.f;
    for (std::size_t li = 0; li < lines.size(); ++li) {
        float line_cross = 0.f;
        for (Item* item : lines[li]) {
            line_cross = std::max(line_cross,
                                  item->margin_cross_start + item->cross + item->margin_cross_end);
        }
        // A single line fills the container's used cross size. `cross_known` was captured before
        // auto heights and min/max clamps, so it misses `min-height` on a row and `min-width` on
        // a column (the reader status bar and the shelf menu). The sizes above are already used.
        if (lines.size() == 1) {
            line_cross = row ? content_block : content_inline;
        }
        line_crosses[li] = line_cross;
        lines_cross_sum += line_cross;
    }
    const float content_cross = row ? content_block : content_inline;
    const float cross_gaps =
        lines.size() > 1 ? cross_gap * static_cast<float>(lines.size() - 1) : 0.f;
    float align_leading = 0.f;
    float align_between = 0.f;
    if (cross_known && lines.size() > 1) {
        const float free_cross = content_cross - lines_cross_sum - cross_gaps;
        if (style.align_content == FlexPack::Stretch && free_cross > 0.5f) {
            const float extra = free_cross / static_cast<float>(lines.size());
            for (float& line_cross : line_crosses) {
                line_cross += extra;
            }
        } else if (free_cross > 0.5f) {
            // `normal` / Stretch with no leftover, and Start, fall through as leading 0.
            const FlexPack pack =
                style.align_content == FlexPack::Stretch ? FlexPack::Start : style.align_content;
            const Pack packed = pack_of(pack, free_cross, static_cast<int>(lines.size()));
            align_leading = packed.leading;
            align_between = packed.between;
        }
    }

    state.line_crosses = std::move(line_crosses);
    state.content_cross = content_cross;
    state.cross_gaps = cross_gaps;
    state.align_leading = align_leading;
    state.align_between = align_between;
}
}  // namespace layout
}  // namespace zui
