#ifndef ZUI_LAYOUT_CACHE_ASK_FUSE_H
#define ZUI_LAYOUT_CACHE_ASK_FUSE_H

#include "layout/cache/child_request.h"

#include <cstddef>
#include <unordered_map>
#include <vector>

namespace zui {
namespace layout {

struct Box;
struct LayoutResult;

// One compute may ask a child twice. A third key is not recorded.
// keep_latest marks earlier asks that the algorithm did not read; drop_superseded removes them.
class AskFuse {
public:
    const LayoutResult* refuse(const Box& child, const LayoutKey& key, bool& capped) const;
    void record(Box& child, const LayoutKey& key, const LayoutResult* result);
    /// The child's earlier asks can go. `last` is the index, in the frame's request vector, of the
    /// ask that stays - the frame tracks it, so this is a store and not a search. The table calls
    /// it once per cell, and searching here for the last ask walked every request recorded so far,
    /// which made a row O(cells^2).
    void keep_latest(const Box& child, std::size_t last);
    /// One pass over the frame's requests, dropping the asks `keep_latest` superseded. Compacting
    /// per call (as this used to) was the other half of the table's quadratic.
    void drop_superseded(std::vector<ChildRequest>& requests) const;
    /// Start over for one child. An algorithm that runs a second pass on purpose -- reserving a
    /// scrollbar gutter, for instance -- must be able to ask again with a different size instead
    /// of being handed the first pass's answer.
    void reset(const Box& child);
    /// Drop every slot but keep the maps' buckets. A frame that is parked and reused would
    /// otherwise allocate a fresh debug proxy for both maps on every compute.
    void clear();

    /// LAYOUT.md section 16 observation channel: how many slots there are, one per child the parent
    /// asked.
    std::size_t slot_count() const { return slots_.size(); }
    /// How many key comparisons `refuse` has run. Each one tokenises both keys, so this is the
    /// counter that says whether the fuse is a hotspot rather than just O(children).
    std::size_t key_compares() const { return key_compares_; }

private:
    struct Slot {
        int count = 0;
        LayoutKey second_key;
        const LayoutResult* second = nullptr;
    };

    // Keyed by the child: `refuse` used to walk one slot per child to find the ask, which is
    // O(children) per ask - and a container that asks every child and then resets the fuse (flex,
    // grid, table) paid O(children^2) per pass. A pointer key needs no `LayoutKey` comparison, so
    // only the answered asks still compare keys.
    std::unordered_map<const Box*, Slot> slots_;
    std::unordered_map<const Box*, std::size_t> keep_from_;
    mutable std::size_t key_compares_ = 0;
};

}  // namespace layout
}  // namespace zui

#endif
