#include "text/grapheme_break.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zui {
namespace text {
namespace {

using Prop = GraphemeBreakProp;

char32_t decode_cp(std::string_view s, std::size_t& i) {
    if (i >= s.size()) {
        return 0;
    }
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
        ++i;
        return c;
    }
    if ((c >> 5) == 0x6 && i + 1 < s.size()) {
        const char32_t cp = (static_cast<char32_t>(c & 0x1f) << 6) |
                            (static_cast<unsigned char>(s[i + 1]) & 0x3f);
        i += 2;
        return cp;
    }
    if ((c >> 4) == 0xe && i + 2 < s.size()) {
        const char32_t cp = (static_cast<char32_t>(c & 0x0f) << 12) |
                            ((static_cast<unsigned char>(s[i + 1]) & 0x3f) << 6) |
                            (static_cast<unsigned char>(s[i + 2]) & 0x3f);
        i += 3;
        return cp;
    }
    if ((c >> 3) == 0x1e && i + 3 < s.size()) {
        const char32_t cp = (static_cast<char32_t>(c & 0x07) << 18) |
                            ((static_cast<unsigned char>(s[i + 1]) & 0x3f) << 12) |
                            ((static_cast<unsigned char>(s[i + 2]) & 0x3f) << 6) |
                            (static_cast<unsigned char>(s[i + 3]) & 0x3f);
        i += 4;
        return cp;
    }
    ++i;
    return 0xfffd;
}

std::size_t utf8_cp_prev(std::string_view s, std::size_t index) {
    if (index == 0) {
        return 0;
    }
    --index;
    while (index > 0 && (static_cast<unsigned char>(s[index]) & 0xc0) == 0x80) {
        --index;
    }
    return index;
}

bool in_range(char32_t cp, char32_t lo, char32_t hi) {
    return cp >= lo && cp <= hi;
}

bool is_extended_pictographic(char32_t cp) {
    // Compact coverage of common emoji / Extended_Pictographic ranges used by caret tests.
    // Not a full Unicode Emoji property table.
    if (cp == 0x00A9 || cp == 0x00AE || cp == 0x203C || cp == 0x2049 || cp == 0x2122 ||
        cp == 0x2139 || cp == 0x2328 || cp == 0x23CF || cp == 0x24C2 || cp == 0x25B6 ||
        cp == 0x25C0 || cp == 0x2B50 || cp == 0x2B55 || cp == 0x3030 || cp == 0x303D ||
        cp == 0x3297 || cp == 0x3299) {
        return true;
    }
    if (in_range(cp, 0x2194, 0x2199) || in_range(cp, 0x21A9, 0x21AA) ||
        in_range(cp, 0x231A, 0x231B) || in_range(cp, 0x23E9, 0x23F3) ||
        in_range(cp, 0x23F8, 0x23FA) || in_range(cp, 0x25AA, 0x25AB) ||
        in_range(cp, 0x25FB, 0x25FE) || in_range(cp, 0x2600, 0x27BF) ||
        in_range(cp, 0x2934, 0x2935) || in_range(cp, 0x2B05, 0x2B07) ||
        in_range(cp, 0x2B1B, 0x2B1C) || in_range(cp, 0x1F000, 0x1F02F) ||
        in_range(cp, 0x1F0A0, 0x1F0FF) || in_range(cp, 0x1F100, 0x1F1FF) ||
        in_range(cp, 0x1F200, 0x1F2FF) || in_range(cp, 0x1F300, 0x1F5FF) ||
        in_range(cp, 0x1F600, 0x1F64F) || in_range(cp, 0x1F680, 0x1F6FF) ||
        in_range(cp, 0x1F700, 0x1F77F) || in_range(cp, 0x1F780, 0x1F7FF) ||
        in_range(cp, 0x1F800, 0x1F8FF) || in_range(cp, 0x1F900, 0x1F9FF) ||
        in_range(cp, 0x1FA00, 0x1FA6F) || in_range(cp, 0x1FA70, 0x1FAFF) ||
        in_range(cp, 0x1FB00, 0x1FBFF)) {
        // Regional indicators live in 1F1E6-1F1FF; those are classified separately.
        if (in_range(cp, 0x1F1E6, 0x1F1FF)) {
            return false;
        }
        return true;
    }
    return false;
}

bool is_extend(char32_t cp) {
    if (cp == 0x200C) {  // ZWNJ — Extend in Grapheme_Cluster_Break
        return true;
    }
    // Combining Diacritical Marks and common mark blocks.
    if (in_range(cp, 0x0300, 0x036F) || in_range(cp, 0x0483, 0x0489) ||
        in_range(cp, 0x0591, 0x05BD) || cp == 0x05BF || in_range(cp, 0x05C1, 0x05C2) ||
        in_range(cp, 0x05C4, 0x05C5) || cp == 0x05C7 || in_range(cp, 0x0610, 0x061A) ||
        in_range(cp, 0x064B, 0x065F) || cp == 0x0670 || in_range(cp, 0x06D6, 0x06DC) ||
        in_range(cp, 0x06DF, 0x06E4) || in_range(cp, 0x06E7, 0x06E8) ||
        in_range(cp, 0x06EA, 0x06ED) || in_range(cp, 0x0711, 0x0711) ||
        in_range(cp, 0x0730, 0x074A) || in_range(cp, 0x07A6, 0x07B0) ||
        in_range(cp, 0x07EB, 0x07F3) || cp == 0x07FD || in_range(cp, 0x0816, 0x0819) ||
        in_range(cp, 0x081B, 0x0823) || in_range(cp, 0x0825, 0x0827) ||
        in_range(cp, 0x0829, 0x082D) || in_range(cp, 0x0859, 0x085B) ||
        in_range(cp, 0x08D3, 0x08FF) || in_range(cp, 0x093C, 0x093C) ||
        in_range(cp, 0x094D, 0x094D) || in_range(cp, 0x0951, 0x0957) ||
        in_range(cp, 0x09BC, 0x09BC) || cp == 0x09CD || in_range(cp, 0x09FE, 0x09FE) ||
        in_range(cp, 0x0A3C, 0x0A3C) || cp == 0x0A4D || in_range(cp, 0x0ABC, 0x0ABC) ||
        cp == 0x0ACD || in_range(cp, 0x0B3C, 0x0B3C) || cp == 0x0B4D ||
        in_range(cp, 0x0BBC, 0x0BBC) || cp == 0x0BCD || cp == 0x0C4D || cp == 0x0CCD ||
        in_range(cp, 0x0D3B, 0x0D3C) || cp == 0x0D4D || cp == 0x0DCA ||
        in_range(cp, 0x0E31, 0x0E31) || in_range(cp, 0x0E34, 0x0E3A) ||
        in_range(cp, 0x0E47, 0x0E4E) || in_range(cp, 0x0EB1, 0x0EB1) ||
        in_range(cp, 0x0EB4, 0x0EBC) || in_range(cp, 0x0EC8, 0x0ECD) ||
        in_range(cp, 0x0F18, 0x0F19) || cp == 0x0F35 || cp == 0x0F37 || cp == 0x0F39 ||
        in_range(cp, 0x0F71, 0x0F7E) || in_range(cp, 0x0F80, 0x0F84) ||
        in_range(cp, 0x0F86, 0x0F87) || in_range(cp, 0x0F8D, 0x0F97) ||
        in_range(cp, 0x0F99, 0x0FBC) || cp == 0x0FC6 || in_range(cp, 0x135D, 0x135F) ||
        in_range(cp, 0x1714, 0x1715) || in_range(cp, 0x17B4, 0x17B5) ||
        in_range(cp, 0x17C9, 0x17D3) || cp == 0x17DD || in_range(cp, 0x180B, 0x180D) ||
        cp == 0x180F || in_range(cp, 0x1885, 0x1886) || cp == 0x18A9 ||
        in_range(cp, 0x1920, 0x1922) || in_range(cp, 0x1A17, 0x1A18) ||
        in_range(cp, 0x1A56, 0x1A5E) || cp == 0x1A60 || in_range(cp, 0x1A62, 0x1A7C) ||
        cp == 0x1A7F || in_range(cp, 0x1AB0, 0x1ACE) || in_range(cp, 0x1B00, 0x1B03) ||
        cp == 0x1B34 || in_range(cp, 0x1B36, 0x1B3A) || cp == 0x1B3C || cp == 0x1B42 ||
        in_range(cp, 0x1B6B, 0x1B73) || in_range(cp, 0x1B80, 0x1B81) ||
        in_range(cp, 0x1BA2, 0x1BA5) || in_range(cp, 0x1BA8, 0x1BA9) ||
        in_range(cp, 0x1BAB, 0x1BAD) || cp == 0x1BE6 || in_range(cp, 0x1BE8, 0x1BE9) ||
        cp == 0x1BED || in_range(cp, 0x1BEF, 0x1BF1) || in_range(cp, 0x1C2C, 0x1C33) ||
        in_range(cp, 0x1C36, 0x1C37) || in_range(cp, 0x1CD0, 0x1CD2) ||
        in_range(cp, 0x1CD4, 0x1CE0) || in_range(cp, 0x1CE2, 0x1CE8) || cp == 0x1CED ||
        cp == 0x1CF4 || in_range(cp, 0x1CF8, 0x1CF9) || in_range(cp, 0x1DC0, 0x1DFF) ||
        in_range(cp, 0x20D0, 0x20F0) || in_range(cp, 0x2CEF, 0x2CF1) ||
        in_range(cp, 0x2D7F, 0x2D7F) || in_range(cp, 0x2DE0, 0x2DFF) ||
        in_range(cp, 0x302A, 0x302F) || in_range(cp, 0x3099, 0x309A) ||
        in_range(cp, 0xA66F, 0xA672) || in_range(cp, 0xA674, 0xA67D) ||
        in_range(cp, 0xA69E, 0xA69F) || in_range(cp, 0xA6F0, 0xA6F1) || cp == 0xA802 ||
        cp == 0xA806 || cp == 0xA80B || in_range(cp, 0xA825, 0xA826) || cp == 0xA82C ||
        in_range(cp, 0xA8C4, 0xA8C5) || in_range(cp, 0xA8E0, 0xA8F1) || cp == 0xA8FF ||
        in_range(cp, 0xA926, 0xA92D) || in_range(cp, 0xA947, 0xA951) ||
        in_range(cp, 0xA980, 0xA982) || cp == 0xA9B3 || in_range(cp, 0xA9B6, 0xA9B9) ||
        in_range(cp, 0xA9BC, 0xA9BD) || cp == 0xA9E5 || in_range(cp, 0xAA29, 0xAA2E) ||
        in_range(cp, 0xAA31, 0xAA32) || in_range(cp, 0xAA35, 0xAA36) || cp == 0xAA43 ||
        cp == 0xAA4C || cp == 0xAA7C || cp == 0xAAB0 || in_range(cp, 0xAAB2, 0xAAB4) ||
        in_range(cp, 0xAAB7, 0xAAB8) || in_range(cp, 0xAABE, 0xAABF) || cp == 0xAAC1 ||
        in_range(cp, 0xAAEC, 0xAAED) || cp == 0xAAF6 || cp == 0xABE5 || cp == 0xABE8 ||
        cp == 0xABED || cp == 0xFB1E || in_range(cp, 0xFE00, 0xFE0F) ||
        in_range(cp, 0xFE20, 0xFE2F) || in_range(cp, 0x101FD, 0x101FD) ||
        in_range(cp, 0x102E0, 0x102E0) || in_range(cp, 0x10376, 0x1037A) ||
        in_range(cp, 0x10A01, 0x10A03) || in_range(cp, 0x10A05, 0x10A06) ||
        in_range(cp, 0x10A0C, 0x10A0F) || in_range(cp, 0x10A38, 0x10A3A) ||
        cp == 0x10A3F || in_range(cp, 0x10AE5, 0x10AE6) || in_range(cp, 0x10D24, 0x10D27) ||
        in_range(cp, 0x10EAB, 0x10EAC) || in_range(cp, 0x10F46, 0x10F50) ||
        in_range(cp, 0x10F82, 0x10F85) || in_range(cp, 0x11001, 0x11001) ||
        cp == 0x11070 || in_range(cp, 0x11073, 0x11074) || in_range(cp, 0x1107F, 0x11081) ||
        cp == 0x110B9 || cp == 0x110BA || cp == 0x110C2 || in_range(cp, 0x11100, 0x11102) ||
        in_range(cp, 0x11127, 0x1112B) || in_range(cp, 0x1112D, 0x11134) ||
        cp == 0x11173 || in_range(cp, 0x11180, 0x11181) || cp == 0x111C9 || cp == 0x111CC ||
        cp == 0x111CF || in_range(cp, 0x1122F, 0x11231) || cp == 0x11234 ||
        in_range(cp, 0x11236, 0x11237) || cp == 0x1123E || cp == 0x11241 ||
        cp == 0x112DF || in_range(cp, 0x112E3, 0x112EA) || in_range(cp, 0x11300, 0x11301) ||
        in_range(cp, 0x1133B, 0x1133C) || cp == 0x11340 || in_range(cp, 0x11366, 0x1136C) ||
        in_range(cp, 0x11370, 0x11374) || in_range(cp, 0x11438, 0x1143F) ||
        in_range(cp, 0x11442, 0x11444) || cp == 0x11446 || cp == 0x1145E ||
        in_range(cp, 0x114B3, 0x114B8) || cp == 0x114BA || in_range(cp, 0x114BF, 0x114C0) ||
        in_range(cp, 0x114C2, 0x114C3) || in_range(cp, 0x115B2, 0x115B5) ||
        in_range(cp, 0x115BC, 0x115BD) || in_range(cp, 0x115BF, 0x115C0) ||
        in_range(cp, 0x115DC, 0x115DD) || in_range(cp, 0x11633, 0x1163A) || cp == 0x1163D ||
        in_range(cp, 0x1163F, 0x11640) || cp == 0x116AB || cp == 0x116AD ||
        in_range(cp, 0x116B0, 0x116B5) || cp == 0x116B7 || in_range(cp, 0x1171D, 0x1171F) ||
        in_range(cp, 0x11722, 0x11725) || in_range(cp, 0x11727, 0x1172B) ||
        in_range(cp, 0x1182F, 0x11837) || in_range(cp, 0x11839, 0x1183A) ||
        in_range(cp, 0x1193B, 0x1193C) || cp == 0x1193E || cp == 0x11943 ||
        in_range(cp, 0x119D4, 0x119D7) || in_range(cp, 0x119DA, 0x119DB) || cp == 0x119E0 ||
        in_range(cp, 0x11A01, 0x11A0A) || in_range(cp, 0x11A33, 0x11A38) ||
        in_range(cp, 0x11A3B, 0x11A3E) || cp == 0x11A47 || in_range(cp, 0x11A51, 0x11A56) ||
        in_range(cp, 0x11A59, 0x11A5B) || in_range(cp, 0x11A8A, 0x11A96) ||
        in_range(cp, 0x11A98, 0x11A99) || in_range(cp, 0x11C30, 0x11C36) ||
        in_range(cp, 0x11C38, 0x11C3D) || cp == 0x11C3F || in_range(cp, 0x11C92, 0x11CA7) ||
        in_range(cp, 0x11CAA, 0x11CB0) || in_range(cp, 0x11CB2, 0x11CB3) ||
        in_range(cp, 0x11CB5, 0x11CB6) || in_range(cp, 0x11D31, 0x11D36) || cp == 0x11D3A ||
        in_range(cp, 0x11D3C, 0x11D3D) || in_range(cp, 0x11D3F, 0x11D45) || cp == 0x11D47 ||
        in_range(cp, 0x11D90, 0x11D91) || cp == 0x11D95 || cp == 0x11D97 ||
        in_range(cp, 0x11EF3, 0x11EF4) || in_range(cp, 0x16AF0, 0x16AF4) ||
        in_range(cp, 0x16B30, 0x16B36) || cp == 0x16F4F || in_range(cp, 0x16F8F, 0x16F92) ||
        in_range(cp, 0x16FE4, 0x16FE4) || in_range(cp, 0x1BC9D, 0x1BC9E) ||
        in_range(cp, 0x1CF00, 0x1CF2D) || in_range(cp, 0x1CF30, 0x1CF46) ||
        in_range(cp, 0x1D167, 0x1D169) || in_range(cp, 0x1D17B, 0x1D182) ||
        in_range(cp, 0x1D185, 0x1D18B) || in_range(cp, 0x1D1AA, 0x1D1AD) ||
        in_range(cp, 0x1D242, 0x1D244) || in_range(cp, 0x1DA00, 0x1DA36) ||
        in_range(cp, 0x1DA3B, 0x1DA6C) || cp == 0x1DA75 || cp == 0x1DA84 ||
        in_range(cp, 0x1DA9B, 0x1DA9F) || in_range(cp, 0x1DAA1, 0x1DAAF) ||
        in_range(cp, 0x1E000, 0x1E006) || in_range(cp, 0x1E008, 0x1E018) ||
        in_range(cp, 0x1E01B, 0x1E021) || in_range(cp, 0x1E023, 0x1E024) ||
        in_range(cp, 0x1E026, 0x1E02A) || in_range(cp, 0x1E08F, 0x1E08F) ||
        in_range(cp, 0x1E130, 0x1E136) || cp == 0x1E2AE || in_range(cp, 0x1E2EC, 0x1E2EF) ||
        in_range(cp, 0x1E8D0, 0x1E8D6) || in_range(cp, 0x1E944, 0x1E94A) ||
        in_range(cp, 0x1F3FB, 0x1F3FF) ||  // Emoji modifiers (skin tones)
        in_range(cp, 0xE0020, 0xE007F) ||  // Tags
        in_range(cp, 0xE0100, 0xE01EF)) {  // Variation selectors supplement
        return true;
    }
    return false;
}

bool is_spacing_mark(char32_t cp) {
    // Sparse SpacingMark coverage for Indic / SE Asian; enough that unknown stays Other.
    if (in_range(cp, 0x0903, 0x0903) || in_range(cp, 0x093B, 0x093B) ||
        in_range(cp, 0x093E, 0x0940) || in_range(cp, 0x0949, 0x094C) ||
        in_range(cp, 0x094E, 0x094F) || in_range(cp, 0x0982, 0x0983) ||
        in_range(cp, 0x09BE, 0x09C0) || in_range(cp, 0x09D7, 0x09D7) ||
        in_range(cp, 0x0A03, 0x0A03) || in_range(cp, 0x0A3E, 0x0A40) ||
        in_range(cp, 0x0A83, 0x0A83) || in_range(cp, 0x0ABE, 0x0AC0) ||
        in_range(cp, 0x0AC9, 0x0AC9) || in_range(cp, 0x0ACB, 0x0ACC) ||
        in_range(cp, 0x0B02, 0x0B03) || in_range(cp, 0x0B3E, 0x0B3E) ||
        in_range(cp, 0x0B40, 0x0B40) || in_range(cp, 0x0B57, 0x0B57) ||
        in_range(cp, 0x0BBE, 0x0BBF) || in_range(cp, 0x0BC1, 0x0BC2) ||
        in_range(cp, 0x0BD7, 0x0BD7) || in_range(cp, 0x0C01, 0x0C03) ||
        in_range(cp, 0x0C41, 0x0C44) || in_range(cp, 0x0C82, 0x0C83) ||
        in_range(cp, 0x0CBE, 0x0CBE) || in_range(cp, 0x0CC0, 0x0CC1) ||
        in_range(cp, 0x0CC3, 0x0CC4) || in_range(cp, 0x0CC7, 0x0CC8) ||
        in_range(cp, 0x0CCA, 0x0CCB) || in_range(cp, 0x0CD5, 0x0CD6) ||
        in_range(cp, 0x0D02, 0x0D03) || in_range(cp, 0x0D3E, 0x0D40) ||
        in_range(cp, 0x0D57, 0x0D57) || in_range(cp, 0x0D82, 0x0D83) ||
        in_range(cp, 0x0DCF, 0x0DD1) || in_range(cp, 0x0DD8, 0x0DDE) ||
        in_range(cp, 0x0DF2, 0x0DF3) || in_range(cp, 0x0F3E, 0x0F3F) ||
        in_range(cp, 0x0F7F, 0x0F7F) || in_range(cp, 0x102B, 0x102C) ||
        in_range(cp, 0x1031, 0x1031) || in_range(cp, 0x1038, 0x1038) ||
        in_range(cp, 0x103B, 0x103C) || in_range(cp, 0x1056, 0x1057) ||
        in_range(cp, 0x17B6, 0x17B6) || in_range(cp, 0x17BE, 0x17C5) ||
        in_range(cp, 0x17C7, 0x17C8) || in_range(cp, 0x1923, 0x1926) ||
        in_range(cp, 0x1929, 0x192B) || in_range(cp, 0x1930, 0x1931) ||
        in_range(cp, 0x1933, 0x1938) || in_range(cp, 0x1A19, 0x1A1A) ||
        in_range(cp, 0x1A55, 0x1A55) || in_range(cp, 0x1A57, 0x1A57) ||
        in_range(cp, 0x1A61, 0x1A61) || in_range(cp, 0x1A63, 0x1A64) ||
        in_range(cp, 0x1A6D, 0x1A72) || in_range(cp, 0x1B04, 0x1B04) ||
        in_range(cp, 0x1B35, 0x1B35) || in_range(cp, 0x1B3B, 0x1B3B) ||
        in_range(cp, 0x1B3D, 0x1B41) || in_range(cp, 0x1B43, 0x1B44) ||
        in_range(cp, 0x1B82, 0x1B82) || in_range(cp, 0x1BA1, 0x1BA1) ||
        in_range(cp, 0x1BA6, 0x1BA7) || in_range(cp, 0x1BAA, 0x1BAA) ||
        in_range(cp, 0x1BE7, 0x1BE7) || in_range(cp, 0x1BEA, 0x1BEC) ||
        in_range(cp, 0x1BEE, 0x1BEE) || in_range(cp, 0x1BF2, 0x1BF3) ||
        in_range(cp, 0x1C24, 0x1C2B) || in_range(cp, 0x1C34, 0x1C35) ||
        in_range(cp, 0x1CE1, 0x1CE1) || in_range(cp, 0x1CF7, 0x1CF7) ||
        in_range(cp, 0xA823, 0xA824) || in_range(cp, 0xA827, 0xA827) ||
        in_range(cp, 0xA880, 0xA881) || in_range(cp, 0xA8B4, 0xA8C3) ||
        in_range(cp, 0xA952, 0xA953) || in_range(cp, 0xA983, 0xA983) ||
        in_range(cp, 0xA9B4, 0xA9B5) || in_range(cp, 0xA9BA, 0xA9BB) ||
        in_range(cp, 0xA9BE, 0xA9C0) || in_range(cp, 0xAA2F, 0xAA30) ||
        in_range(cp, 0xAA33, 0xAA34) || in_range(cp, 0xAA4D, 0xAA4D) ||
        in_range(cp, 0xAAEB, 0xAAEB) || in_range(cp, 0xAAEE, 0xAAEF) ||
        in_range(cp, 0xAAF5, 0xAAF5) || in_range(cp, 0xABE3, 0xABE4) ||
        in_range(cp, 0xABE6, 0xABE7) || in_range(cp, 0xABEC, 0xABEC)) {
        return true;
    }
    return false;
}

bool is_prepend(char32_t cp) {
    // Sparse Prepend (e.g. Indic number signs); unknown stays Other.
    if (cp == 0x0600 || cp == 0x0601 || cp == 0x0602 || cp == 0x0603 || cp == 0x0604 ||
        cp == 0x0605 || cp == 0x06DD || cp == 0x070F || cp == 0x0890 || cp == 0x0891 ||
        cp == 0x08E2 || cp == 0x0D4E || cp == 0x110BD || cp == 0x110CD ||
        in_range(cp, 0x111C2, 0x111C3) || cp == 0x1193F || cp == 0x11941 ||
        cp == 0x11A3A || in_range(cp, 0x11A84, 0x11A89) || cp == 0x11D46) {
        return true;
    }
    return false;
}

bool is_control(char32_t cp) {
    if (cp == 0x000D || cp == 0x000A) {
        return false;  // CR/LF classified separately
    }
    if (cp <= 0x001F || cp == 0x007F || in_range(cp, 0x0080, 0x009F)) {
        return true;
    }
    // Line/paragraph separators and a few format controls treated as Control in GCB.
    if (cp == 0x2028 || cp == 0x2029 || cp == 0x200B /* WJ excluded; ZWSP is Control in GCB */) {
        return true;
    }
    if (in_range(cp, 0x2060, 0x2064) || in_range(cp, 0x2066, 0x206F) || cp == 0xFEFF ||
        in_range(cp, 0xFFF0, 0xFFFB) || in_range(cp, 0x1BCA0, 0x1BCA3) ||
        in_range(cp, 0x1D173, 0x1D17A) || in_range(cp, 0xE0000, 0xE001F) ||
        in_range(cp, 0xE0080, 0xE00FF) || in_range(cp, 0xE01F0, 0xE0FFF)) {
        return true;
    }
    return false;
}

Prop hangul_syllable(char32_t cp) {
    // Hangul syllables AC00..D7A3: LV if (cp-AC00) % 28 == 0, else LVT.
    if (in_range(cp, 0xAC00, 0xD7A3)) {
        return ((cp - 0xAC00) % 28 == 0) ? Prop::LV : Prop::LVT;
    }
    return Prop::Other;
}

}  // namespace

