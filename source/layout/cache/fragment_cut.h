#ifndef ZUI_LAYOUT_CACHE_FRAGMENT_CUT_H
#define ZUI_LAYOUT_CACHE_FRAGMENT_CUT_H

#include "layout/cache/break_token.h"

#include <algorithm>

namespace zui {
namespace layout {

/// One fragment of a container that breaks between its rows / lines / children (LAYOUT.md
/// section 12). `cut_fragment` is the arithmetic every such container shares; the caller decides
/// *where* it breaks (which row does not fit) and what the fragment's content reached.
struct FragmentCut {
    /// This fragment's border-box block size.
    float border_block = 0;
    /// The content inside this fragment, measured from its own content start.
    float content_block = 0;
    BreakToken token;
    /// How much taller the fragment would have had to be for the first content that did not fit.
    float space_shortage = 0;
};

/// `own_start` is how much of the box's own block axis earlier fragments consumed, `remaining`
/// what the fragmentainer offered. `chrome_start` / `chrome_end` are the box's block-start /
/// block-end border plus padding, `content` the content this fragment holds (measured from the
/// fragment's content start), `box_size` the box's own block size when it is definite (`< 0`:
/// content-sized), `finished` whether everything was placed, `first_unplaced` the child index the
/// next fragment resumes at (`-1`: no child boundary), `unplaced_line` the line index the next
/// fragment resumes at (`0`: none) and `shortage` how much taller this fragment had to be for the
/// first content it could not place.
///
/// A fragment where nothing was placed reports zero block size, so a container can tell "this
/// opens the next fragment" from "this fits"; a fragment whose first content could not go anywhere
/// else keeps it and overflows, which is the amount the container's balancing stretches by.
inline FragmentCut cut_fragment(float own_start, float remaining, float chrome_start,
                                float chrome_end, float content, float box_size, bool finished,
                                int first_unplaced, int unplaced_line, float shortage) {
    FragmentCut out;
    own_start = std::max(0.f, own_start);
    const float limit = own_start + std::max(0.f, remaining);
    const bool first_fragment = own_start <= 0.01f;
    const float client_start = first_fragment ? chrome_start : own_start;
    const float placed = std::max(0.f, content);
    const float content_end = client_start + placed;
    const bool empty = placed <= 0.01f && !finished;
    const float box_end = box_size >= 0.f ? box_size : content_end + (finished ? chrome_end : 0.f);
    const float bounded = std::min(box_end, limit);
    const float fragment_end =
        empty ? own_start : std::min(box_end, std::max(bounded, content_end));
    if (fragment_end < box_end - 0.01f || !finished) {
        out.token.child_index =
            first_unplaced >= 0 ? first_unplaced : (empty ? 0 : -1);
        out.token.line_index = std::max(0, unplaced_line);
        out.token.block_offset = fragment_end;
    }
    out.border_block = std::max(0.f, fragment_end - own_start);
    out.content_block = std::max(0.f, std::min(placed, fragment_end - client_start));
    out.space_shortage = std::max(0.f, shortage);
    return out;
}

}  // namespace layout
}  // namespace zui

#endif
