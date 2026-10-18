#ifndef ZUI_PLUGIN_HOTKEY_CHORD_H
#define ZUI_PLUGIN_HOTKEY_CHORD_H

// Hotkey chord shared by every OS file. A Windows test calls this function.
// The platform translation unit uses the same result, so two keys cannot collapse
// into one registration inside a second copy of the encoder.

#include <zui/plugin.h>

#include <string>

namespace zui {
namespace hotkey_chord {

struct Chord {
    // Caller key plus modifiers, in a fixed order. Two different chords differ here.
    std::string label;
    // Empty when at least one platform can name the key. Otherwise why it cannot.
    std::string reason;
    bool win32 = false;
    unsigned win32_vk = 0;
    unsigned win32_mods = 0;
    bool linux = false;
    unsigned linux_keysym = 0;
    unsigned linux_mods = 0;
    bool macos = false;
    unsigned macos_keycode = 0;
    unsigned macos_mods = 0;
    bool android = false;
    int android_keycode = 0;
    int android_meta = 0;
    // iOS can name a letter or digit for a key command. It still has no global hotkey.
    bool ios_input = false;
    std::string ios_characters;
    unsigned ios_flags = 0;
};

Chord encode(const plugin::Hotkey& key);

// True when both chords name the same key and the same modifiers.
bool same(const Chord& lhs, const Chord& rhs);

}  // namespace hotkey_chord
}  // namespace zui

#endif
