#ifndef ZUI_LAYOUT_CACHE_BREAK_TOKEN_H
#define ZUI_LAYOUT_CACHE_BREAK_TOKEN_H

namespace zui {
namespace layout {

struct BreakToken {
    // The first child of the box that did not fit (`-1`: no child boundary was reached).
    int child_index = -1;
    // The first line of an inline formatting context that did not fit (`0`: none).
    int line_index = 0;
    // The character offset inside the first line that did not fit (`0`: the line start).
    int text_offset = 0;
    // How much of the box's own block axis this fragment consumed, measured from the box's
    // border-box start. A box with a definite block size can be split even when all of its content
    // fits, and no child index or line index can describe where to resume: this is that position
    // (LAYOUT.md section 12). Chromium carries the same number as `consumed_block_size`.
    float block_offset = 0;
    // When the break is inside the child at `child_index` (its own layout split between its lines),
    // how much of *that child's* block axis it already consumed. The child resumes at this offset,
    // and the parent's own `block_offset` cannot carry it: the two are in different frames.
    float child_block_offset = 0;
    // When that child itself broke at a nested child boundary, the child's own `child_index` /
    // `child_block_offset` (deeper multi-level fragmentation). `-1` / `0` when the child only
    // broke between its own lines.
    int child_inner_index = -1;
    float child_inner_block = 0;

    /// True when this fragment is complete: nothing was pushed to a next fragmentainer.
    bool done() const {
        return child_index < 0 && line_index == 0 && text_offset == 0 && block_offset <= 0.f;
    }
};

}  // namespace layout
}  // namespace zui

#endif
