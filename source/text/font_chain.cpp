#include "text/font_chain.h"
#include "text/font_chain_internal.h"

#include "text/dwrite_raster.h"
#include "text/mask_gamma.h"

#include "text/gpos_kern.h"
#include "text/hb_shape.h"
#include "trace/trace.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H
#include FT_OUTLINE_H
#include FT_SYNTHESIS_H
#include FT_TRUETYPE_TABLES_H

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <unordered_set>

namespace zui {
namespace text {
namespace {


/// CSS `font-weight: normal`, the initial value every element starts from.
constexpr int kNormalWeight = 400;


// The CSS initial `font-weight` is `normal`, 400. A variable font opens at whatever instance
// it declares as its own default, and that default is not always 400: Noto Sans SC declares
// `Weight` with default 100 (Thin), which measures about 4% narrower than the 400 a browser
// instances. Static faces have no axes and are left alone.
void apply_weight(FT_Face face, int weight) {
    if ((face->face_flags & FT_FACE_FLAG_MULTIPLE_MASTERS) == 0) {
        return;
    }
    FT_MM_Var* var = nullptr;
    if (FT_Get_MM_Var(face, &var) != 0 || var == nullptr) {
        return;
    }
    std::vector<FT_Fixed> coords(static_cast<size_t>(var->num_axis));
    for (FT_UInt i = 0; i < var->num_axis; ++i) {
        coords[i] = var->axis[i].def;
        if (var->axis[i].tag == FT_MAKE_TAG('w', 'g', 'h', 't')) {
            const FT_Fixed want = static_cast<FT_Fixed>(weight) << 16;
            coords[i] = std::min(std::max(want, var->axis[i].minimum), var->axis[i].maximum);
        }
    }
    FT_Set_Var_Design_Coordinates(face, var->num_axis, coords.data());
    FT_Done_MM_Var(face->glyph->library, var);
    // The HarfBuzz font caches the previous instance. A later shape has to read these coordinates
    // once; until the next `apply_weight` they do not move.
    hb_shape_note_var(face);
}


/// How much wider a synthetic bold stroke makes one glyph, in em. Measured on the pinned browser
/// (`build/bold_probe.html`): with SimSun - a family with no bold face - the outline strike adds
/// exactly 25/64, 30/64 and 40/64 px per three glyphs at 20, 24 and 32px, i.e. 5/256 em each.
constexpr float kSyntheticBoldEm = 5.0f / 256.0f;


// The font box of a line comes from the face's OS/2 win metrics, not from hhea: Noto Sans SC
// reports 1.00/0.20 em through hhea but 1.16/0.29 em through win metrics, and the platform text
// stack (so a browser too) shows the latter. hhea stays the answer when a face has no OS/2 table.
void face_box(FT_Face face, float pixel, float& ascent, float& descent, float& x_height,
              float& avg_char_width, float& max_char_width) {
    ascent = face->size->metrics.ascender / 64.0f;
    descent = -face->size->metrics.descender / 64.0f;
    x_height = pixel * 0.5f;
    avg_char_width = 0.f;
    max_char_width = 0.f;
    if (face->units_per_EM == 0) {
        return;
    }
    const float scale = pixel / static_cast<float>(face->units_per_EM);
    // hhea.advanceWidthMax: Blink's MaxCharWidth falls back to this order of magnitude on
    // platforms without DirectWrite's fMaxCharWidth.
    if (face->max_advance_width > 0) {
        max_char_width = static_cast<float>(face->max_advance_width) * scale;
    }
    const auto* os2 = static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(face, ft_sfnt_os2));
    if (os2 == nullptr) {
        return;
    }
    if (os2->usWinAscent > 0) {
        ascent = static_cast<float>(os2->usWinAscent) * scale;
    }
    if (os2->usWinDescent > 0) {
        descent = static_cast<float>(os2->usWinDescent) * scale;
    }
    // sxHeight is valid from OS/2 version 2. `vertical-align: middle` aligns to half of it.
    if (os2->version >= 2 && os2->sxHeight > 0) {
        x_height = static_cast<float>(os2->sxHeight) * scale;
    }
    if (os2->xAvgCharWidth > 0) {
        avg_char_width = static_cast<float>(os2->xAvgCharWidth) * scale;
    }
}


std::uint32_t hash_path(std::string_view path) {
    std::uint32_t h = 2166136261u;
    for (unsigned char c : path) {
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

std::uint32_t face_key_for(std::string_view path, int weight, bool italic) {
    std::uint32_t h = hash_path(path);
    h ^= static_cast<std::uint32_t>(weight) * 0x9e3779b9u;
    if (italic) {
        h ^= 0x85ebca6bu;
    }
    return h == 0 ? 1u : h;
}

}  // namespace

uint32_t next_cp(std::string_view s, size_t& i) {
    if (i >= s.size()) {
        return 0;
    }
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
        ++i;
        return c;
    }
    if ((c >> 5) == 0x6 && i + 1 < s.size()) {
        uint32_t cp = ((c & 0x1f) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3f);
        i += 2;
        return cp;
    }
    if ((c >> 4) == 0xe && i + 2 < s.size()) {
        uint32_t cp = ((c & 0x0f) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3f) << 6) |
                      (static_cast<unsigned char>(s[i + 2]) & 0x3f);
        i += 3;
        return cp;
    }
    if ((c >> 3) == 0x1e && i + 3 < s.size()) {
        uint32_t cp = ((c & 0x07) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3f) << 12) |
                      ((static_cast<unsigned char>(s[i + 2]) & 0x3f) << 6) |
                      (static_cast<unsigned char>(s[i + 3]) & 0x3f);
        i += 4;
        return cp;
    }
    ++i;
    return 0xfffd;
}

