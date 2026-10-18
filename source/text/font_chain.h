#ifndef ZUI_TEXT_FONT_CHAIN_H
#define ZUI_TEXT_FONT_CHAIN_H

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace zui {
namespace text {

uint32_t next_cp(std::string_view s, size_t& i);
bool is_cjk(uint32_t cp);
bool is_space(uint32_t cp);

/// Chromium stores a text run's width as a LayoutUnit and *ceils* it onto the 1/64 px grid.
/// Measured on an inline-block span with the embedded Noto Sans at 16px: one `a` is 575/64
/// (exact 574.464), one space 267/64 (exact 266.24), but `aa` is 1149/64 = ceil(1148.93) - one
/// ceil over the run, not one per glyph (`4` and `42` are 586/64 and 1172/64, the same rule). The
/// epsilon absorbs float noise so a sum that already sits on the grid is not pushed a full step:
/// the advances here come from integer font units, so a real fraction is far larger than it.
inline float ceil_advance(float value) { return std::ceil(value * 64.f - 0.01f) / 64.f; }

/// Four baseline phases per pixel. DirectWrite rasterizes the fractional pen at one of these so
/// the mask matches the phase Edge used, and a whole pixel of pen still shares one cache entry.
inline int glyph_pixel_phase(float pen) {
    const float frac = pen - std::floor(pen);
    int phase = static_cast<int>(std::lround(frac * 4.f));
    if (phase >= 4 || phase < 0) {
        phase = 0;
    }
    return phase;
}

inline float glyph_phase_origin(int phase) { return static_cast<float>(phase) * 0.25f; }

/// Paint slot that advances the pen and draws nothing. A ligature continuation uses it so the
/// caret split still has one advance per codepoint while the ligature glyph is drawn once.
constexpr std::uint32_t kSkipPaintGlyph = 0xffffffffu;

/// Registers a face file for a CSS family name, so both ends of a comparison shape the same
/// file (TESTPLAN section 6). Case-insensitive; a registered name wins over the platform
/// lookup for that family, and the last registration of a name wins.
void register_embedded_font(const std::string& family, const std::string& path);

/// The file `register_embedded_font` last stored for @p family, or empty when none is registered.
std::string embedded_font_path(std::string_view family);

/// Registers local `@font-face` `src: url()` files. `base_dir` is the stylesheet directory, or
/// the document directory for a `<style>` element. A file already on disk is available before
/// the first layout, so every `font-display` value uses it. A face that is still loading follows
/// the pinned browser's periods (block and auto: 3s invisible, then infinite swap; swap: visible
/// fallback immediately, then infinite swap; fallback: 100ms invisible, then 3s swap; optional:
/// 100ms invisible, then the fallback stays). `format("woff2")` is skipped.
void register_font_faces(std::string_view css, const std::filesystem::path& base_dir);

/// Where a registered `@font-face` family is on its font-display timeline. A family registered
/// only through `register_embedded_font` (no descriptor) is `Use`.
enum class FontDisplayPhase { Use, Invisible, Fallback };

FontDisplayPhase font_display_phase(std::string_view family);

/// Pins the clock used by `font_display_phase`. A negative value restores the steady clock.
void font_display_test_clock(int now_ms);

/// Overrides one family's request time and arrival. `loaded_at_ms` is ignored when `loaded` is
/// false. The family's `font-display` mode stays whatever `@font-face` registered.
void font_display_test_arm(std::string_view family, int requested_ms, int loaded_at_ms, bool loaded);

/// Bumped by every `register_embedded_font`. Callers that cache what a family resolves to (the
/// layout side caches `ch` / `ex` per used font) fold this into their key so a registration made
/// after the first layout cannot leave a stale entry behind.
std::uint64_t font_registry_generation();

/// One positioned glyph. `pixels` is tightly packed grayscale, valid only until
/// the next load() on the same FontChain.
struct LoadedGlyph {
    bool ok = false;
    float advance = 0;
    float bearing_x = 0;
    float bearing_y = 0;
    float width = 0;
    float height = 0;
    const std::uint8_t* pixels = nullptr;
    std::uint32_t face_key = 0;
    std::uint32_t glyph_index = 0;
    std::string path;
};

/// One outline curve in em space, y up, origin on the baseline. `kind` is 1 line, 2 quadratic,
/// 3 cubic. A line uses (x0,y0)-(x1,y1); a quadratic adds (x2,y2); a cubic adds (x3,y3).
struct OutlineCurve {
    float x0 = 0;
    float y0 = 0;
    float x1 = 0;
    float y1 = 0;
    float x2 = 0;
    float y2 = 0;
    float x3 = 0;
    float y3 = 0;
    std::int32_t kind = 0;
};

/// Unhinted outline for GPU coverage. `advance` is in pixels at `px`. The box is em units.
/// An empty `curves` vector with `ok` is a glyph that has no ink (a space).
struct OutlineGlyph {
    bool ok = false;
    float advance = 0;
    float min_x = 0;
    float min_y = 0;
    float max_x = 0;
    float max_y = 0;
    std::uint32_t face_key = 0;
    std::uint32_t glyph_index = 0;
    std::vector<OutlineCurve> curves;
};

/// Faces opened from a CSS font-family list, then the platform fallback chain.
/// A codepoint uses the first face that actually has the glyph.
class FontChain {
public:
    FontChain();
    ~FontChain();

    FontChain(const FontChain&) = delete;
    FontChain& operator=(const FontChain&) = delete;

    // weight is CSS font-weight (400 normal / 700 bold). italic selects italic/oblique faces.
    bool init(std::string_view families, int weight = 400, bool italic = false);
    bool ready() const { return !faces_.empty(); }

    float ascent() const { return ascent_; }
    /// Size the faces and refresh `ascent` / `descent` / `x_height`. Layout asks this once per
    /// box for the strut; it does not need the space glyph's cached advance.
    void use_px(float px);
    float descent() const { return descent_; }
    /// Win font box of the face that draws `cp`, ascent and descent each rounded to a whole
    /// pixel and then added. A fallback face can be taller than the chain's first face: Arial
    /// at 15px is 17, and the CJK face Edge uses for the glyphs Arial lacks is 21.
    float glyph_font_box(std::uint32_t cp, float px);
    // OS/2 sxHeight at the current pixel size. Zero until a face has been sized.
    float x_height() const { return x_height_; }
    /// OS/2 `xAvgCharWidth` at the current pixel size. Text-field intrinsic width (HTML
    /// rendering / Blink `GetAvgCharWidth`) reads this; zero until a face has been sized.
    float avg_char_width() const { return avg_char_width_; }
    /// `hhea.advanceWidthMax` at the current pixel size. Paired with `avg_char_width` for the
    /// IE-compatible text-field preferred width.
    float max_char_width() const { return max_char_width_; }
    int weight() const { return weight_; }
    bool italic() const { return italic_; }

    // Optional weight/italic override the values from init(); face_key mixes path + weight + italic
    // so bold vs regular (or italic) never collapse into one shaped run.
    /// `want_path` fills `LoadedGlyph::path` with the file the glyph resolved to. Nothing in the
    /// layout path reads it (only the font-resolution tests do), and filling it costs a string copy
    /// per glyph - which is a heap allocation, once per glyph of every shaped run.
    /// `glyph_id` 0 loads the cmap glyph for `cp`. Any other id loads that glyph from the face
    /// `cp` resolves to, which is how a ligature is painted from its first codepoint.
    /// `pen_x` / `pen_y` are the baseline the glyph is painted at. Only the fractional part is
    /// used, and only when `render` is set: Windows rasterizes that phase with DirectWrite.
    /// The advance does not depend on it.
    // `coverage_green` is the text color's green channel. Rendered coverage is warped with
    // Skia's sRGB contrast table for that channel (`text/mask_gamma.h`). Measurement ignores it.
    LoadedGlyph load(std::uint32_t cp, float px, bool render, int weight = -1, bool italic = false,
                     bool want_path = false, std::uint32_t glyph_id = 0, float pen_x = 0,
                     float pen_y = 0, std::uint8_t coverage_green = 0);
    /// Unhinted outline in em units. Synthetic bold emboldens the outline before it is read.
    /// Does not rasterize. `glyph_id` 0 selects the cmap glyph.
    bool capture_outline(std::uint32_t cp, float px, std::uint32_t glyph_id, OutlineGlyph& out);
    // Extra advance between two glyphs of one face at the current pixel size. Zero when the font has no kerning.
    float kerning(std::uint32_t face_key, std::uint32_t left_glyph, std::uint32_t right_glyph);

    /// HarfBuzz glyph ids and advances for a same-face codepoint span at `px`. Writes one slot per
    /// codepoint. The advances are the FreeType base advances and carry no pair kerning: callers add
    /// `kerning()`, the same value paint uses, because HarfBuzz answers in 1/64 px and its rounded
    /// kern delta would drift the run width. `ligatures` enables `liga` and splits each ligature
    /// across its source codepoints. `draw_out`, when set, is 0 for a ligature continuation.
    /// Returns false when HarfBuzz is off, the face is unknown, or shaping cannot be expanded back
    /// to one slot per codepoint — callers keep FreeType advances plus `kerning()` then.
    /// `cmap_glyphs` is optional: the caller's codepoint-to-glyph answer, with `advances_out`
    /// pre-filled. HarfBuzz keeps the glyph for most slots and the advance is then the caller's
    /// (same FreeType load flags, same `linearHoriAdvance`), so the per-glyph load is skipped.
    bool shape_advances(std::uint32_t face_key, float px, const std::uint32_t* cps,
                        std::size_t count, float* advances_out, std::uint32_t* glyphs_out,
                        bool ligatures = true, std::uint8_t* draw_out = nullptr,
                        const std::uint32_t* cmap_glyphs = nullptr);

private:
    /// Drop this chain's references to the process-wide face pool. The FreeType library stays.
    void release_open_faces();
    /// Put every face at `size_q` (1/64 px). A shared face may have been resized by another chain
    /// since this chain last asked, so a matching `px_q_` is not enough.
    void ensure_px(std::int64_t size_q);

    struct Face;
    /// Advance for one pixel size and codepoint. Paint still rasterizes on a miss;
    /// repeated measure() calls do not reload the outline.
    struct Metric {
        bool ok = false;
        float advance = 0;
        int face_index = 0;
        std::uint32_t glyph_index = 0;
        std::uint32_t face_key = 0;
    };
    std::vector<Face> faces_;
    std::vector<std::uint8_t> bitmap_;
    std::unordered_map<std::uint64_t, Metric> metrics_;
    void* library_ = nullptr;
    /// Used pixel size in 1/64 px units, so a fractional size is a distinct cache entry.
    std::int64_t px_q_ = 0;
    float ascent_ = 0;
    float descent_ = 0;
    float x_height_ = 0;
    float avg_char_width_ = 0;
    float max_char_width_ = 0;
    int weight_ = 400;
    bool italic_ = false;
};

// Temporary submit split. Remove with the probe clocks.
struct OutlineCaptureDiag {
    double load_ms = 0;
    double ft_load_ms = 0;
    double decompose_ms = 0;
    std::uint64_t calls = 0;
};
OutlineCaptureDiag take_outline_capture_diag();

}  // namespace text
}  // namespace zui

#endif
