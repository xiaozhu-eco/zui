#ifndef ZUI_LAYOUT_INLINE_SCRIPT_H
#define ZUI_LAYOUT_INLINE_SCRIPT_H

#include <cstdint>

namespace zui {
namespace layout {
namespace inline_text {

enum class Script : std::uint8_t {
    Common,
    Inherited,
    Latin,
    Hebrew,
    Arabic,
    Cjk,
    Other,
};

Script script_of(std::uint32_t cp);

// Collapse Common/Inherited onto the neighboring strong script for run breaks.
Script resolve_script(Script current, Script previous_strong);

bool is_strong_script(Script script);

}  // namespace inline_text
}  // namespace layout
}  // namespace zui

#endif