bool is_space(uint32_t cp) {
    return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == 0x3000;
}

bool is_cjk(uint32_t cp) {
    return (cp >= 0x1100 && cp <= 0x11FF) || (cp >= 0x2E80 && cp <= 0x2FFF) ||
           (cp >= 0x3000 && cp <= 0x303F) || (cp >= 0x3040 && cp <= 0x30FF) ||
           (cp >= 0x31F0 && cp <= 0x31FF) || (cp >= 0x3400 && cp <= 0x9FFF) ||
           (cp >= 0xAC00 && cp <= 0xD7AF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0xFF00 && cp <= 0xFFEF) || (cp >= 0x20000 && cp <= 0x2A6DF);
}

/// One FreeType face for a file at one variable-font weight. Layout, paint, and the font-metric
/// probe all used to call `FT_New_Face` on the same file. The face is mutable (`FT_Set_Char_Size`,
/// the glyph slot), so every user re-sizes it before reading. UI thread only.
struct SharedFace {
    FT_Face face = nullptr;
    std::string key;
    int refs = 0;
    std::int64_t size_q = 0;
};

namespace {

std::unordered_map<std::string, std::unique_ptr<SharedFace>>& face_pool() {
    static std::unordered_map<std::string, std::unique_ptr<SharedFace>> pool;
    return pool;
}

FT_Library shared_ft_library() {
    static FT_Library library = nullptr;
    if (library == nullptr && FT_Init_FreeType(&library) != 0) {
        library = nullptr;
    }
    return library;
}

}  // namespace


FontChain::FontChain() = default;

void FontChain::release_open_faces() {
    for (Face& f : faces_) {
        if (f.shared == nullptr) {
            if (f.face != nullptr) {
                hb_shape_forget(f.face);
                FT_Done_Face(f.face);
            }
            f.face = nullptr;
            continue;
        }
        SharedFace* shared = f.shared;
        f.shared = nullptr;
        f.face = nullptr;
        if (--shared->refs > 0) {
            continue;
        }
        if (shared->face != nullptr) {
            hb_shape_forget(shared->face);
            FT_Done_Face(shared->face);
            shared->face = nullptr;
        }
        const std::string key = shared->key;
        face_pool().erase(key);
    }
    faces_.clear();
    library_ = nullptr;
}

FontChain::~FontChain() { release_open_faces(); }

void FontChain::use_px(float px) {
    if (faces_.empty() || px <= 0.f) {
        return;
    }
    const std::int64_t size_q =
        std::max<std::int64_t>(64, static_cast<std::int64_t>(px * 64.0f + 0.5f));
    ensure_px(size_q);
}

