#include "html/document.h"

#include <cstdint>

namespace zui {
namespace html {

const Atom* literal_atom(const char* text) {
    if (text == nullptr) {
        return nullptr;
    }
    struct Slot {
        const char* key = nullptr;
        Atom atom;
    };
    static Slot slots[256];
    std::size_t i = (reinterpret_cast<std::uintptr_t>(text) >> 3) & 255u;
    for (int n = 0; n < 256; ++n) {
        Slot& slot = slots[i];
        if (slot.key == nullptr) {
            slot.atom = Atom::intern(text);
            slot.key = text;
            return &slot.atom;
        }
        if (slot.key == text) {
            return &slot.atom;
        }
        i = (i + 1) & 255u;
    }
    return nullptr;
}

}  // namespace html
}  // namespace zui
