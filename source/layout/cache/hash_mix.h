#ifndef ZUI_LAYOUT_CACHE_HASH_MIX_H
#define ZUI_LAYOUT_CACHE_HASH_MIX_H

#include <cstdint>
#include <vector>

namespace zui {
namespace layout {

inline void hash_mix(std::uint64_t& hash, std::uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
}

inline std::uint64_t hash_tokens(const std::vector<std::uint64_t>& tokens) {
    std::uint64_t hash = 0;
    for (std::uint64_t token : tokens) {
        hash_mix(hash, token);
    }
    return hash;
}

}  // namespace layout
}  // namespace zui

#endif