void FontChain::ensure_px(std::int64_t size_q) {
    bool drifted = false;
    for (const Face& f : faces_) {
        if (f.shared != nullptr && f.shared->size_q != size_q) {
            drifted = true;
            break;
        }
    }
    if (px_q_ == size_q && !drifted) {
        return;
    }
    const bool box_stale = px_q_ != size_q;
    px_q_ = size_q;
    for (Face& f : faces_) {
        if (f.face == nullptr) {
            continue;
        }
        if (f.shared != nullptr && f.shared->size_q == size_q) {
            continue;
        }
        FT_Set_Char_Size(f.face, 0, static_cast<FT_F26Dot6>(size_q), 0, 72);
        if (f.shared != nullptr) {
            f.shared->size_q = size_q;
        }
    }
    if (!box_stale || faces_.empty() || faces_.front().face == nullptr) {
        return;
    }
    ascent_ = 0;
    descent_ = 0;
    x_height_ = 0;
    avg_char_width_ = 0;
    max_char_width_ = 0;
    face_box(faces_.front().face, static_cast<float>(size_q) / 64.0f, ascent_, descent_, x_height_,
             avg_char_width_, max_char_width_);
    if (descent_ < 0) {
        descent_ = 0;
    }
}

bool FontChain::init(std::string_view families, int weight, bool italic) {
    release_open_faces();
    metrics_.clear();
    px_q_ = 0;
    ascent_ = 0;
    descent_ = 0;
    x_height_ = 0;
    avg_char_width_ = 0;
    max_char_width_ = 0;
    weight_ = weight <= 0 ? kNormalWeight : weight;
    italic_ = italic;
    FT_Library library = shared_ft_library();
    if (library == nullptr) {
        return false;
    }
    library_ = library;
    auto& pool = face_pool();
    for (const FaceFile& choice : files_for(families, weight_, italic_)) {
        std::string pool_key = choice.path;
        pool_key.push_back('\n');
        pool_key.append(std::to_string(weight_));
        auto it = pool.find(pool_key);
        if (it == pool.end()) {
            FT_Face face = nullptr;
            // Opening a face (and walking the fallback list until one opens) is the cold cost.
            // A second chain for the same file and weight reuses the face below.
            zui::trace::Zone zone("font.load", zui::trace::Category::Text);
            if (FT_New_Face(library, choice.path.c_str(), 0, &face) != 0) {
                continue;
            }
            apply_weight(face, weight_);
            auto created = std::make_unique<SharedFace>();
            created->face = face;
            created->key = pool_key;
            it = pool.emplace(std::move(pool_key), std::move(created)).first;
        }
        SharedFace* shared = it->second.get();
        shared->refs += 1;
        Face slot;
        slot.face = shared->face;
        slot.path = choice.path;
        slot.key = face_key_for(choice.path, weight_, italic_);
        slot.synthetic_bold = choice.synthetic_bold;
        slot.shared = shared;
        faces_.push_back(std::move(slot));
    }
    if (faces_.empty()) {
        library_ = nullptr;
        return false;
    }
    return true;
}


