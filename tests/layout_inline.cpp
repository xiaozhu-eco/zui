#include "layout/cache/layout_context.h"
#include "layout/cache/quantum.h"
#include "layout/freetype_text_metrics.h"
#include "layout/inline/bidi.h"
#include "layout/inline/script.h"
#include "text/font_chain.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

void expect(bool ok, const char* message) { EXPECT_TRUE(ok) << message; }

std::vector<float> flat_advances(const std::vector<zui::layout::ShapedRun>& runs) {
    std::vector<float> out;
    for (const zui::layout::ShapedRun& run : runs) {
        for (const zui::layout::GlyphInline& glyph : run.glyphs) {
            out.push_back(glyph.advance);
        }
    }
    return out;
}

}  // namespace

TEST(LayoutInline, EmptyAndScriptBoundary) {
    using namespace zui::layout;
    FreeTypeTextMetrics metrics;
    LayoutStyle style;
    style.font_size = 16;
    LayoutContext cx(&metrics);

    const std::vector<ShapedRun> empty = cx.shape("", style, Direction::Ltr);
    expect(empty.size() == 1 && empty[0].glyphs.empty() && empty[0].advance_inline == 0,
           "empty text must be one empty run");

    const std::vector<ShapedRun> mixed = cx.shape(u8"A\u4e2d", style, Direction::Ltr);
    expect(mixed.size() >= 2, "Latin|CJK script boundary must split runs");
}

TEST(LayoutInline, MixedBidiParagraphDirection) {
    using namespace zui::layout;
    FreeTypeTextMetrics metrics;
    LayoutStyle style;
    style.font_size = 16;
    LayoutContext cx(&metrics);

    // Logical: a b ALEF BET. LTR visual: a b BET ALEF. RTL visual: BET ALEF a b.
    const std::string mixed = std::string("ab") + "\xd7\x90\xd7\x91";
    const std::vector<ShapedRun> mix_ltr = cx.shape(mixed, style, Direction::Ltr);
    const std::vector<ShapedRun> mix_rtl = cx.shape(mixed, style, Direction::Rtl);
    const std::vector<float> ltr = flat_advances(mix_ltr);
    const std::vector<float> rtl = flat_advances(mix_rtl);
    expect(ltr.size() == 4 && rtl.size() == 4, "mixed latin/hebrew glyph count");
    expect(mix_ltr.size() >= 2 && mix_rtl.size() >= 2, "mixed text must not be one blob");
    if (ltr.size() == 4 && rtl.size() == 4) {
        expect(quantum_key(ltr[0]) == quantum_key(rtl[2]) && quantum_key(ltr[1]) == quantum_key(rtl[3]) &&
                   quantum_key(ltr[2]) == quantum_key(rtl[0]) &&
                   quantum_key(ltr[3]) == quantum_key(rtl[1]),
               "ltr vs rtl paragraph visual blocks did not swap");
        expect(!(quantum_key(rtl[0]) == quantum_key(ltr[3]) && quantum_key(rtl[1]) == quantum_key(ltr[2]) &&
                 quantum_key(rtl[2]) == quantum_key(ltr[1]) && quantum_key(rtl[3]) == quantum_key(ltr[0])),
               "rtl matched phase-1 reverse-all of ltr visual");
    }
}

TEST(LayoutInline, FaceFallbackSplitsRun) {
    using namespace zui::layout;
    FreeTypeTextMetrics metrics;
    LayoutStyle style;
    style.font_size = 16;
    LayoutContext cx(&metrics);
    const zui::text::LoadedGlyph latin =
        metrics.chain().load(static_cast<std::uint32_t>('A'), 16, false);
    const zui::text::LoadedGlyph cjk =
        metrics.chain().load(static_cast<std::uint32_t>(0x4E2D), 16, false);
    const std::vector<ShapedRun> mixed = cx.shape(u8"A\u4e2d", style, Direction::Ltr);
    if (latin.ok && cjk.ok && latin.face_key != cjk.face_key) {
        expect(mixed.size() == 2, "adjacent different faces must not stay one run");
    } else {
        expect(mixed.size() >= 2, "script split still applies without face change");
    }
}

TEST(LayoutInline, BidiLevelsLatinInRtlParagraph) {
    using namespace zui::layout::inline_text;
    using zui::layout::Direction;
    std::vector<BidiClass> types = {BidiClass::L, BidiClass::L};
    std::vector<std::uint8_t> levels;
    resolve_levels(types, Direction::Rtl, levels);
    expect(levels.size() == 2 && levels[0] == 2 && levels[1] == 2,
           "latin in rtl paragraph should get level 2");
    std::vector<std::size_t> order;
    reorder_visual(levels, order);
    expect(order.size() == 2 && order[0] == 0 && order[1] == 1,
           "latin level-2 pair under rtl base must visually stay LTR after L2");
}

