#include "layout/inline/script.h"

#include "text/font_chain.h"

namespace zui {
namespace layout {
namespace inline_text {

bool is_strong_script(Script script) {
    return script != Script::Common && script != Script::Inherited;
}

Script script_of(std::uint32_t cp) {
    if (cp <= 0x7f) {
        if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z')) {
            return Script::Latin;
        }
        return Script::Common;
    }
    // Combining marks.
    if ((cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x1AB0 && cp <= 0x1AFF) ||
        (cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x20D0 && cp <= 0x20FF) ||
        (cp >= 0xFE20 && cp <= 0xFE2F)) {
        return Script::Inherited;
    }
    // Hebrew.
    if ((cp >= 0x0590 && cp <= 0x05FF) || (cp >= 0xFB1D && cp <= 0xFB4F)) {
        return Script::Hebrew;
    }
    // Arabic.
    if ((cp >= 0x0600 && cp <= 0x06FF) || (cp >= 0x0750 && cp <= 0x077F) ||
        (cp >= 0x08A0 && cp <= 0x08FF) || (cp >= 0xFB50 && cp <= 0xFDFF) ||
        (cp >= 0xFE70 && cp <= 0xFEFF)) {
        return Script::Arabic;
    }
    // Latin extended.
    if ((cp >= 0x00C0 && cp <= 0x02AF) || (cp >= 0x1E00 && cp <= 0x1EFF) ||
        (cp >= 0x2C60 && cp <= 0x2C7F) || (cp >= 0xA720 && cp <= 0xA7FF) ||
        (cp >= 0xAB30 && cp <= 0xAB6F)) {
        return Script::Latin;
    }
    if (text::is_cjk(cp)) {
        return Script::Cjk;
    }
    // Digits, punctuation, spaces, symbols stay Common.
    if ((cp >= 0x0030 && cp <= 0x0039) || (cp >= 0x2000 && cp <= 0x206F) ||
        (cp >= 0x2070 && cp <= 0x218F) || (cp >= 0x2190 && cp <= 0x21FF) ||
        (cp >= 0x2200 && cp <= 0x22FF) || (cp >= 0x3000 && cp <= 0x303F) || cp == 0x00A0 ||
        cp == 0x3000) {
        return Script::Common;
    }
    return Script::Other;
}

Script resolve_script(Script current, Script previous_strong) {
    if (current == Script::Inherited || current == Script::Common) {
        return is_strong_script(previous_strong) ? previous_strong : Script::Common;
    }
    return current;
}

}  // namespace inline_text
}  // namespace layout
}  // namespace zui