LoadedGlyph FontChain::load(std::uint32_t cp, float px, bool render, int weight, bool italic,
                            bool want_path, std::uint32_t glyph_id, float pen_x, float pen_y,
                            std::uint8_t coverage_green) {
    LoadedGlyph out;
    if (faces_.empty() || px <= 0) {
        return out;
    }
    // Prefer the chain's init() style. Callers that need another weight/italic should
    // open a separate chain via FreeTypeTextMetrics::chain_for (keeps Face::key == face_key).
    (void)weight;
    (void)italic;
    const int use_weight = weight_;
    const bool use_italic = italic_;
    // Font sizes are not always whole pixels -- the UA gives form controls 13.3333px and em
    // lengths land anywhere -- so the size is set in 1/64 px steps through the point size at
    // 72 dpi instead of rounding down to an integer pixel size.
    const std::int64_t size_q =
        std::max<std::int64_t>(64, static_cast<std::int64_t>(px * 64.0f + 0.5f));
    ensure_px(size_q);

    const std::uint64_t mk =
        (static_cast<std::uint64_t>(size_q) << 32) ^ static_cast<std::uint64_t>(cp) ^
        (static_cast<std::uint64_t>(use_weight) << 17) ^ (use_italic ? 0x80000000ull : 0) ^
        (static_cast<std::uint64_t>(glyph_id) * 0x9E3779B97F4A7C15ull);
    auto mit = metrics_.find(mk);
    if (mit != metrics_.end() && !render) {
        const Metric& m = mit->second;
        out.ok = m.ok;
        out.advance = m.advance;
        out.face_key = m.face_key;
        out.glyph_index = m.glyph_index;
        if (want_path && m.face_index >= 0 && m.face_index < static_cast<int>(faces_.size())) {
            out.path = faces_[static_cast<size_t>(m.face_index)].path;
        }
        return out;
    }

    int face_index = 0;
    Face* chosen = &faces_.front();
    FT_UInt glyph = 0;
    if (mit != metrics_.end()) {
        face_index = mit->second.face_index;
        glyph = mit->second.glyph_index;
        if (face_index < 0 || face_index >= static_cast<int>(faces_.size())) {
            face_index = 0;
        }
        chosen = &faces_[static_cast<size_t>(face_index)];
    } else {
        for (int i = 0; i < static_cast<int>(faces_.size()); ++i) {
            FT_UInt gi = FT_Get_Char_Index(faces_[static_cast<size_t>(i)].face, cp);
            if (gi != 0) {
                face_index = i;
                chosen = &faces_[static_cast<size_t>(i)];
                glyph = glyph_id != 0 ? glyph_id : gi;
                break;
            }
        }
    }
    // The advance is always the unhinted `linearHoriAdvance`. Hinting it, or taking DirectWrite's
    // measure, moves the run width off the browser. The bitmap is a separate step below.
    const FT_Int32 flags = FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING;
    if (FT_Load_Glyph(chosen->face, glyph, flags) != 0) {
        if (mit == metrics_.end()) {
            Metric m;
            m.face_index = face_index;
            m.glyph_index = glyph;
            metrics_.emplace(mk, m);
        }
        return out;
    }
    FT_GlyphSlot slot = chosen->face->glyph;
    out.ok = true;
    // Subpixel advance: the hinted `advance.x` is rounded to whole pixels, which loses up
    // to half a pixel per glyph and makes every string measurably narrower than a browser
    // that positions glyphs with subpixel precision. linearHoriAdvance is not grid-fitted, and
    // the grid itself is applied once per shaped run (`text/ceil_advance`).
    out.advance = slot->linearHoriAdvance / 65536.0f;
    if (chosen->synthetic_bold) {
        // The fake bold stroke the browser draws widens the advance too: 5/256 em per glyph,
        // measured on the pinned browser's outline strikes (`build/bold_probe.html`).
        out.advance += px * kSyntheticBoldEm;
    }
    out.bearing_x = static_cast<float>(slot->bitmap_left);
    out.bearing_y = static_cast<float>(slot->bitmap_top);
    out.face_key = face_key_for(chosen->path, use_weight, use_italic);
    out.glyph_index = glyph;
    if (render || want_path) {
        out.path = chosen->path;
    }
    if (mit == metrics_.end()) {
        Metric m;
        m.ok = true;
        m.advance = out.advance;
        m.face_index = face_index;
        m.glyph_index = glyph;
        m.face_key = face_key_for(chosen->path, use_weight, use_italic);
        metrics_.emplace(mk, m);
    }
    if (!render) {
        return out;
    }
    // Bitmap path kept for GlyphAtlas::get. The painter does not call it: vector glyphs upload
    // the unhinted outline and cover them in the fragment shader. This mask still does not
    // feed back into `out.advance`.
    const auto warp_coverage = [&]() {
        if (bitmap_.empty()) {
            return;
        }
        const std::uint8_t* lut = glyph_coverage_lut(coverage_green);
        for (std::uint8_t& pixel : bitmap_) {
            pixel = lut[pixel];
        }
    };
    const float origin_x = glyph_phase_origin(glyph_pixel_phase(pen_x));
    const float origin_y = glyph_phase_origin(glyph_pixel_phase(pen_y));
    std::vector<std::uint32_t> axis_tags;
    std::vector<float> axis_values;
    if ((chosen->face->face_flags & FT_FACE_FLAG_MULTIPLE_MASTERS) != 0) {
        FT_MM_Var* var = nullptr;
        if (FT_Get_MM_Var(chosen->face, &var) == 0 && var != nullptr && var->num_axis > 0) {
            std::vector<FT_Fixed> coords(var->num_axis, 0);
            if (FT_Get_Var_Design_Coordinates(chosen->face, var->num_axis, coords.data()) == 0) {
                axis_tags.resize(var->num_axis);
                axis_values.resize(var->num_axis);
                for (FT_UInt i = 0; i < var->num_axis; ++i) {
                    axis_tags[i] = var->axis[i].tag;
                    axis_values[i] = static_cast<float>(coords[i]) / 65536.0f;
                }
            }
            if (chosen->face->glyph != nullptr) {
                FT_Done_MM_Var(chosen->face->glyph->library, var);
            }
        }
    }
    DWriteMask mask;
    if (dwrite_raster_glyph(chosen->path, glyph, px, origin_x, origin_y, axis_tags.data(),
                            axis_values.data(), static_cast<int>(axis_tags.size()), mask,
                            chosen->synthetic_bold) &&
        mask.ok) {
        bitmap_ = std::move(mask.alpha);
        warp_coverage();
        out.bearing_x = static_cast<float>(mask.left);
        out.bearing_y = static_cast<float>(mask.top);
        out.width = static_cast<float>(mask.width);
        out.height = static_cast<float>(mask.height);
        out.pixels = bitmap_.empty() ? nullptr : bitmap_.data();
        return out;
    }
    // A synthetic bold glyph is loaded as an outline, emboldened, and only then rendered:
    // `FT_GlyphSlot_Embolden` works on the outline, and rendering first would leave it untouched.
    const FT_Int32 free_type_flags =
        FT_LOAD_NO_BITMAP | FT_LOAD_TARGET_LIGHT | FT_LOAD_FORCE_AUTOHINT |
        (chosen->synthetic_bold ? 0 : FT_LOAD_RENDER);
    if (FT_Load_Glyph(chosen->face, glyph, free_type_flags) != 0) {
        return out;
    }
    slot = chosen->face->glyph;
    if (chosen->synthetic_bold) {
        // FreeType's own fake bold: an outline; the stroke matches DirectWrite's closely enough
        // that the two raster paths agree on the mask's size. It returns nothing.
        FT_GlyphSlot_Embolden(slot);
        if (FT_Render_Glyph(slot, FT_RENDER_MODE_LIGHT) != 0) {
            return out;
        }
    }
    if (slot->bitmap.buffer == nullptr || slot->bitmap.width == 0 || slot->bitmap.rows == 0) {
        return out;
    }
    const int gw = static_cast<int>(slot->bitmap.width);
    const int gh = static_cast<int>(slot->bitmap.rows);
    const int pitch = slot->bitmap.pitch;
    bitmap_.assign(static_cast<size_t>(gw * gh), 0);
    for (int row = 0; row < gh; ++row) {
        const int src_row = pitch >= 0 ? row : gh - 1 - row;
        const unsigned char* src = slot->bitmap.buffer + src_row * std::abs(pitch);
        std::uint8_t* dst = bitmap_.data() + static_cast<size_t>(row * gw);
        for (int col = 0; col < gw; ++col) {
            dst[col] = src[col];
        }
    }
    warp_coverage();
    out.bearing_x = static_cast<float>(slot->bitmap_left);
    out.bearing_y = static_cast<float>(slot->bitmap_top);
    out.width = static_cast<float>(gw);
    out.height = static_cast<float>(gh);
    out.pixels = bitmap_.data();
    return out;
}

