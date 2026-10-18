#ifndef ZUI_TEXT_GRAPHEME_BREAK_H
#define ZUI_TEXT_GRAPHEME_BREAK_H

// Minimal UAX#29 grapheme cluster helpers for caret / cluster stepping.
// Implemented rules (caret subset): GB3, GB4, GB5, GB6, GB7, GB8, GB9, GB9a,
// GB9b, GB11, GB12, GB13, GB999. No ICU; unknown code points are Other
// (boundary between Others = yes). Property coverage is compact: ASCII/control,
// Extend, ZWJ, common Extended_Pictographic ranges, Regional_Indicator, Hangul
// L/V/T/LV/LVT, Prepend, SpacingMark.

#include <cstddef>
#include <cstdint>
#include <string>

namespace zui {
namespace text {

enum class GraphemeBreakProp : std::uint8_t {
    Other = 0,
    CR,
    LF,
    Control,
    Extend,
    ZWJ,
    Regional_Indicator,
    Prepend,
    SpacingMark,
    L,
    V,
    T,
    LV,
    LVT,
    Extended_Pictographic,
};

GraphemeBreakProp grapheme_prop(char32_t cp);

// True if a boundary exists between the codepoint ending at `before` and the one
// starting at `at` in UTF-8 `text`. `at==0` or `at==text.size()` are boundaries.
bool grapheme_is_boundary(const std::string& text, std::size_t at);

// Past the grapheme cluster that contains `index` (or at size if past end).
std::size_t grapheme_next(const std::string& text, std::size_t index);

// Start of the previous grapheme cluster before `index` (or 0).
std::size_t grapheme_prev(const std::string& text, std::size_t index);

}  // namespace text
}  // namespace zui

#endif