GraphemeBreakProp grapheme_prop(char32_t cp) {
    if (cp == 0x000D) {
        return Prop::CR;
    }
    if (cp == 0x000A) {
        return Prop::LF;
    }
    if (cp == 0x200D) {
        return Prop::ZWJ;
    }
    if (in_range(cp, 0x1F1E6, 0x1F1FF)) {
        return Prop::Regional_Indicator;
    }
    // Hangul jamo
    if (in_range(cp, 0x1100, 0x115F) || in_range(cp, 0xA960, 0xA97C)) {
        return Prop::L;
    }
    if (in_range(cp, 0x1160, 0x11A7) || in_range(cp, 0xD7B0, 0xD7C6)) {
        return Prop::V;
    }
    if (in_range(cp, 0x11A8, 0x11FF) || in_range(cp, 0xD7CB, 0xD7FB)) {
        return Prop::T;
    }
    {
        const Prop h = hangul_syllable(cp);
        if (h != Prop::Other) {
            return h;
        }
    }
    if (is_extend(cp)) {
        return Prop::Extend;
    }
    if (is_spacing_mark(cp)) {
        return Prop::SpacingMark;
    }
    if (is_prepend(cp)) {
        return Prop::Prepend;
    }
    if (is_control(cp)) {
        return Prop::Control;
    }
    if (is_extended_pictographic(cp)) {
        return Prop::Extended_Pictographic;
    }
    return Prop::Other;
}

