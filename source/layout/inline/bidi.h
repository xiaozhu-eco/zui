#ifndef ZUI_LAYOUT_INLINE_BIDI_H
#define ZUI_LAYOUT_INLINE_BIDI_H

#include "layout/cache/direction.h"

#include <cstdint>
#include <vector>

namespace zui {
namespace layout {
namespace inline_text {

enum class BidiClass : std::uint8_t {
    L,
    R,
    AL,
    EN,
    ES,
    ET,
    AN,
    CS,
    NSM,
    BN,
    B,
    S,
    WS,
    ON,
    LRE,
    RLE,
    LRO,
    RLO,
    PDF,
    LRI,
    RLI,
    FSI,
    PDI,
};

BidiClass bidi_class_of(std::uint32_t cp);

// Resolve paragraph embedding levels (UBA subset: X1-X9 embeds/isolates + weak/neutral/implicit + L2).
// `base` is the CSS direction for the paragraph / IFC.
void resolve_levels(const std::vector<BidiClass>& types, Direction base,
                    std::vector<std::uint8_t>& levels);

// UAX #9 L2: reorder indexes [0, n) into visual left-to-right order for the given levels.
void reorder_visual(const std::vector<std::uint8_t>& levels, std::vector<std::size_t>& order);

Direction direction_of_level(std::uint8_t level);

// UAX #9 L4 / Bidi_Mirrored: paired punctuation for odd embedding levels (FreeType path).
std::uint32_t mirror_cp(std::uint32_t cp);

}  // namespace inline_text
}  // namespace layout
}  // namespace zui

#endif