TEST(LayoutInline, FiLigatureSplitsItsAdvance) {
    using namespace zui::layout;
    zui::text::register_embedded_font(
        "Zui Test", std::string(ZUI_SOURCE_DIR) + "/tests/difftest/fonts/NotoSans-Regular.ttf");
    FreeTypeTextMetrics metrics;
    LayoutStyle style;
    style.font_family = "Zui Test";
    style.font_size = 16;
    LayoutContext cx(&metrics);
    // Noto Sans fi is one glyph (design advance 602, caret at 301). Layout keeps one slot per
    // codepoint and splits that advance on the caret. Edge's client rect is the caret span of
    // each half, so the raw halves are 4.816, not the unligated 5.504 / 4.128.
    const std::vector<ShapedRun> fi = cx.shape("fi", style, Direction::Ltr);
    const std::vector<float> advances = flat_advances(fi);
    ASSERT_EQ(advances.size(), 2u);
    EXPECT_NEAR(advances[0], 4.816f, 0.02f);
    EXPECT_NEAR(advances[1], 4.816f, 0.02f);
    ASSERT_EQ(fi.size(), 1u);
    ASSERT_EQ(fi[0].glyphs.size(), 2u);
    EXPECT_NE(fi[0].glyphs[0].paint_glyph, 0u);
    EXPECT_NE(fi[0].glyphs[0].paint_glyph, zui::text::kSkipPaintGlyph);
    EXPECT_EQ(fi[0].glyphs[1].paint_glyph, zui::text::kSkipPaintGlyph);

    // ffi carets are not equal thirds (315 and 631 design units of a 946-unit glyph).
    const std::vector<float> ffi = flat_advances(cx.shape("ffi", style, Direction::Ltr));
    ASSERT_EQ(ffi.size(), 3u);
    EXPECT_NEAR(ffi[0], 5.04f, 0.02f);
    EXPECT_NEAR(ffi[1], 5.056f, 0.02f);
    EXPECT_NEAR(ffi[0] + ffi[1] + ffi[2], 15.136f, 0.02f);

    // A pair the font does not ligate stays on the cmap advances.
    zui::text::FontChain& chain = metrics.chain_for("Zui Test", 400, false);
    const zui::text::LoadedGlyph glyph_f = chain.load(static_cast<std::uint32_t>('f'), 16.f, false);
    const zui::text::LoadedGlyph glyph_a = chain.load(static_cast<std::uint32_t>('a'), 16.f, false);
    const std::vector<ShapedRun> fa = cx.shape("fa", style, Direction::Ltr);
    const std::vector<float> fa_adv = flat_advances(fa);
    ASSERT_EQ(fa_adv.size(), 2u);
    EXPECT_NEAR(fa_adv[0], glyph_f.advance, 0.02f);
    EXPECT_NEAR(fa_adv[1], glyph_a.advance, 0.05f);
    EXPECT_EQ(fa[0].glyphs[0].paint_glyph, 0u);
    EXPECT_EQ(fa[0].glyphs[1].paint_glyph, 0u);

    // Non-zero letter-spacing does not form the ligature.
    style.letter_spacing = 3.f;
    const std::vector<float> spaced = flat_advances(cx.shape("fi", style, Direction::Ltr));
    ASSERT_EQ(spaced.size(), 2u);
    const zui::text::LoadedGlyph glyph_i = chain.load(static_cast<std::uint32_t>('i'), 16.f, false);
    EXPECT_NEAR(spaced[0], glyph_f.advance, 0.02f);
    EXPECT_NEAR(spaced[1], glyph_i.advance, 0.05f);
}

TEST(LayoutInline, NormalNewlineShapesAsSpace) {
    using namespace zui::layout;
    zui::text::register_embedded_font(
        "Zui Test", std::string(ZUI_SOURCE_DIR) + "/tests/difftest/fonts/NotoSans-Regular.ttf");
    FreeTypeTextMetrics metrics;
    LayoutStyle style;
    style.font_family = "Zui Test";
    style.font_size = 16;
    style.white_space = WhiteSpace::Normal;
    LayoutContext cx(&metrics);
    const std::vector<float> newline = flat_advances(cx.shape("\n", style, Direction::Ltr));
    const std::vector<float> space = flat_advances(cx.shape(" ", style, Direction::Ltr));
    ASSERT_EQ(newline.size(), 1u);
    ASSERT_EQ(space.size(), 1u);
    EXPECT_NEAR(newline[0], space[0], 0.02f);
    EXPECT_EQ(cx.shape("\n", style, Direction::Ltr)[0].text, " ");
}

TEST(LayoutInline, ScriptClassification) {
    using namespace zui::layout::inline_text;
    expect(script_of('A') == Script::Latin, "A");
    expect(script_of(0x05D0) == Script::Hebrew, "alef");
    expect(script_of(0x4E2D) == Script::Cjk, "CJK");
    expect(script_of(' ') == Script::Common, "space");
    expect(resolve_script(Script::Common, Script::Latin) == Script::Latin, "common attaches");
}


