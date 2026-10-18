#include "plugins/hotkey/chord.h"

namespace zui {
namespace hotkey_chord {
namespace {

std::string canonical_key(std::string_view key, int& number, char& letter, char& digit) {
    number = -1;
    letter = 0;
    digit = 0;
    if (key.size() >= 2 && (key[0] == 'F' || key[0] == 'f')) {
        int value = 0;
        for (std::size_t i = 1; i < key.size(); ++i) {
            if (key[i] < '0' || key[i] > '9') {
                return {};
            }
            value = value * 10 + (key[i] - '0');
        }
        if (value >= 1 && value <= 24) {
            number = value;
            return "F" + std::to_string(value);
        }
        return {};
    }
    if (key.size() == 1) {
        const unsigned char ch = static_cast<unsigned char>(key[0]);
        if (ch >= 'a' && ch <= 'z') {
            letter = static_cast<char>(ch - 'a' + 'A');
            return std::string(1, letter);
        }
        if (ch >= 'A' && ch <= 'Z') {
            letter = static_cast<char>(ch);
            return std::string(1, letter);
        }
        if (ch >= '0' && ch <= '9') {
            digit = static_cast<char>(ch);
            return std::string(1, digit);
        }
    }
    return {};
}

unsigned macos_letter(char letter) {
    // Carbon virtual keycodes (HIToolbox/Events.h), not ASCII.
    switch (letter) {
    case 'A': return 0x00;
    case 'B': return 0x0B;
    case 'C': return 0x08;
    case 'D': return 0x02;
    case 'E': return 0x0E;
    case 'F': return 0x03;
    case 'G': return 0x05;
    case 'H': return 0x04;
    case 'I': return 0x22;
    case 'J': return 0x26;
    case 'K': return 0x28;
    case 'L': return 0x25;
    case 'M': return 0x2E;
    case 'N': return 0x2D;
    case 'O': return 0x1F;
    case 'P': return 0x23;
    case 'Q': return 0x0C;
    case 'R': return 0x0F;
    case 'S': return 0x01;
    case 'T': return 0x11;
    case 'U': return 0x20;
    case 'V': return 0x09;
    case 'W': return 0x0D;
    case 'X': return 0x07;
    case 'Y': return 0x10;
    case 'Z': return 0x06;
    default: return 0;
    }
}

unsigned macos_digit(char digit) {
    switch (digit) {
    case '0': return 0x1D;
    case '1': return 0x12;
    case '2': return 0x13;
    case '3': return 0x14;
    case '4': return 0x15;
    case '5': return 0x17;
    case '6': return 0x16;
    case '7': return 0x1A;
    case '8': return 0x1C;
    case '9': return 0x19;
    default: return 0;
    }
}

unsigned macos_function(int number) {
    switch (number) {
    case 1: return 0x7A;
    case 2: return 0x78;
    case 3: return 0x63;
    case 4: return 0x76;
    case 5: return 0x60;
    case 6: return 0x61;
    case 7: return 0x62;
    case 8: return 0x64;
    case 9: return 0x65;
    case 10: return 0x6D;
    case 11: return 0x67;
    case 12: return 0x6F;
    case 13: return 0x69;
    case 14: return 0x6B;
    case 15: return 0x71;
    case 16: return 0x6A;
    case 17: return 0x40;
    case 18: return 0x4F;
    case 19: return 0x50;
    case 20: return 0x5A;
    default: return 0;
    }
}

std::string label_for(const plugin::Hotkey& key, const std::string& canonical) {
    std::string label;
    if (key.control) {
        label += "ctrl+";
    }
    if (key.alt) {
        label += "alt+";
    }
    if (key.shift) {
        label += "shift+";
    }
    if (key.meta) {
        label += "meta+";
    }
    label += canonical.empty() ? key.key : canonical;
    return label;
}

}  // namespace

Chord encode(const plugin::Hotkey& key) {
    Chord chord;
    int number = -1;
    char letter = 0;
    char digit = 0;
    const std::string canonical = canonical_key(key.key, number, letter, digit);
    chord.label = label_for(key, canonical);
    if (canonical.empty()) {
        chord.reason = key.key.empty() ? "hotkey key is empty" : "no keycode for " + key.key;
        return chord;
    }

    // Win32 modifiers: MOD_ALT, MOD_CONTROL, MOD_SHIFT, MOD_WIN.
    if (key.alt) {
        chord.win32_mods |= 0x0001u;
    }
    if (key.control) {
        chord.win32_mods |= 0x0002u;
    }
    if (key.shift) {
        chord.win32_mods |= 0x0004u;
    }
    if (key.meta) {
        chord.win32_mods |= 0x0008u;
    }
    if (number > 0) {
        chord.win32_vk = 0x70u + static_cast<unsigned>(number - 1);
    } else if (letter != 0) {
        chord.win32_vk = static_cast<unsigned char>(letter);
    } else {
        chord.win32_vk = static_cast<unsigned char>(digit);
    }
    chord.win32 = chord.win32_vk != 0;

    // X11: ShiftMask, ControlMask, Mod1Mask (Alt), Mod4Mask (Super).
    if (key.shift) {
        chord.linux_mods |= 1u;
    }
    if (key.control) {
        chord.linux_mods |= 4u;
    }
    if (key.alt) {
        chord.linux_mods |= 8u;
    }
    if (key.meta) {
        chord.linux_mods |= 64u;
    }
    if (number > 0) {
        chord.linux_keysym = 0xffbeu + static_cast<unsigned>(number - 1);
    } else if (letter != 0) {
        chord.linux_keysym = static_cast<unsigned>(letter - 'A' + 'a');
    } else {
        chord.linux_keysym = static_cast<unsigned char>(digit);
    }
    chord.linux = chord.linux_keysym != 0;

    // Carbon: controlKey 1<<12, optionKey 1<<11, shiftKey 1<<9, cmdKey 1<<8.
    if (key.control) {
        chord.macos_mods |= 1u << 12;
    }
    if (key.alt) {
        chord.macos_mods |= 1u << 11;
    }
    if (key.shift) {
        chord.macos_mods |= 1u << 9;
    }
    if (key.meta) {
        chord.macos_mods |= 1u << 8;
    }
    if (number > 0) {
        chord.macos_keycode = macos_function(number);
    } else if (letter != 0) {
        chord.macos_keycode = macos_letter(letter);
    } else {
        chord.macos_keycode = macos_digit(digit);
    }
    chord.macos = chord.macos_keycode != 0;

    // Android KeyEvent: META_SHIFT_ON, META_ALT_ON, META_CTRL_ON, META_META_ON.
    if (key.shift) {
        chord.android_meta |= 1;
    }
    if (key.alt) {
        chord.android_meta |= 2;
    }
    if (key.control) {
        chord.android_meta |= 0x1000;
    }
    if (key.meta) {
        chord.android_meta |= 0x10000;
    }
    if (number >= 1 && number <= 12) {
        chord.android_keycode = 131 + (number - 1);
    } else if (letter != 0) {
        chord.android_keycode = 29 + (letter - 'A');
    } else if (digit != 0) {
        chord.android_keycode = 7 + (digit - '0');
    }
    chord.android = chord.android_keycode != 0;

    // UIKeyModifierShift/Control/Alternate/Command. Letters and digits only.
    if (key.shift) {
        chord.ios_flags |= 1u << 17;
    }
    if (key.control) {
        chord.ios_flags |= 1u << 18;
    }
    if (key.alt) {
        chord.ios_flags |= 1u << 19;
    }
    if (key.meta) {
        chord.ios_flags |= 1u << 20;
    }
    if (letter != 0) {
        chord.ios_characters = std::string(1, static_cast<char>(letter - 'A' + 'a'));
        chord.ios_input = true;
    } else if (digit != 0) {
        chord.ios_characters = std::string(1, digit);
        chord.ios_input = true;
    }
    return chord;
}

bool same(const Chord& lhs, const Chord& rhs) { return lhs.label == rhs.label; }

}  // namespace hotkey_chord
}  // namespace zui