namespace {

struct CpAt {
    std::size_t begin = 0;
    std::size_t end = 0;
    char32_t cp = 0;
    Prop prop = Prop::Other;
};

bool decode_at(const std::string& text, std::size_t at, CpAt& out) {
    if (at >= text.size()) {
        return false;
    }
    out.begin = at;
    std::size_t i = at;
    out.cp = decode_cp(text, i);
    out.end = i;
    out.prop = grapheme_prop(out.cp);
    return true;
}

bool decode_before(const std::string& text, std::size_t at, CpAt& out) {
    if (at == 0 || at > text.size()) {
        return false;
    }
    const std::size_t begin = utf8_cp_prev(text, at);
    return decode_at(text, begin, out);
}

// Collect props of codepoints strictly before `at` (oldest → newest) for GB11 / GB12 lookback.
std::vector<Prop> props_before(const std::string& text, std::size_t at) {
    std::vector<Prop> props;
    std::size_t i = 0;
    while (i < at) {
        CpAt cur;
        if (!decode_at(text, i, cur) || cur.end > at) {
            break;
        }
        props.push_back(cur.prop);
        i = cur.end;
    }
    return props;
}

bool should_break(Prop a, Prop b, const std::vector<Prop>& before_a) {
    // GB3: CR × LF
    if (a == Prop::CR && b == Prop::LF) {
        return false;
    }
    // GB4: (Control | CR | LF) ÷
    if (a == Prop::Control || a == Prop::CR || a == Prop::LF) {
        return true;
    }
    // GB5: ÷ (Control | CR | LF)
    if (b == Prop::Control || b == Prop::CR || b == Prop::LF) {
        return true;
    }
    // GB6: L × (L | V | LV | LVT)
    if (a == Prop::L &&
        (b == Prop::L || b == Prop::V || b == Prop::LV || b == Prop::LVT)) {
        return false;
    }
    // GB7: (LV | V) × (V | T)
    if ((a == Prop::LV || a == Prop::V) && (b == Prop::V || b == Prop::T)) {
        return false;
    }
    // GB8: (LVT | T) × T
    if ((a == Prop::LVT || a == Prop::T) && b == Prop::T) {
        return false;
    }
    // GB9: × (Extend | ZWJ)
    if (b == Prop::Extend || b == Prop::ZWJ) {
        return false;
    }
    // GB9a: × SpacingMark
    if (b == Prop::SpacingMark) {
        return false;
    }
    // GB9b: Prepend ×
    if (a == Prop::Prepend) {
        return false;
    }
    // GB11: Extended_Pictographic Extend* ZWJ × Extended_Pictographic
    if (a == Prop::ZWJ && b == Prop::Extended_Pictographic) {
        // Walk left: ZWJ is `a`; before that, zero+ Extend, then Extended_Pictographic.
        std::size_t i = before_a.size();
        while (i > 0 && before_a[i - 1] == Prop::Extend) {
            --i;
        }
        if (i > 0 && before_a[i - 1] == Prop::Extended_Pictographic) {
            return false;
        }
    }
    // GB12 / GB13: sot (RI RI)* RI × RI  — do not break within an RI pair.
    if (a == Prop::Regional_Indicator && b == Prop::Regional_Indicator) {
        std::size_t ri_count = 1;  // include `a`
        std::size_t i = before_a.size();
        while (i > 0 && before_a[i - 1] == Prop::Regional_Indicator) {
            ++ri_count;
            --i;
        }
        // Odd count of RI before `b` ⇒ no break (pair with previous RI).
        if ((ri_count % 2) == 1) {
            return false;
        }
    }
    // GB999: ÷ Any
    return true;
}

}  // namespace

