#ifndef ZUI_LAYOUT_CACHE_BOX_CACHE_H
#define ZUI_LAYOUT_CACHE_BOX_CACHE_H

#include "layout/cache/layout_key.h"
#include "layout/cache/layout_result.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace zui {
namespace layout {

constexpr std::size_t kMeasureSlotLimit = 8;

class BoxCache {
public:
    const LayoutResult* find(const LayoutKey& key) const;
    /// Takes the key by value: the slot owns it, and the caller keeps its own copy for the return
    /// value of `LayoutContext::store`. Returns true when the call replaced a slot that was already
    /// there - the caller needs that, because replacing a result can make the results above it
    /// stale, while inserting one cannot (a slot that was absent is one no cached parent could
    /// have been answered with).
    bool store(LayoutKey key, std::shared_ptr<LayoutResult> result);
    /// Move the pinned Final to `key`'s slot. The pin is "the last Final this box answered with",
    /// and an answer served from the cache is still an answer: a Final ask that hits must move the
    /// pin, or a measurement pass's layout (min-content, say) stays pinned after the real layout
    /// re-used an earlier key. Returns whether the pin actually moved to a different result - the
    /// pin's `used_requests` are one of the two things `LayoutContext::pending` reads, so a move is
    /// one of the edits that advance the dirtiness generation.
    bool touch_final(const LayoutKey& key);
    /// Clears the pin without touching the slots. `drop_slots` keeps the pin on purpose (paint
    /// reads it), which is why the one caller that has to break a name the pin holds does it here
    /// instead of inside `drop_slots`.
    void clear_pin() { pin_.reset(); }
    /// Drops every slot that names one of `boxes`, and the pin when it names one. A stored result
    /// records its children (`used_requests`, `child_places`) and an inline item or a float record
    /// can name a descendant, so a slot can outlive the boxes it recorded. Returns how many slots
    /// went; the caller has to move the epoch of the box that lost them (see
    /// `LayoutContext::forget_boxes`).
    std::size_t drop_slots_naming(const std::unordered_set<const Box*>& boxes);
    void drop_slots();
    /// Monotonic freshness epoch. Slot drops, replacements, and measure eviction advance it;
    /// `drop_slots` does not bring it back to zero, or a parent that remembered the old value
    /// would treat the new slots as the ones it already checked.
    std::uint64_t epoch() const { return epoch_; }
    void bump_epoch() { ++epoch_; }
    const LayoutResult* pinned_final() const { return pin_.get(); }
    bool empty() const { return slots_.empty(); }
    /// The number of non-Final slots, kept up to date by every mutation so the eviction pass below
    /// does not have to recount after each removal.
    std::size_t measure_count() const;
    // Drops the oldest non-Final slots until only `limit` remain. The pinned Final stays.
    std::size_t evict_measures(std::size_t limit);

    /// Frame stamp of the layout context's "boxes this frame stored" set. It is a stamp and not
    /// a flag because the set is per frame and is never cleared: the context keeps one counter
    /// and every box remembers which frame last put it in the set.
    bool seen_in(std::uint64_t stamp) const { return seen_stamp_ == stamp; }
    void mark_seen(std::uint64_t stamp) { seen_stamp_ = stamp; }

    template <typename Fn> void for_results(Fn&& fn) const {
        for (const auto& slot : slots_) {
            if (slot.second != nullptr) {
                fn(*slot.second);
            }
        }
    }

private:
    struct KeyHash {
        std::size_t operator()(const LayoutKey& key) const noexcept {
            return static_cast<std::size_t>(key.hash());
        }
    };
    struct KeyEq {
        bool operator()(const LayoutKey& left, const LayoutKey& right) const {
            return left.equal(right);
        }
    };

    std::unordered_map<LayoutKey, std::shared_ptr<LayoutResult>, KeyHash, KeyEq> slots_;
    std::vector<const LayoutKey*> order_;
    std::shared_ptr<LayoutResult> pin_;
    std::size_t measures_ = 0;
    std::uint64_t seen_stamp_ = 0;
    std::uint64_t epoch_ = 0;
};

}  // namespace layout
}  // namespace zui

#endif
