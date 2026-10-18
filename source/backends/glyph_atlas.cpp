#include "backends/glyph_atlas.h"

#include "text/font_chain.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace zui {
namespace backends {

struct GlyphAtlas::Chain {
    text::FontChain fonts;
};

GlyphAtlas::GlyphAtlas() {
    pixels_.assign(static_cast<size_t>(width_ * height_), 0);
    pixels_[0] = 255;
    auto created = std::make_unique<Chain>();
    ready_ = created->fonts.init("sans-serif");
    if (ready_) {
        chains_.emplace("sans-serif", std::move(created));
    }
}

GlyphAtlas::~GlyphAtlas() = default;

bool GlyphAtlas::pack(int w, int h, int& x, int& y) {
    if (row_x_ + w + 1 >= width_) {
        row_y_ += row_h_ + 1;
        row_x_ = 1;
        row_h_ = 0;
    }
    if (row_y_ + h + 1 >= height_) {
        // Distinct from "this glyph has no ink". Callers grow once and retry; once the device
        // edge is reached, `grow` latches `grow_exhausted_` so a full atlas does not ask again.
        if (!grow_exhausted_) {
            needs_grow_ = true;
        }
        return false;
    }
    x = row_x_;
    y = row_y_;
    row_x_ += w + 1;
    row_h_ = std::max(row_h_, h);
    return true;
}

void GlyphAtlas::set_max_edge(int edge) {
    if (edge >= 64) {
        max_edge_ = edge;
    }
}

void GlyphAtlas::reserve_edge(int edge) {
    if (edge < 64) {
        edge = 64;
    }
    if (edge > max_edge_) {
        edge = max_edge_;
    }
    if (!glyphs_.empty()) {
        while ((width_ < edge || height_ < edge) && grow()) {
        }
        return;
    }
    if (edge <= width_ && edge <= height_) {
        return;
    }
    width_ = edge;
    height_ = edge;
    pixels_.assign(static_cast<size_t>(width_) * static_cast<size_t>(height_), 0);
    pixels_[0] = 255;
    row_x_ = 1;
    row_y_ = 1;
    row_h_ = 0;
    dirty_ = true;
}

bool GlyphAtlas::grow() {
    if (width_ >= max_edge_ || height_ >= max_edge_) {
        grow_exhausted_ = true;
        needs_grow_ = false;
        return false;
    }
    const int nw = std::min(width_ * 2, max_edge_);
    const int nh = std::min(height_ * 2, max_edge_);
    if (nw == width_ && nh == height_) {
        grow_exhausted_ = true;
        needs_grow_ = false;
        return false;
    }
    std::vector<std::uint8_t> next(static_cast<size_t>(nw) * static_cast<size_t>(nh), 0);
    for (int row = 0; row < height_; ++row) {
        std::memcpy(next.data() + static_cast<size_t>(row) * static_cast<size_t>(nw),
                    pixels_.data() + static_cast<size_t>(row) * static_cast<size_t>(width_),
                    static_cast<size_t>(width_));
    }
    // UVs are fractions of the texture. The pixels stay in the top-left, so every stored glyph
    // moves by old/new. Callers must do this before they bake UVs for the frame.
    const float sx = static_cast<float>(width_) / static_cast<float>(nw);
    const float sy = static_cast<float>(height_) / static_cast<float>(nh);
    for (auto& entry : glyphs_) {
        Glyph& g = entry.second;
        g.u0 *= sx;
        g.u1 *= sx;
        g.v0 *= sy;
        g.v1 *= sy;
    }
    pixels_ = std::move(next);
    width_ = nw;
    height_ = nh;
    // The pack cursor still points at the first free pixel. The texture is wider, so the current
    // row continues; the next wrap lands below the copied rows, which the doubled height covers.
    dirty_ = true;
    needs_grow_ = false;
    return true;
}

const Glyph* GlyphAtlas::get(std::uint32_t cp, float font_px, std::string_view family, int weight,
                             bool italic, std::uint32_t glyph_id, float pen_x, float pen_y,
                             std::uint8_t coverage_green) {
    if (font_px <= 0) {
        return nullptr;
    }
    if (weight <= 0) {
        weight = 400;
    }
    std::string family_key = family.empty() ? std::string("sans-serif") : std::string(family);
    // One FontChain per (family, weight, italic): bold and regular must not share a face list.
    std::string key = family_key;
    key.push_back('\0');
    key.append(std::to_string(weight));
    key.push_back(italic ? 'i' : 'n');
    auto it = chains_.find(key);
    if (it == chains_.end()) {
        auto created = std::make_unique<Chain>();
        if (!created->fonts.init(family_key, weight, italic)) {
            return nullptr;
        }
        it = chains_.emplace(std::move(key), std::move(created)).first;
    }
    text::FontChain& fonts = it->second->fonts;
    // Key and load with the same 1/64 px size the layout engine measured at, otherwise a
    // fractional font size would be rasterized (and advanced) at a rounded one.
    const std::int64_t px_q =
        std::max<std::int64_t>(64, static_cast<std::int64_t>(font_px * 64.0f + 0.5f));
    // FNV-1a. XORing the subpixel phase straight into the low bits aliases CJK: phase is 0..3,
    // so `phase << 8` is 0x100/0x200/0x300, and U+7684 at phase 0 collides with U+7784 at phase 1.
    // A chapter then paints the wrong character for every pair that shares a slot. Mix each field
    // on its own so a phase, a weight, or a coverage channel cannot cancel a codepoint.
    std::uint64_t gkey = 14695981039346656037ull;
    auto mix = [&gkey](std::uint64_t value) {
        gkey ^= value;
        gkey *= 1099511628211ull;
    };
    for (unsigned char c : family_key) {
        mix(c);
    }
    mix(static_cast<std::uint64_t>(px_q));
    mix(cp);
    mix(static_cast<std::uint64_t>(weight));
    mix(italic ? 1ull : 0ull);
    mix(glyph_id);
    mix(static_cast<std::uint64_t>(text::glyph_pixel_phase(pen_x)));
    mix(static_cast<std::uint64_t>(text::glyph_pixel_phase(pen_y)));
    mix(coverage_green);
    auto found = glyphs_.find(gkey);
    if (found != glyphs_.end()) {
        return &found->second;
    }
    const text::LoadedGlyph loaded =
        fonts.load(cp, static_cast<float>(px_q) / 64.0f, true, weight, italic, false, glyph_id,
                   pen_x, pen_y, coverage_green);
    if (!loaded.ok) {
        return nullptr;
    }
    const int gw = static_cast<int>(loaded.width + 0.5f);
    const int gh = static_cast<int>(loaded.height + 0.5f);
    int x = 0;
    int y = 0;
    if (gw > 0 && gh > 0 && !pack(gw, gh, x, y)) {
        return nullptr;
    }
    if (gw > 0 && gh > 0 && loaded.pixels != nullptr) {
        for (int row = 0; row < gh; ++row) {
            const std::uint8_t* src = loaded.pixels + static_cast<size_t>(row * gw);
            std::uint8_t* dst = pixels_.data() + static_cast<size_t>((y + row) * width_ + x);
            for (int col = 0; col < gw; ++col) {
                dst[col] = src[col];
            }
        }
        dirty_ = true;
    }
    Glyph g;
    g.w = loaded.width;
    g.h = loaded.height;
    g.bearing_x = loaded.bearing_x;
    g.bearing_y = loaded.bearing_y;
    g.advance = loaded.advance;
    g.face_key = loaded.face_key;
    g.glyph_index = loaded.glyph_index;
    g.u0 = static_cast<float>(x) / static_cast<float>(width_);
    g.v0 = static_cast<float>(y) / static_cast<float>(height_);
    g.u1 = static_cast<float>(x + gw) / static_cast<float>(width_);
    g.v1 = static_cast<float>(y + gh) / static_cast<float>(height_);
    auto [ins, _] = glyphs_.emplace(gkey, g);
    return &ins->second;
}

float GlyphAtlas::kerning(std::string_view family, int weight, bool italic, float font_px,
                          std::uint32_t left_face, std::uint32_t left_glyph,
                          std::uint32_t right_face, std::uint32_t right_glyph) {
    if (left_face == 0 || right_face == 0 || left_face != right_face || left_glyph == 0 ||
        right_glyph == 0 || font_px <= 0.f) {
        return 0.f;
    }
    if (weight <= 0) {
        weight = 400;
    }
    // Ensure the chain is sized: get() loads a dummy space so FreeType's pixel size matches layout.
    if (get(static_cast<std::uint32_t>(' '), font_px, family, weight, italic) == nullptr) {
        return 0.f;
    }
    std::string family_key = family.empty() ? std::string("sans-serif") : std::string(family);
    std::string key = family_key;
    key.push_back('\0');
    key.append(std::to_string(weight));
    key.push_back(italic ? 'i' : 'n');
    auto it = chains_.find(key);
    if (it == chains_.end()) {
        return 0.f;
    }
    return it->second->fonts.kerning(left_face, left_glyph, right_glyph);
}

}  // namespace backends
}  // namespace zui
