#include "layout/flex/pass.h"

#include <utility>

namespace zui {
namespace layout {

FlexPass::FlexPass(Box& box, const LayoutKey& key, LayoutContext& cx, bool reserve_block_bar)
    : box_(box), key_(key), cx_(cx), reserve_block_bar_(reserve_block_bar) {}

// The pass runs as one ordered chain of phases. Each phase reads the state the ones before it
// wrote and appends its own; `finish` assembles the `LayoutResult`.
FlexPassOutcome FlexPass::run() {
    State state;
    state.out.converged = true;
    state.out.margin_strut.blocks_margin_collapse = true;
    measure_container(state);
    measure_items(state);
    build_lines(state);
    pack_lines(state);
    place_lines(state);
    finish(state);
    return {std::move(state.out), state.block_bar_needed};
}

}  // namespace layout
}  // namespace zui