float FontChain::kerning(std::uint32_t face_key, std::uint32_t left_glyph,
                         std::uint32_t right_glyph) {
    if (left_glyph == 0 || right_glyph == 0) {
        return 0;
    }
    // `x_scale` is the face's current size. Another chain may have resized the shared face.
    if (px_q_ > 0) {
        ensure_px(px_q_);
    }
    for (Face& face : faces_) {
        if (face.key != face_key || face.face == nullptr) {
            continue;
        }
        // GPOS first: it is where a modern face keeps its kerning. The legacy `kern` table is the
        // fallback, read unfitted so the value stays fractional - the fitted one is rounded to
        // whole pixels, which is a half pixel of drift per pair. Layout prefers HarfBuzz via
        // `shape_advances`; this path remains for paint and the HB-off fallback.
        if (const std::int16_t units = gpos_kern_units(face.face, weight_, left_glyph, right_glyph);
            units != 0 && face.face->units_per_EM > 0) {
            return static_cast<float>(units) * face.face->size->metrics.x_scale /
                   (65536.0f * 64.0f);
        }
        FT_Vector delta;
        if (FT_Get_Kerning(face.face, left_glyph, right_glyph, FT_KERNING_UNFITTED, &delta) != 0) {
            return 0;
        }
        return static_cast<float>(delta.x) / 64.f;
    }
    return 0;
}