bool grapheme_is_boundary(const std::string& text, std::size_t at) {
    if (at == 0 || at >= text.size()) {
        return true;
    }
    // Require `at` to sit on a codepoint boundary; otherwise treat as non-boundary for safety.
    if ((static_cast<unsigned char>(text[at]) & 0xc0) == 0x80) {
        return false;
    }
    CpAt left;
    CpAt right;
    if (!decode_before(text, at, left) || !decode_at(text, at, right)) {
        return true;
    }
    if (left.end != at) {
        return true;
    }
    const std::vector<Prop> before = props_before(text, left.begin);
    return should_break(left.prop, right.prop, before);
}

std::size_t grapheme_next(const std::string& text, std::size_t index) {
    if (index >= text.size()) {
        return text.size();
    }
    // Advance at least one codepoint, then continue while no boundary.
    std::size_t i = index;
    (void)decode_cp(text, i);
    while (i < text.size() && !grapheme_is_boundary(text, i)) {
        (void)decode_cp(text, i);
    }
    return i;
}

std::size_t grapheme_prev(const std::string& text, std::size_t index) {
    if (index == 0) {
        return 0;
    }
    if (index > text.size()) {
        index = text.size();
    }
    std::size_t i = utf8_cp_prev(text, index);
    while (i > 0 && !grapheme_is_boundary(text, i)) {
        i = utf8_cp_prev(text, i);
    }
    return i;
}

}  // namespace text
}  // namespace zui