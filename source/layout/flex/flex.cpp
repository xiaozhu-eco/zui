#include "layout/flex/flex.h"
#include "layout/flex/detail.h"
#include "layout/flex/pass.h"

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

LayoutResult layout_flex(Box& box, const LayoutKey& key, LayoutContext& cx) {
    FlexPassOutcome measured = FlexPass(box, key, cx, false).run();
    if (!measured.block_bar_needed) {
        return std::move(measured.layout);
    }
    // The items have to be asked again with a smaller inline size, so the first pass's answers
    // must not be handed back by the ask fuse; every item is asked once per pass on purpose.
    cx.note_flex_bar_rerun();
    for (Box* child : box.children) {
        if (child != nullptr) {
            cx.keep_latest_ask(*child);
            cx.reset_ask_fuse(*child);
        }
    }
    return FlexPass(box, key, cx, true).run().layout;
}

}  // namespace layout
}  // namespace zui
