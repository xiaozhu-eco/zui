#include "layout/cache/layout_key.h"

#include "layout/box.h"
#include "layout/cache/hash_mix.h"
#include "layout/style_literal.h"

#include <chrono>
#include <cstring>

namespace zui {
namespace layout {
namespace {
struct KeyDiag {
    double hash_ms = 0;
    double equal_ms = 0;
    double seal_ms = 0;
    std::uint64_t hash_n = 0;
    std::uint64_t equal_n = 0;
    std::uint64_t seal_n = 0;
};
KeyDiag g_key_diag;
}  // namespace

void reset_key_diag() { g_key_diag = {}; }

void key_diag(double& hash_ms, double& equal_ms, std::uint64_t& hash_n, std::uint64_t& equal_n,
              double& seal_ms, std::uint64_t& seal_n) {
    hash_ms = g_key_diag.hash_ms;
    equal_ms = g_key_diag.equal_ms;
    hash_n = g_key_diag.hash_n;
    equal_n = g_key_diag.equal_n;
    seal_ms = g_key_diag.seal_ms;
    seal_n = g_key_diag.seal_n;
}

namespace {

/// A null style is the same empty object `style_or_default` uses for a box with no interned
/// style. Constructing one at the call site allocated a debug iterator proxy for every string and
/// vector in `LayoutStyle` (font, lang, four grid lines, the track vectors) on every seal, and
/// the hot path never read it: `key.style` is the interned style.
const LayoutStyle& style_of_key(const std::shared_ptr<const LayoutStyle>& style) {
    if (style) {
        return *style;
    }
    static const LayoutStyle empty;
    return empty;
}

bool math_length_value(const std::string* value) {
    if (value == nullptr) {
        return false;
    }
    return value->rfind("calc(", 0) == 0 || value->rfind("min(", 0) == 0 ||
           value->rfind("max(", 0) == 0 || value->rfind("clamp(", 0) == 0;
}

/// A math function on the block size is resolved from the node, not from a percent flag, and two
/// nodes can share one `LayoutStyle`. The bit has to be read from this box.
bool node_honors_block_basis(const html::Node* node) {
    if (node == nullptr) {
        return false;
    }
    return math_length_value(style_literal(node, "height")) ||
           math_length_value(style_literal(node, "min-height")) ||
           math_length_value(style_literal(node, "max-height")) ||
           math_length_value(style_literal(node, "top")) ||
           math_length_value(style_literal(node, "bottom"));
}

}  // namespace

bool LayoutKey::equal(const LayoutKey& other) const {
    if (style.get() != other.style.get() || content != other.content || used_sig != other.used_sig) {
        return false;
    }
    if (sealed && other.sealed && seal_n == other.seal_n) {
        return std::memcmp(seal_words, other.seal_words, static_cast<std::size_t>(seal_n) * sizeof(std::uint64_t)) ==
               0;
    }
    return space.equal(other.space, style_of_key(style));
}

LayoutKey key_for(const Box& box, const ConstraintSpace& space) {
    LayoutKey key;
    key.style = box.style_ptr;
    key.content = box.content;
    key.space = space;
    key.space.honor_block_basis = node_honors_block_basis(box.node);
    // Section 9 step C: the box's `content-visibility: auto` skip state decides its geometry, so
    // it has to be part of the key. Reading it here is what lets every ask carry it without each
    // parent having to know the child's flag.
    key.space.contents_skipped = box.contents_skipped;
    // Length transitions write used overlays outside the interned style; fingerprint them so the
    // cache cannot keep the pre-transition geometry while sample() moves width/margin/padding.
    if (box.node != nullptr) {
        key.used_sig = html::length_overlay_sig(box.node->used);
    }
    seal_layout_key(key);
    ++g_key_diag.seal_n;
    return key;
}

std::uint64_t LayoutKey::hash() const {
    if (sealed) {
        return seal_hash;
    }
    std::uint64_t mixed = 0;
    hash_mix(mixed, static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(style.get())));
    hash_mix(mixed, content);
    hash_mix(mixed, space.hash(style_of_key(style)));
    hash_mix(mixed, used_sig);
    return mixed;
}

void seal_layout_key(LayoutKey& key) {
    const LayoutStyle& style = style_of_key(key.style);
    int n = 0;
    const bool ok = key.space.export_tokens(style, key.seal_words, LayoutKey::kSealCap, n);
    // A truncated or empty token list stays on the live path. A stale `sealed` flag would make
    // `hash()` return words that no longer describe `space`.
    if (!ok) {
        key.sealed = false;
        key.seal_n = 0;
        return;
    }
    // Same mix as `HashSink` over `tokenize`, then `ConstraintSpace::hash`'s exclusion word,
    // then `LayoutKey::hash`. One tokenize; calling `hash()` here would tokenize again.
    std::uint64_t space_hash = 0;
    for (int i = 0; i < n; ++i) {
        hash_mix(space_hash, key.seal_words[i]);
    }
    const std::uint64_t exclusion_hash = key.space.exclusion ? key.space.exclusion->hash() : 0;
    hash_mix(space_hash, exclusion_hash);
    std::uint64_t mixed = 0;
    hash_mix(mixed, static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(key.style.get())));
    hash_mix(mixed, key.content);
    hash_mix(mixed, space_hash);
    hash_mix(mixed, key.used_sig);
    key.seal_n = static_cast<std::uint8_t>(n);
    key.seal_hash = mixed;
    key.sealed = true;
}

}  // namespace layout
}  // namespace zui