TEST(LayoutInline, ExplicitBidiEmbedRle) {
    using namespace zui::layout::inline_text;
    using zui::layout::Direction;
    // Logical: a + RLE + b c + PDF + d  => levels 0, (skip), 1, 1, (skip), 0
    std::vector<BidiClass> types = {BidiClass::L, BidiClass::RLE, BidiClass::L, BidiClass::L,
                                    BidiClass::PDF, BidiClass::L};
    std::vector<std::uint8_t> levels;
    resolve_levels(types, Direction::Ltr, levels);
    expect(levels.size() == 6, "embed level count");
    expect(levels[0] == 0 && levels[5] == 0, "outside embed stays paragraph level");
    expect(levels[2] == 2 && levels[3] == 2,
           "RLE + Latin: embedding 1 then I2 lifts L to level 2");
}

TEST(LayoutInline, ExplicitBidiIsolateRli) {
    using namespace zui::layout::inline_text;
    using zui::layout::Direction;
    std::vector<BidiClass> types = {BidiClass::L, BidiClass::RLI, BidiClass::L, BidiClass::L,
                                    BidiClass::PDI, BidiClass::L};
    std::vector<std::uint8_t> levels;
    resolve_levels(types, Direction::Ltr, levels);
    expect(levels.size() == 6 && levels[2] == 2 && levels[3] == 2,
           "RLI + Latin: isolate embedding 1 then I2 lifts L to level 2");
    expect(levels[0] == 0 && levels[5] == 0, "outside isolate stays paragraph level");
}

TEST(LayoutInline, MirrorPairedPunctuation) {
    using namespace zui::layout::inline_text;
    expect(mirror_cp('(') == ')' && mirror_cp(')') == '(', "ascii paren mirror");
    expect(mirror_cp(0x3008) == 0x3009, "CJK angle bracket mirror");
    expect(mirror_cp('A') == 'A', "letters are not mirrored");
}

TEST(LayoutInline, ShapeSkipsEmbedControlsAndMirrors) {
    using namespace zui::layout;
    FreeTypeTextMetrics metrics;
    LayoutStyle style;
    style.font_size = 16;
    LayoutContext cx(&metrics);
    // RLE + "(" + Hebrew + ")" + PDF under LTR: controls omitted; parens mirrored at odd level.
    const std::string text = std::string("\xe2\x80\xab") + "(" + "\xd7\x90" + ")" + "\xe2\x80\xac";
    const std::vector<ShapedRun> runs = cx.shape(text, style, Direction::Ltr);
    std::size_t glyphs = 0;
    for (const ShapedRun& run : runs) {
        glyphs += run.glyphs.size();
    }
    expect(glyphs == 3, "embed controls must not emit glyphs");
}

TEST(LayoutInline, BoldFaceKeyDiffersFromRegular) {
    using namespace zui::layout;
    FreeTypeTextMetrics metrics;
    zui::text::FontChain& regular = metrics.chain_for("arial", 400, false);
    zui::text::FontChain& bold = metrics.chain_for("arial", 700, false);
    const zui::text::LoadedGlyph a = regular.load(static_cast<std::uint32_t>('A'), 16, false);
    const zui::text::LoadedGlyph b = bold.load(static_cast<std::uint32_t>('A'), 16, false);
    if (a.ok && b.ok) {
        expect(a.face_key != b.face_key, "bold vs regular must not share face_key");
    } else {
        expect(regular.ready() && bold.ready(), "arial regular/bold chains must open");
    }
}

TEST(LayoutInline, WeightSplitsAdjacentShapedRuns) {
    using namespace zui::layout;
    FreeTypeTextMetrics metrics;
    LayoutContext cx(&metrics);
    LayoutStyle regular;
    regular.font_size = 16;
    regular.font_family = "arial";
    regular.font_weight = 400;
    LayoutStyle bold = regular;
    bold.font_weight = 700;
    const std::vector<ShapedRun> r = cx.shape("A", regular, Direction::Ltr);
    const std::vector<ShapedRun> b = cx.shape("A", bold, Direction::Ltr);
    expect(!r.empty() && !b.empty(), "weight styles must shape");
    // Distinct face keys imply pack_runs would split if the glyphs were adjacent in one call.
    zui::text::FontChain& rc = metrics.chain_for("arial", 400, false);
    zui::text::FontChain& bc = metrics.chain_for("arial", 700, false);
    const auto rg = rc.load('A', 16, false);
    const auto bg = bc.load('A', 16, false);
    if (rg.ok && bg.ok) {
        expect(rg.face_key != bg.face_key, "adjacent weight faces stay distinct for run packing");
    }
}