float FontChain::glyph_font_box(std::uint32_t cp, float px) {
    if (faces_.empty() || px <= 0.f) {
        return 0.f;
    }
    const LoadedGlyph glyph = load(cp, px, false);
    Face* chosen = &faces_.front();
    if (glyph.face_key != 0) {
        for (Face& face : faces_) {
            if (face.key == glyph.face_key) {
                chosen = &face;
                break;
            }
        }
    }
    float ascent = 0.f;
    float descent = 0.f;
    float x_height = 0.f;
    float avg = 0.f;
    float max_w = 0.f;
    const float sized = px_q_ > 0 ? static_cast<float>(px_q_) / 64.f : px;
    face_box(chosen->face, sized, ascent, descent, x_height, avg, max_w);
    if (descent < 0.f) {
        descent = 0.f;
    }
    return std::round(ascent) + std::round(descent);
}

bool FontChain::shape_advances(std::uint32_t face_key, float px, const std::uint32_t* cps,
                               std::size_t count, float* advances_out, std::uint32_t* glyphs_out,
                               bool ligatures, std::uint8_t* draw_out,
                               const std::uint32_t* cmap_glyphs) {
    if (cps == nullptr || advances_out == nullptr || glyphs_out == nullptr || count == 0 ||
        px <= 0 || faces_.empty()) {
        return false;
    }
    Face* chosen = nullptr;
    for (Face& face : faces_) {
        if (face.key == face_key && face.face != nullptr) {
            chosen = &face;
            break;
        }
    }
    if (chosen == nullptr) {
        return false;
    }
    const std::int64_t size_q =
        std::max<std::int64_t>(64, static_cast<std::int64_t>(px * 64.0f + 0.5f));
    // Keep every face of the chain on the same size; load() and hb-ft both read FT size.
    ensure_px(size_q);
    std::vector<HbGlyphAdvance> slots(count);
    // The shaper is allowed to keep a slot's advance: the caller's value for the same glyph is what
    // `load` produced with the same FT flags. Hand it over, and let the loop below put it back.
    for (std::size_t i = 0; i < count; ++i) {
        slots[i].advance = advances_out[i];
    }
    if (!hb_shape_advances(chosen->face, cps, count, slots.data(), ligatures, cmap_glyphs)) {
        return false;
    }
    const float bold_delta = chosen->synthetic_bold ? px * kSyntheticBoldEm : 0.f;
    for (std::size_t i = 0; i < count; ++i) {
        // The shaped path replaces the advances, so the synthetic bold stroke has to be added
        // here as well as in `load` - the layout takes this path for a same-face run. A slot the
        // shaper left alone keeps the caller's advance, which already carries that delta.
        if (!slots[i].unchanged) {
            advances_out[i] = slots[i].advance + bold_delta;
        }
        glyphs_out[i] = slots[i].glyph_index;
        if (draw_out != nullptr) {
            draw_out[i] = slots[i].draw ? 1u : 0u;
        }
    }
    return true;
}

}  // namespace text
}  // namespace zui
