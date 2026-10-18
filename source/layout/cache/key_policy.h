#ifndef ZUI_LAYOUT_CACHE_KEY_POLICY_H
#define ZUI_LAYOUT_CACHE_KEY_POLICY_H

namespace zui {
namespace layout {

/// Whether the initial containing block (the viewport) has to be part of a layout key.
///
/// `ConstraintSpace::icb` is read in exactly two places, both of them the lazy resolution of a
/// `calc()` / `min()` / `max()` / `clamp()` `width` or `height` (see `flow.cpp` and
/// `in_flow.cpp`). Everything else that needs the viewport has already resolved it while the
/// style was interned. A document whose styles have no such value therefore cannot observe the
/// viewport through a key, and leaving it out is what lets a resize re-use a box whose used size
/// does not depend on its container instead of re-keying the whole tree - measured on a 3.7k-box
/// page, every new width re-queried every box because this token moved.
///
/// Sticky on purpose: it is only ever set (as styles are interned) and is cleared when the engine
/// is reset, so it can cost re-use but never correctness.
///
/// The scope is process-wide, not per document: this is a function-local `static`, cleared only by
/// `LayoutEngine::reset()` (`engine.cpp`). LAYOUT.md section 15 keeps layout on the frame thread
/// alone, so two `LayoutEngine`s sharing it is acceptable; if two documents may be alive at the
/// same time, this flag has to move onto `LayoutContext`.
///
/// It is not a per-box property: an ancestor has to move with a descendant that reads the ICB, so
/// the flag has to cover the whole document.
inline bool& icb_key_flag() {
    static bool needed = false;
    return needed;
}

/// Set when `note_icb_length` flips the flag from false to true. Existing BoxCache slots were
/// hashed without the ICB token; leaving them in the map breaks unordered_map's hash/bucket
/// invariant and shows up as flaky UAFs in LayoutKey::hash/equal. `LayoutEngine::layout` takes
/// this and drops every slot before the frame asks the cache.
///
/// The drop has to destroy the map, not `clear()` it. MSVC's `clear` re-hashes every key when
/// the bucket array is large relative to the element count (the state left after measure slots
/// are evicted). The new hash does not find the old bucket, so the node is freed while a bucket
/// still points at it, and the next insert reads that freed node.
inline bool& icb_key_slots_stale() {
    static bool stale = false;
    return stale;
}

inline bool icb_in_layout_keys() { return icb_key_flag(); }

/// One interned style has a math `width` or `height`, so from now on the ICB stays in every key.
inline void note_icb_length() {
    if (!icb_key_flag()) {
        icb_key_flag() = true;
        icb_key_slots_stale() = true;
    }
}

/// A new document starts over.
inline void reset_icb_lengths() {
    icb_key_flag() = false;
    icb_key_slots_stale() = false;
}

inline bool take_icb_key_slots_stale() {
    const bool stale = icb_key_slots_stale();
    icb_key_slots_stale() = false;
    return stale;
}

}  // namespace layout
}  // namespace zui

#endif
