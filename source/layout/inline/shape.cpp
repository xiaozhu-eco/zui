#include "layout/inline/shape.h"

#include "layout/freetype_text_metrics.h"
#include "layout/inline/bidi.h"
#include "layout/inline/script.h"
#include "text/font_chain.h"
#include "trace/trace.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zui {
namespace layout {
namespace inline_text {

namespace {

struct CpInfo {
    std::uint32_t cp = 0;
    std::size_t byte_begin = 0;
    std::size_t byte_end = 0;
    BidiClass bidi = BidiClass::ON;
    Script script = Script::Common;
    Script resolved_script = Script::Common;
    std::uint8_t level = 0;
};

struct GlyphOut {
    GlyphInline glyph;
    std::uint32_t face = 0;
    Script script = Script::Common;
    std::uint8_t level = 0;
    std::size_t logical_index = 0;
    // View into the source text, or into `transform_utf8` for a mapped codepoint. `pack_runs`
    // copies it into the owned run before either buffer is reused.
    std::string_view chars;
};

/// CSS Text 3 section 2.1 simple case mappings. Latin-1, Latin Extended-A, Greek and Cyrillic cover
/// every cased script the engine's fonts actually carry; a codepoint outside them is left as-is.
std::uint32_t to_uppercase(std::uint32_t cp) {
    if (cp >= 'a' && cp <= 'z') {
        return cp - 32;
    }
    if ((cp >= 0xE0 && cp <= 0xF6) || (cp >= 0xF8 && cp <= 0xFE)) {
        return cp - 32;
    }
    if (cp == 0xFF) {
        return 0x178;
    }
    if (cp >= 0x100 && cp <= 0x17F) {
        return (cp & 1u) != 0 ? cp - 1 : cp;
    }
    if (cp >= 0x3B1 && cp <= 0x3C9 && cp != 0x3C2) {
        return cp - 32;
    }
    if (cp == 0x3C2) {
        return 0x3A3;
    }
    if (cp >= 0x430 && cp <= 0x44F) {
        return cp - 32;
    }
    if (cp >= 0x450 && cp <= 0x45F) {
        return cp - 80;
    }
    return cp;
}

std::uint32_t to_lowercase(std::uint32_t cp) {
    if (cp >= 'A' && cp <= 'Z') {
        return cp + 32;
    }
    if ((cp >= 0xC0 && cp <= 0xD6) || (cp >= 0xD8 && cp <= 0xDE)) {
        return cp + 32;
    }
    if (cp == 0x178) {
        return 0xFF;
    }
    if (cp >= 0x100 && cp <= 0x17F) {
        return (cp & 1u) == 0 ? cp + 1 : cp;
    }
    if (cp >= 0x391 && cp <= 0x3A9) {
        return cp + 32;
    }
    if (cp >= 0x410 && cp <= 0x42F) {
        return cp + 32;
    }
    if (cp >= 0x400 && cp <= 0x40F) {
        return cp + 80;
    }
    return cp;
}

/// A word starts at the first cased letter after a separator. Digits and marks are part of a word,
/// so `3d` capitalizes to `3d`, which is also what the browser does.
bool is_word_separator(std::uint32_t cp) {
    if (text::is_space(cp)) {
        return true;
    }
    return (cp >= 0x21 && cp <= 0x2F) || (cp >= 0x3A && cp <= 0x40) || (cp >= 0x5B && cp <= 0x60) ||
           (cp >= 0x7B && cp <= 0x7E);
}

bool is_cased_letter(std::uint32_t cp) {
    return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || (cp >= 0xC0 && cp <= 0x24F) ||
           (cp >= 0x370 && cp <= 0x4FF);
}

/// Mapped codepoints (text-transform, segment-break fold) are encoded here. Reserved to
/// `text.size() * 3 + 8` before any view is taken, so an append cannot move the earlier views.
thread_local std::string transform_utf8;

std::string_view encode_transformed(std::uint32_t value) {
    const std::size_t at = transform_utf8.size();
    if (value < 0x80) {
        transform_utf8.push_back(static_cast<char>(value));
    } else if (value < 0x800) {
        transform_utf8.push_back(static_cast<char>(0xC0 | (value >> 6)));
        transform_utf8.push_back(static_cast<char>(0x80 | (value & 0x3F)));
    } else {
        transform_utf8.push_back(static_cast<char>(0xE0 | (value >> 12)));
        transform_utf8.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
        transform_utf8.push_back(static_cast<char>(0x80 | (value & 0x3F)));
    }
    return std::string_view(transform_utf8.data() + at, transform_utf8.size() - at);
}

void decode(std::string_view text, std::vector<CpInfo>& out) {
    out.clear();
    std::size_t index = 0;
    Script previous_strong = Script::Common;
    while (index < text.size()) {
        CpInfo info;
        info.byte_begin = index;
        info.cp = text::next_cp(text, index);
        info.byte_end = index;
        info.bidi = bidi_class_of(info.cp);
        info.script = script_of(info.cp);
        info.resolved_script = resolve_script(info.script, previous_strong);
        if (is_strong_script(info.script)) {
            previous_strong = info.script;
        } else if (is_strong_script(info.resolved_script)) {
            previous_strong = info.resolved_script;
        }
        out.push_back(info);
    }
    // Second pass: Common at the start that precedes a strong script inherits forward.
    Script next_strong = Script::Common;
    for (std::size_t i = out.size(); i > 0; --i) {
        CpInfo& info = out[i - 1];
        if (is_strong_script(info.script)) {
            next_strong = info.script;
        } else if (info.resolved_script == Script::Common && is_strong_script(next_strong)) {
            info.resolved_script = next_strong;
        }
    }
}

void shape_logical_run(std::string_view text, const std::vector<CpInfo>& cps, std::size_t begin,
                       std::size_t end, const LayoutStyle& style, text::FontChain* chain, bool kern,
                       std::vector<GlyphOut>& glyphs) {
    if (begin >= end) {
        return;
    }
    struct Piece {
        float advance = 0;
        std::uint32_t face = 0;
        std::uint32_t glyph = 0;
        std::uint32_t cmap = 0;
        std::uint32_t shaped_cp = 0;
        bool draw = true;
        bool wrap_after = false;
        bool collapsible = false;
        bool forced_break = false;
        Script script = Script::Common;
        std::uint8_t level = 0;
        std::size_t logical_index = 0;
        std::string_view chars;
    };
    std::vector<Piece> pieces;
    pieces.reserve(end - begin);
    // CSS Text 3 section 2.1: `text-transform` is applied to the text before shaping, so the glyphs,
    // their advances and everything painted from this run are the transformed ones. `capitalize`
    // tracks word starts across the run.
    bool at_word_start = true;
    for (std::size_t i = begin; i < end; ++i) {
        const CpInfo& info = cps[i];
        // X9: embedding/isolate controls and BN do not produce glyphs.
        if (info.bidi == BidiClass::LRE || info.bidi == BidiClass::RLE ||
            info.bidi == BidiClass::LRO || info.bidi == BidiClass::RLO ||
            info.bidi == BidiClass::PDF || info.bidi == BidiClass::LRI ||
            info.bidi == BidiClass::RLI || info.bidi == BidiClass::FSI ||
            info.bidi == BidiClass::PDI || info.bidi == BidiClass::BN) {
            continue;
        }
        Piece piece;
        piece.script = info.resolved_script;
        piece.level = info.level;
        piece.logical_index = i;
        piece.chars = text.substr(info.byte_begin, info.byte_end - info.byte_begin);
        std::uint32_t shaped_source = info.cp;
        if (style.text_transform == TextTransform::Uppercase) {
            shaped_source = to_uppercase(shaped_source);
        } else if (style.text_transform == TextTransform::Lowercase) {
            shaped_source = to_lowercase(shaped_source);
        } else if (style.text_transform == TextTransform::Capitalize) {
            if (at_word_start && is_cased_letter(shaped_source)) {
                shaped_source = to_uppercase(shaped_source);
            }
        }
        if (style.text_transform == TextTransform::Capitalize) {
            // A word starts again after a separator. A word that begins with something uncased
            // (`3d`) never capitalizes its later letters, so any other character closes the word.
            if (is_word_separator(info.cp)) {
                at_word_start = true;
            } else {
                at_word_start = false;
            }
        }
        // CSS Text 3 segment-break transformation: under `normal` / `nowrap` a newline is
        // collapsible white space and must shape as U+0020. Loading the newline itself uses the
        // .notdef advance, so the one glyph a collapsed `\n    ` run keeps is wider than a space
        // (`inline-text` `#justify` line 2).
        const bool keeps_breaks = style.white_space == WhiteSpace::Pre ||
                                  style.white_space == WhiteSpace::PreWrap ||
                                  style.white_space == WhiteSpace::PreLine ||
                                  style.white_space == WhiteSpace::BreakSpaces;
        if (!keeps_breaks && (info.cp == '\n' || info.cp == '\r' || info.cp == '\f')) {
            shaped_source = static_cast<std::uint32_t>(' ');
        }
        if (shaped_source != info.cp) {
            piece.chars = encode_transformed(shaped_source);
        }
        // `keeps_breaks` is decided above, with the segment-break fold. A preserved newline is a
        // forced break; a folded one is already a collapsible space.
        piece.forced_break = keeps_breaks && (info.cp == '\n' || info.cp == '\r');
        piece.collapsible = text::is_space(info.cp) && !piece.forced_break;
        // CSS Text 3: word-break / overflow-wrap control soft wrap opportunities.
        bool wrap = piece.collapsible || piece.forced_break;
        if (style.word_break == WordBreak::BreakAll ||
            style.overflow_wrap == OverflowWrap::Anywhere) {
            wrap = wrap || (!piece.forced_break && info.cp >= 0x20);
        } else if (style.word_break == WordBreak::KeepAll) {
            // Keep CJK grapheme clusters together; spaces/forced breaks still wrap.
            wrap = piece.collapsible || piece.forced_break;
        } else if (style.overflow_wrap == OverflowWrap::BreakWord) {
            // Soft wrap after letters as a last-resort opportunity (line breaker still prefers
            // earlier opportunities). Approximate by allowing breaks after non-space glyphs.
            wrap = wrap || text::is_cjk(info.cp) ||
                   (!piece.collapsible && !piece.forced_break && info.cp >= 0x20);
        } else {
            wrap = wrap || text::is_cjk(info.cp);
        }
        piece.wrap_after = wrap;
        // L4: odd embedding levels mirror paired punctuation before FreeType / HarfBuzz lookup.
        piece.shaped_cp = (info.level & 1u) != 0 ? mirror_cp(shaped_source) : shaped_source;
        if (chain != nullptr && chain->ready()) {
            const text::LoadedGlyph loaded = chain->load(piece.shaped_cp, style.font_size, false,
                                                         style.font_weight, style.font_italic);
            piece.advance = loaded.advance;
            piece.face = loaded.face_key;
            piece.glyph = loaded.glyph_index;
        }
        pieces.push_back(piece);
    }

    // Prefer HarfBuzz for same-face spans. The kern feature is NOT taken from HarfBuzz: hb-ft
    // answers in 1/64 px units, so its pair delta is a rounded difference whose error (up to 1/64
    // per pair, and up to 2/64 once two roundings disagree) then rides on top of the exact
    // FreeType base advance. `FontChain::kerning` reads GPOS in design units and is what paint and
    // `FreeTypeTextMetrics` already use. `liga` follows the browser unless letter-spacing is
    // non-zero (CSS Fonts: a non-normal letter-spacing should not form ligatures). A ligature is
    // expanded back to one slot per codepoint; paint draws the ligature glyph once.
    if (chain != nullptr) {
        std::size_t span = 0;
        while (span < pieces.size()) {
            std::size_t span_end = span + 1;
            while (span_end < pieces.size() && pieces[span].face != 0 &&
                   pieces[span_end].face == pieces[span].face) {
                ++span_end;
            }
            if (pieces[span].face != 0 && span_end > span) {
                // One scratch per thread. A cold frame shapes dozens of short runs; allocating
                // five vectors per run was debug-heap traffic on top of the shaper itself.
                thread_local std::vector<std::uint32_t> shaped_cps;
                thread_local std::vector<float> advances;
                thread_local std::vector<std::uint32_t> glyph_ids;
                thread_local std::vector<std::uint32_t> cmap_glyphs;
                thread_local std::vector<std::uint8_t> draw;
                const std::size_t span_n = span_end - span;
                // One codepoint cannot ligate, and `calt` / `clig` are off. `load` already stored
                // the cmap glyph and its unhinted advance. Kerning still runs on the pieces below.
                if (span_n == 1) {
                    span = span_end;
                    continue;
                }
                shaped_cps.resize(span_n);
                advances.resize(span_n);
                glyph_ids.resize(span_n);
                cmap_glyphs.resize(span_n);
                draw.assign(span_n, 1u);
                for (std::size_t i = span; i < span_end; ++i) {
                    shaped_cps[i - span] = pieces[i].shaped_cp;
                    // The codepoint's cmap glyph and its advance are already in hand (`load` above).
                    // Passing them lets the shaper skip a per-glyph FreeType load for every slot it
                    // does not substitute.
                    cmap_glyphs[i - span] = pieces[i].glyph;
                    advances[i - span] = pieces[i].advance;
                }
                // Non-zero letter-spacing keeps the letters separate. The spacing itself is added
                // later, per cluster, by the inline measurer.
                const bool ligatures = style.letter_spacing == 0.f;
                const bool shaped_ok =
                    chain->shape_advances(pieces[span].face, style.font_size, shaped_cps.data(),
                                          shaped_cps.size(), advances.data(), glyph_ids.data(),
                                          ligatures, draw.data(), cmap_glyphs.data());
                if (shaped_ok) {
                    for (std::size_t i = 0; i < shaped_cps.size(); ++i) {
                        Piece& piece = pieces[span + i];
                        piece.cmap = piece.glyph;
                        piece.advance = advances[i];
                        piece.glyph = glyph_ids[i];
                        piece.draw = draw[i] != 0;
                    }
                }
            }
            span = span_end;
        }
    }

    // Kerning within the same face, in logical order, and it rides on the FIRST glyph of the pair:
    // Chrome's ShapeResult stores a glyph's advance as "own width + kern with the next glyph", which
    // is what the difftest per-glyph advance channel reads (inline-text's `lines.advances` showed the
    // kern on the second glyph where the browser has it on the first). Paint does not care: adding
    // the kern before the second glyph's pen is the same pen, and the run's width is the same sum.
    if (kern && chain != nullptr) {
        for (std::size_t i = 0; i + 1 < pieces.size(); ++i) {
            // The two halves of one ligature share a glyph id. Kerning that pair with itself
            // would add a gap inside the glyph paint draws only once.
            const bool inside_ligature = pieces[i].glyph != 0 &&
                                         pieces[i].glyph == pieces[i + 1].glyph &&
                                         (!pieces[i].draw || !pieces[i + 1].draw);
            if (inside_ligature) {
                continue;
            }
            if (pieces[i].face == pieces[i + 1].face && pieces[i].glyph != 0 &&
                pieces[i + 1].glyph != 0) {
                pieces[i].advance +=
                    chain->kerning(pieces[i].face, pieces[i].glyph, pieces[i + 1].glyph);
            }
        }
    }

    for (Piece& piece : pieces) {
        GlyphOut out;
        out.glyph.advance = piece.advance;
        out.glyph.wrap_after = piece.wrap_after;
        out.glyph.collapsible = piece.collapsible;
        out.glyph.forced_break = piece.forced_break;
        if (!piece.draw) {
            out.glyph.paint_glyph = text::kSkipPaintGlyph;
        } else if (piece.cmap != 0 && piece.glyph != piece.cmap) {
            out.glyph.paint_glyph = piece.glyph;
        }
        out.face = piece.face;
        out.script = piece.script;
        out.level = piece.level;
        out.logical_index = piece.logical_index;
        out.chars = std::move(piece.chars);
        glyphs.push_back(out);
    }
}

std::vector<ShapedRun> pack_runs(const std::vector<GlyphOut>& visual) {
    std::vector<ShapedRun> runs;
    // Where each visual glyph landed, so the ceil below can find its glyph again.
    std::vector<std::pair<std::size_t, std::size_t>> slot_of(visual.size(), {0, 0});
    std::uint32_t last_face = 0;
    Script last_script = Script::Common;
    std::uint8_t last_level = 0;
    bool have = false;
    for (std::size_t index = 0; index < visual.size(); ++index) {
        const GlyphOut& item = visual[index];
        if (!have || item.face != last_face || item.script != last_script ||
            item.level != last_level) {
            runs.emplace_back();
            last_face = item.face;
            last_script = item.script;
            last_level = item.level;
            have = true;
        }
        GlyphInline g = item.glyph;
        g.offset = runs.back().advance_inline;
        runs.back().advance_inline += g.advance;
        runs.back().glyphs.push_back(g);
        slot_of[index] = {runs.size() - 1, runs.back().glyphs.size() - 1};
        if (!item.glyph.forced_break) {
            runs.back().text += item.chars;
        }
    }
    // The text box's width is one LayoutUnit and the browser ceils it: Blink turns the summed
    // float advance of a text box into a LayoutUnit with `FromFloatCeil`, so `aa` measures 1149/64
    // px where the two exact advances add up to 1148.93 (`text/ceil_advance`). The ceil is over the
    // whole box, NOT over each shaped run inside it: a run-split CJK+Latin sentence (`example-style`
    // `.muted`, 39667/64 in the browser) came out 2/64 wide when every run was ceiled on its own,
    // because a face boundary is not a LayoutUnit boundary.
    //
    // The remainder rides on the box's last glyph in LOGICAL order: Chrome corrects the final glyph
    // of the ShapeResult, so an rtl paragraph carries it on the same glyph an ltr paragraph does
    // (the bidi reorder happens after shaping). Taking the visually last glyph instead made the
    // per-glyph advances depend on the paragraph direction - `LayoutCache.Block1Protocol` and
    // `LayoutInline.MixedBidiParagraphDirection` caught it.
    float total = 0.f;
    for (const ShapedRun& run : runs) {
        total += run.advance_inline;
    }
    if (!visual.empty()) {
        const float snapped = text::ceil_advance(total);
        if (snapped != total) {
            std::size_t best = 0;
            for (std::size_t i = 1; i < visual.size(); ++i) {
                if (visual[i].logical_index > visual[best].logical_index) {
                    best = i;
                }
            }
            GlyphInline& glyph = runs[slot_of[best].first].glyphs[slot_of[best].second];
            glyph.advance += snapped - total;
            runs[slot_of[best].first].advance_inline += snapped - total;
        }
    }
    if (runs.empty()) {
        runs.emplace_back();
    }
    return runs;
}

}  // namespace

std::vector<ShapedRun> shape(std::string_view text, const LayoutStyle& style, Direction base,
                             FreeTypeTextMetrics* metrics) {
    // TEMPORARY diagnostic split of the `shape` zone (P5 of the layout-cost plan).
    zui::trace::Zone decode_zone("shape.decode", zui::trace::Category::LayoutPhase);
    // Drop the previous call's mapped bytes before any new view is taken. Capacity stays, so
    // the views created below are not invalidated by a later append in this call.
    transform_utf8.clear();
    const std::size_t transform_cap = text.size() * 3 + 8;
    if (transform_utf8.capacity() < transform_cap) {
        transform_utf8.reserve(transform_cap);
    }
    // Reused across runs in this thread. The returned `ShapedRun`s own their text; these buffers
    // do not escape. `shape` does not call itself.
    thread_local std::vector<CpInfo> cps;
    thread_local std::vector<BidiClass> types;
    thread_local std::vector<std::uint8_t> levels;
    thread_local std::vector<GlyphOut> logical_glyphs;
    thread_local std::vector<std::uint8_t> glyph_levels;
    thread_local std::vector<std::size_t> order;
    thread_local std::vector<GlyphOut> visual;
    decode(text, cps);
    if (cps.empty()) {
        return {ShapedRun{}};
    }

    types.clear();
    types.reserve(cps.size());
    for (const CpInfo& info : cps) {
        types.push_back(info.bidi);
    }
    levels.clear();
    resolve_levels(types, base, levels);
    for (std::size_t i = 0; i < cps.size(); ++i) {
        cps[i].level = levels[i];
    }
    decode_zone.close();
    zui::trace::Zone glyphs_zone("shape.glyphs", zui::trace::Category::LayoutPhase);

    text::FontChain* chain =
        metrics != nullptr
            ? &metrics->chain_for(style.font_family, style.font_weight, style.font_italic)
            : nullptr;
    // `font-kerning: auto` and `normal` both leave the face's `kern` feature on, for every script
    // and language. The browser kerns a Latin run inside a `lang="zh-CN"` document exactly as it
    // does inside an English one, so language is not consulted here.
    const bool kern = !style.font_kerning_none;

    // Logical runs: split on embedding level or resolved script.
    logical_glyphs.clear();
    logical_glyphs.reserve(cps.size());
    std::size_t run_begin = 0;
    while (run_begin < cps.size()) {
        std::size_t run_end = run_begin + 1;
        while (run_end < cps.size() && cps[run_end].level == cps[run_begin].level &&
               cps[run_end].resolved_script == cps[run_begin].resolved_script) {
            ++run_end;
        }
        shape_logical_run(text, cps, run_begin, run_end, style, chain, kern, logical_glyphs);
        run_begin = run_end;
    }
    glyphs_zone.close();
    zui::trace::Zone pack_zone("shape.pack", zui::trace::Category::LayoutPhase);

    // L2 visual reorder of per-codepoint glyphs (1:1 with cps when metrics missing too).
    glyph_levels.clear();
    glyph_levels.reserve(logical_glyphs.size());
    for (const GlyphOut& g : logical_glyphs) {
        glyph_levels.push_back(g.level);
    }
    order.clear();
    reorder_visual(glyph_levels, order);

    visual.clear();
    visual.reserve(order.size());
    for (std::size_t index : order) {
        visual.push_back(logical_glyphs[index]);
    }
    return pack_runs(visual);
}

}  // namespace inline_text
}  // namespace layout
}  // namespace zui
