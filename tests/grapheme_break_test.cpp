#include "text/grapheme_break.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <vector>

namespace {

using zui::text::grapheme_is_boundary;
using zui::text::grapheme_next;
using zui::text::grapheme_prev;

/// Collect every grapheme-cluster boundary in @p text (including 0 and size).
std::vector<std::size_t> boundaries(const std::string& text) {
    std::vector<std::size_t> out;
    out.push_back(0);
    for (std::size_t i = grapheme_next(text, 0); i < text.size(); i = grapheme_next(text, i)) {
        out.push_back(i);
    }
    if (out.back() != text.size()) {
        out.push_back(text.size());
    }
    return out;
}

void expect_clusters(const std::string& text, const std::vector<std::string>& clusters) {
    std::size_t at = 0;
    for (const std::string& cluster : clusters) {
        ASSERT_LE(at + cluster.size(), text.size()) << "cluster overrun at " << at;
        EXPECT_EQ(text.substr(at, cluster.size()), cluster) << "at byte " << at;
        const std::size_t next = grapheme_next(text, at);
        EXPECT_EQ(next, at + cluster.size()) << "grapheme_next from " << at;
        if (next > 0) {
            EXPECT_EQ(grapheme_prev(text, next), at) << "grapheme_prev from " << next;
        }
        EXPECT_TRUE(grapheme_is_boundary(text, at));
        at = next;
    }
    EXPECT_EQ(at, text.size());
    EXPECT_TRUE(grapheme_is_boundary(text, text.size()));
}

}  // namespace

TEST(GraphemeBreak, AsciiSingleScalars) {
    const std::string text = "abc";
    expect_clusters(text, {"a", "b", "c"});
    EXPECT_EQ(grapheme_next(text, 0), 1u);
    EXPECT_EQ(grapheme_next(text, 1), 2u);
    EXPECT_EQ(grapheme_next(text, 2), 3u);
    EXPECT_EQ(grapheme_next(text, 3), 3u);
    EXPECT_EQ(grapheme_prev(text, 3), 2u);
    EXPECT_EQ(grapheme_prev(text, 2), 1u);
    EXPECT_EQ(grapheme_prev(text, 1), 0u);
    EXPECT_EQ(grapheme_prev(text, 0), 0u);
    // BMP multi-byte that is still one grapheme per scalar (CJK).
    const std::string cjk = u8"中文";
    expect_clusters(cjk, {u8"中", u8"文"});
}

TEST(GraphemeBreak, CombiningAcute) {
    // e + COMBINING ACUTE ACCENT (U+0301) = one grapheme cluster.
    const std::string text = "e\xCC\x81x";
    expect_clusters(text, {"e\xCC\x81", "x"});
    EXPECT_EQ(grapheme_next(text, 0), 3u);
    EXPECT_EQ(grapheme_prev(text, 3), 0u);
}

TEST(GraphemeBreak, EmojiZwjFamily) {
    // Man + ZWJ + Woman + ZWJ + Girl (U+1F468 U+200D U+1F469 U+200D U+1F467).
    const std::string family = u8"👨‍👩‍👧";
    const std::string tail = "y";
    const std::string text = family + tail;
    expect_clusters(text, {family, tail});
    EXPECT_EQ(grapheme_next(text, 0), family.size());
    EXPECT_EQ(grapheme_prev(text, family.size()), 0u);
    EXPECT_EQ(grapheme_next(text, family.size()), text.size());
}

TEST(GraphemeBreak, RegionalIndicatorFlag) {
    // Regional Indicator Symbol Letter U + S (U+1F1FA U+1F1F8) = one flag cluster.
    const std::string flag = u8"🇺🇸";
    const std::string text = flag + "z";
    expect_clusters(text, {flag, "z"});
    EXPECT_EQ(grapheme_next(text, 0), flag.size());
    EXPECT_EQ(grapheme_prev(text, flag.size()), 0u);
    // Two flags in a row: each pair is one cluster (GB12/GB13).
    const std::string us = u8"🇺🇸";
    const std::string cn = u8"🇨🇳";
    const std::string flags = us + cn;
    expect_clusters(flags, {us, cn});
}

TEST(GraphemeBreak, HangulSyllable) {
    // Precomposed Hangul syllable (LVT) is one cluster.
    const std::string syllable = u8"한";
    expect_clusters(syllable + "a", {syllable, "a"});
    // L + V + T jamo sequence forms one cluster (GB6-GB8).
    // HANGUL CHOSEONG HIEUH (U+1112) + JUNGSEONG A (U+1161) + JONGSEONG NIEUN (U+11AB)
    const std::string jamo = u8"\u1112\u1161\u11AB";
    expect_clusters(jamo + "b", {jamo, "b"});
    EXPECT_EQ(grapheme_next(jamo, 0), jamo.size());
    EXPECT_EQ(grapheme_prev(jamo, jamo.size()), 0u);
}
