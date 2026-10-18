#include "text/hb_shape.h"

#include "text/font_chain.h"

#if defined(ZUI_WITH_HARFBUZZ)
#include <hb-ft.h>
#include <hb-ot.h>
#include <hb.h>

#include FT_MULTIPLE_MASTERS_H
#endif

#include <vector>
#include <unordered_map>

namespace zui {
namespace text {

#if defined(ZUI_WITH_HARFBUZZ)

namespace {

/// One HarfBuzz font per FreeType face, plus one scratch buffer per thread.
///
/// `hb_ft_font_create_referenced` walks the FT face and installs the hb-ft font funcs; measured on
/// the repetition probe (2000 rows of `第 N 章 标题`) that construction was ~40 us of a ~42 us
/// `hb_shape` call, i.e. essentially all of it - 83.6 ms of a 137.9 ms cold layout. hb-ft reads the
/// FT size at shape time, so one font serves every pixel size a face is ever set to, and the buffer
/// only has to be cleared between calls.
///
/// The font is keyed by the FT face pointer, so every `FT_Done_Face` has to call
/// `hb_shape_forget`: FreeType reuses addresses, and a stale entry would shape with the closed
/// face's funcs.
std::unordered_map<FT_Face, hb_font_t*>& hb_fonts() {
    static std::unordered_map<FT_Face, hb_font_t*> cache;
    return cache;
}

/// Design coordinates last pushed onto the HarfBuzz font. A variable face (Noto Sans SC) used to
/// call `FT_Get_MM_Var` on every shaped run; the axis count does not change for the life of the
/// face, and the coordinates only change when `apply_weight` instances it again.
struct AppliedVar {
    FT_UInt axes = 0;
    std::vector<FT_Fixed> design;
    bool known = false;
};

std::unordered_map<FT_Face, AppliedVar>& applied_vars() {
    static std::unordered_map<FT_Face, AppliedVar> cache;
    return cache;
}

void sync_var_coords(FT_Face face, hb_font_t* font) {
    if ((face->face_flags & FT_FACE_FLAG_MULTIPLE_MASTERS) == 0) {
        return;
    }
    AppliedVar& slot = applied_vars()[face];
    if (!slot.known) {
        FT_MM_Var* mm = nullptr;
        if (FT_Get_MM_Var(face, &mm) != 0 || mm == nullptr || mm->num_axis == 0) {
            if (mm != nullptr && face->glyph != nullptr) {
                FT_Done_MM_Var(face->glyph->library, mm);
            }
            return;
        }
        slot.axes = mm->num_axis;
        slot.design.assign(slot.axes, 0);
        if (FT_Get_Var_Design_Coordinates(face, slot.axes, slot.design.data()) == 0) {
            std::vector<float> coords(slot.axes, 0.0f);
            for (FT_UInt i = 0; i < slot.axes; ++i) {
                coords[i] = static_cast<float>(slot.design[i]) / 65536.0f;
            }
            hb_font_set_var_coords_design(font, coords.data(), slot.axes);
            slot.known = true;
        }
        if (face->glyph != nullptr) {
            FT_Done_MM_Var(face->glyph->library, mm);
        }
        return;
    }
    // Coordinates change only in `apply_weight`, which clears `known`. Reading them back on every
    // shaped run was a FreeType query plus a heap vector for a value that had not moved.
}

hb_font_t* hb_font_for(FT_Face face) {
    std::unordered_map<FT_Face, hb_font_t*>& cache = hb_fonts();
    const auto found = cache.find(face);
    if (found != cache.end()) {
        return found->second;
    }
    hb_font_t* font = hb_ft_font_create_referenced(face);
    if (font != nullptr) {
        // Match FontChain::load's layout path: unhinted outlines. Base advances still come from
        // `linearHoriAdvance`; hb-ft's 26.6 `advance.x` alone drifts ~1/64 px vs Chrome.
        hb_ft_font_set_load_flags(font, FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING);
        cache.emplace(face, font);
    }
    return font;
}

hb_buffer_t* scratch_buffer() {
    static thread_local hb_buffer_t* buffer = hb_buffer_create();
    return buffer;
}

hb_feature_t make_feature(char c0, char c1, char c2, char c3, std::uint32_t value) {
    hb_feature_t feature{};
    feature.tag = HB_TAG(c0, c1, c2, c3);
    feature.value = value;
    feature.start = 0;
    feature.end = HB_FEATURE_GLOBAL_END;
    return feature;
}

struct RawGlyph {
    std::uint32_t glyph = 0;
    std::uint32_t cluster = 0;
};

bool shape_raw(hb_font_t* font, const std::uint32_t* cps, std::size_t count, bool ligatures,
               std::vector<RawGlyph>& raw, hb_direction_t& direction) {
    hb_buffer_t* buffer = scratch_buffer();
    if (buffer == nullptr) {
        return false;
    }
    hb_buffer_clear_contents(buffer);
    hb_buffer_add_utf32(buffer, cps, static_cast<int>(count), 0, static_cast<int>(count));
    hb_buffer_guess_segment_properties(buffer);

    // `kern` stays off: hb-ft reports in 1/64 px, so its pair delta is a rounded difference and
    // the caller kerns with `FontChain::kerning` (see hb_shape.h). `liga` is the browser default.
    // `clig` / `calt` stay off: contextual alternates would swap glyph ids that are still 1:1 and
    // move advances that already match. A ligature (fewer glyphs) is expanded below.
    const hb_feature_t features[] = {
        make_feature('k', 'e', 'r', 'n', 0u),
        make_feature('l', 'i', 'g', 'a', ligatures ? 1u : 0u),
        make_feature('c', 'l', 'i', 'g', 0u),
        make_feature('c', 'a', 'l', 't', 0u),
    };
    hb_shape(font, buffer, features,
             static_cast<unsigned int>(sizeof(features) / sizeof(features[0])));

    const unsigned int glyph_count = hb_buffer_get_length(buffer);
    const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, nullptr);
    if (infos == nullptr || glyph_count == 0) {
        return false;
    }
    direction = hb_buffer_get_direction(buffer);
    raw.resize(glyph_count);
    for (unsigned int i = 0; i < glyph_count; ++i) {
        raw[i].glyph = infos[i].codepoint;
        raw[i].cluster = infos[i].cluster;
    }
    return true;
}

float unhinted_advance(FT_Face face, std::uint32_t glyph) {
    if (FT_Load_Glyph(face, glyph, FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING) != 0) {
        return -1.f;
    }
    return static_cast<float>(face->glyph->linearHoriAdvance) / 65536.0f;
}

// Split one ligature glyph across `n` source codepoints. Carets come back in design units
// (the font scale is the UPEM for this call). They are scaled by the unhinted pixel advance,
// not by HarfBuzz's 26.6 rounding: that rounding put Noto's fi caret at 4.8125 px, and Edge's
// client rect is the caret span of the unrounded half (4.816 → 4.828125).
bool split_ligature(FT_Face face, hb_font_t* font, std::uint32_t glyph, float total, int n,
                    HbGlyphAdvance* out) {
    if (n <= 1 || n > 8 || total < 0.f) {
        return false;
    }
    const double x_scale = static_cast<double>(face->size->metrics.x_scale);
    const double design =
        x_scale > 0.0 ? static_cast<double>(total) * 65536.0 * 64.0 / x_scale : 0.0;
    hb_position_t carets[8];
    unsigned int caret_count = 8;
    const unsigned int upem = face->units_per_EM > 0 ? face->units_per_EM : 1000;
    hb_font_set_scale(font, static_cast<int>(upem), static_cast<int>(upem));
    hb_ot_layout_get_ligature_carets(font, HB_DIRECTION_LTR, glyph, 0, &caret_count, carets);

    bool use_carets = design > 1.0 && caret_count == static_cast<unsigned int>(n - 1);
    float cuts[9];
    cuts[0] = 0.f;
    if (use_carets) {
        for (unsigned int c = 0; c < caret_count; ++c) {
            const float at =
                static_cast<float>(static_cast<double>(carets[c]) * static_cast<double>(total) /
                                   design);
            if (!(at > cuts[c] && at < total)) {
                use_carets = false;
                break;
            }
            cuts[c + 1] = at;
        }
    }
    if (!use_carets) {
        const float share = total / static_cast<float>(n);
        for (int k = 1; k < n; ++k) {
            cuts[k] = share * static_cast<float>(k);
        }
    }
    float consumed = 0.f;
    for (int k = 0; k < n; ++k) {
        const float at = (k + 1 == n) ? total : cuts[k + 1];
        out[k].advance = at - consumed;
        consumed = at;
        out[k].glyph_index = glyph;
        out[k].draw = k == 0;
    }
    return true;
}

}  // namespace

bool hb_shape_advances(FT_Face face, const std::uint32_t* cps, std::size_t count,
                       HbGlyphAdvance* out, bool ligatures, const std::uint32_t* cmap_glyphs) {
    if (face == nullptr || cps == nullptr || out == nullptr || count == 0) {
        return false;
    }
    if (face->size == nullptr) {
        return false;
    }

    hb_font_t* font = hb_font_for(face);
    if (font == nullptr) {
        return false;
    }
    // Mirror the FT face's variation coordinates onto the HarfBuzz font by hand.
    //
    // hb-ft only does this in `hb_ft_font_changed`, and only when its build defines
    // `HAVE_FT_GET_VAR_BLEND_COORDINATES` (`hb-ft.cc`: `#if defined(...) && !defined(HB_NO_VAR)`).
    // The vendored build has no configure probe and never defines it - the macro appears nowhere
    // outside `hb-ft.cc` - so a variable face is shaped at its *default* instance: its GPOS
    // kern values are the Thin instance's. Measured against the difftest browser (Chrome at
    // wght 400): `Av`-style pairs came out (13/64)px where Chrome's instanced face gives
    // (21/64)px, so every kerned string measured ~3/64px wide per pair. Static faces (Arial)
    // matched exactly, which is what pointed at instancing rather than at the shaping call.
    sync_var_coords(face, font);
    // Match FontChain::load's layout path: unhinted outlines. Base advances still come from
    // linearHoriAdvance below — hb-ft's 26.6 advance.x alone drifts ~1/64 px vs Chrome.
    //
    // The remaining 2/64 px gap to the browser (`build/adv_probe.py`, Noto Sans at 16px: 957/64
    // where Chrome reports 959/64) is *not* hinting: shaping with the face's default flags gave
    // `inline-text` 229 length differences (was 69) and `FT_LOAD_TARGET_LIGHT` the same 229, so
    // neither full nor slight hinting is what the browser lays out with. The unhinted path below is
    // the closest of the three; whatever the rest is, it is not the FT load flags.
    // One scratch for every shaped run. The cold frame used to allocate it per string.
    thread_local std::vector<RawGlyph> raw;
    hb_direction_t direction = HB_DIRECTION_LTR;
    if (!shape_raw(font, cps, count, ligatures, raw, direction)) {
        return false;
    }

    // One glyph per codepoint: the common path. Advances stay the unhinted linear advance.
    // HarfBuzz's own x_advance is 1/64-quantized (hb-ft) and is not used as the base.
    if (raw.size() == count) {
        for (std::size_t i = 0; i < count; ++i) {
            if (cmap_glyphs != nullptr && cmap_glyphs[i] == raw[i].glyph && raw[i].glyph != 0) {
                // HarfBuzz kept this codepoint's cmap glyph. `FontChain::load` already resolved the
                // advance for exactly this glyph with the same FT load flags and the same
                // `linearHoriAdvance`, so the caller's value stands and the FT load is skipped.
                out[i].glyph_index = raw[i].glyph;
                out[i].draw = true;
                out[i].unchanged = true;
                continue;
            }
            const float advance = unhinted_advance(face, raw[i].glyph);
            if (advance < 0.f) {
                return false;
            }
            out[i].advance = advance;
            out[i].glyph_index = raw[i].glyph;
            out[i].draw = true;
        }
        return true;
    }

    // Fewer glyphs than codepoints is a ligature (or something this splitter does not model).
    // RTL carets are measured from the other side; leave those on the unligated fallback.
    if (!ligatures || direction != HB_DIRECTION_LTR || raw.size() > count) {
        return false;
    }
    std::vector<int> owner(count, -1);
    for (std::size_t g = 0; g < raw.size(); ++g) {
        const std::uint32_t start = raw[g].cluster;
        const std::uint32_t end =
            (g + 1 < raw.size()) ? raw[g + 1].cluster : static_cast<std::uint32_t>(count);
        if (end <= start || end > count) {
            return false;
        }
        for (std::uint32_t k = start; k < end; ++k) {
            if (owner[k] != -1) {
                return false;
            }
            owner[k] = static_cast<int>(g);
        }
    }
    for (int slot : owner) {
        if (slot < 0) {
            return false;
        }
    }
    for (std::size_t g = 0; g < raw.size(); ++g) {
        const std::uint32_t start = raw[g].cluster;
        const std::uint32_t end =
            (g + 1 < raw.size()) ? raw[g + 1].cluster : static_cast<std::uint32_t>(count);
        const int n = static_cast<int>(end - start);
        const float advance = unhinted_advance(face, raw[g].glyph);
        if (advance < 0.f) {
            return false;
        }
        if (n == 1) {
            out[start].advance = advance;
            out[start].glyph_index = raw[g].glyph;
            out[start].draw = true;
            continue;
        }
        if (!split_ligature(face, font, raw[g].glyph, advance, n, out + start)) {
            return false;
        }
    }

    return true;
}

void hb_shape_forget(FT_Face face) {
    if (face == nullptr) {
        return;
    }
    std::unordered_map<FT_Face, hb_font_t*>& cache = hb_fonts();
    const auto found = cache.find(face);
    if (found == cache.end()) {
        applied_vars().erase(face);
        return;
    }
    hb_font_destroy(found->second);
    cache.erase(found);
    applied_vars().erase(face);
}

void hb_shape_note_var(FT_Face face) {
    if (face == nullptr) {
        return;
    }
    applied_vars()[face].known = false;
}

#else

bool hb_shape_advances(FT_Face, const std::uint32_t*, std::size_t, HbGlyphAdvance*, bool,
                       const std::uint32_t*) {
    return false;
}

void hb_shape_forget(FT_Face) {}

void hb_shape_note_var(FT_Face) {}

#endif

}  // namespace text
}  // namespace zui
