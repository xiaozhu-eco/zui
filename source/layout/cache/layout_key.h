#ifndef ZUI_LAYOUT_CACHE_LAYOUT_KEY_H
#define ZUI_LAYOUT_CACHE_LAYOUT_KEY_H

#include "layout/cache/constraint_space.h"
#include "layout/cache/content_version.h"
#include "layout/layout_style.h"

#include <cstdint>
#include <memory>

namespace zui {
namespace layout {

struct Box;

struct LayoutKey {
    std::shared_ptr<const LayoutStyle> style;
    ContentVersion content = 0;
    ConstraintSpace space;
    /// Live length-transition overlays on the box's node (0 when none).
    std::uint64_t used_sig = 0;

    /// Tokens of `space` for this `style`, captured by `seal_layout_key`. A cache lookup then
    /// hashes and compares the words instead of tokenizing both keys again. `sealed` is false
    /// for a key built by hand, and any write to `space` or `style` after a seal has to seal again.
    static constexpr int kSealCap = 64;
    std::uint64_t seal_words[kSealCap]{};
    std::uint64_t seal_hash = 0;
    std::uint8_t seal_n = 0;
    bool sealed = false;

    bool equal(const LayoutKey& other) const;
    std::uint64_t hash() const;
};

LayoutKey key_for(const Box& box, const ConstraintSpace& space);

/// Fills `seal_words` from the key's current style and space. Call again after changing either.
void seal_layout_key(LayoutKey& key);

/// TEMPORARY: wall time inside LayoutKey::hash / equal. Remove with the diagnostic clocks.
void reset_key_diag();
void key_diag(double& hash_ms, double& equal_ms, std::uint64_t& hash_n, std::uint64_t& equal_n,
              double& seal_ms, std::uint64_t& seal_n);

}  // namespace layout
}  // namespace zui

#endif
