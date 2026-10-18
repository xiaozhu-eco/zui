#include "backends/glyph_outlines.h"

#include "backends/draw_effect.h"
#include "text/font_chain.h"

#include <chrono>
#include <memory>
#include <string>

namespace zui {
namespace backends {

struct GlyphOutlines::Chain {
    text::FontChain fonts;
};

namespace {

struct GlyphPaintClock {
    double prepare_ms = 0;
    double load_ms = 0;
    double xfer_ms = 0;
    double append_ms = 0;
    double capture_ms = 0;
    std::uint64_t glyphs = 0;
    std::uint64_t span_miss = 0;
};

GlyphPaintClock g_glyph_paint;

}  // namespace

GlyphPaintDiag take_glyph_paint_diag() {
    GlyphPaintDiag out;
    out.prepare_ms = g_glyph_paint.prepare_ms;
    out.load_ms = g_glyph_paint.load_ms;
    out.xfer_ms = g_glyph_paint.xfer_ms;
    out.append_ms = g_glyph_paint.append_ms;
    out.capture_ms = g_glyph_paint.capture_ms;
    out.glyphs = g_glyph_paint.glyphs;
    out.span_miss = g_glyph_paint.span_miss;
    g_glyph_paint = {};
    return out;
}

GlyphOutlines::GlyphOutlines() = default;
GlyphOutlines::~GlyphOutlines() = default;

GlyphOutlines::Chain* GlyphOutlines::chain_for(std::string_view family, int weight, bool italic) {
    if (weight <= 0) {
        weight = 400;
    }
    const std::string_view family_key = family.empty() ? std::string_view("sans-serif") : family;
    if (last_valid_ && last_chain_ != nullptr && last_weight_ == weight && last_italic_ == italic &&
        last_family_ == family_key) {
        return last_chain_;
    }
    std::string key(family_key);
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
    last_family_.assign(family_key);
    last_weight_ = weight;
    last_italic_ = italic;
    last_chain_ = it->second.get();
    last_valid_ = true;
    return last_chain_;
}

GlyphOutlines::Draw GlyphOutlines::prepare(std::uint32_t cp, float font_px, std::string_view family,
                                           int weight, bool italic, std::uint32_t glyph_id) {
    Draw out;
    if (font_px <= 0.f) {
        return out;
    }
    Chain* chain = chain_for(family, weight, italic);
    if (chain == nullptr) {
        return out;
    }
    const text::LoadedGlyph measured =
        chain->fonts.load(cp, font_px, false, weight, italic, false, glyph_id);
    if (!measured.ok || measured.glyph_index == 0) {
        return out;
    }
    const std::uint64_t key = (static_cast<std::uint64_t>(measured.face_key) << 32) |
                              static_cast<std::uint64_t>(measured.glyph_index);
    auto found = spans_.find(key);
    if (found == spans_.end()) {
        ++g_glyph_paint.span_miss;
        text::OutlineGlyph outline;
        const bool captured = chain->fonts.capture_outline(cp, font_px, glyph_id, outline);
        if (!captured || !outline.ok) {
            return out;
        }
        Span span;
        span.curve0 = static_cast<std::uint32_t>(curves_.size());
        span.count = static_cast<std::uint32_t>(outline.curves.size());
        span.min_x = outline.min_x;
        span.min_y = outline.min_y;
        span.max_x = outline.max_x;
        span.max_y = outline.max_y;
        span.advance_em = outline.advance / font_px;
        // `reserve(size + n)` every glyph made capacity land on size, so the next glyph
        // allocated again and copied the whole shelf. A page of new outlines was quadratic.
        const std::size_t need = curves_.size() + outline.curves.size();
        if (need > curves_.capacity()) {
            std::size_t cap = curves_.capacity() < 256 ? 256 : curves_.capacity();
            while (cap < need) {
                cap *= 2;
            }
            curves_.reserve(cap);
        }
        for (const text::OutlineCurve& src : outline.curves) {
            GpuGlyphCurve dst{};
            dst.a[0] = src.x0;
            dst.a[1] = src.y0;
            dst.a[2] = src.x1;
            dst.a[3] = src.y1;
            dst.b[0] = src.x2;
            dst.b[1] = src.y2;
            dst.b[2] = src.x3;
            dst.b[3] = src.y3;
            dst.kind = src.kind;
            curves_.push_back(dst);
        }
        found = spans_.emplace(key, span).first;
    }
    const Span& span = found->second;
    out.ok = true;
    out.curve0 = span.curve0;
    out.count = span.count;
    out.min_x = span.min_x;
    out.min_y = span.min_y;
    out.max_x = span.max_x;
    out.max_y = span.max_y;
    out.advance = span.advance_em * font_px;
    out.face_key = measured.face_key;
    out.glyph_index = measured.glyph_index;
    return out;
}

float GlyphOutlines::kerning(std::string_view family, int weight, bool italic, float font_px,
                             std::uint32_t left_face, std::uint32_t left_glyph,
                             std::uint32_t right_face, std::uint32_t right_glyph) {
    if (left_face == 0 || right_face == 0 || left_face != right_face || left_glyph == 0 ||
        right_glyph == 0 || font_px <= 0.f) {
        return 0.f;
    }
    Chain* chain = chain_for(family, weight, italic);
    if (chain == nullptr) {
        return 0.f;
    }
    // Size the face. Kerning reads the chain's current pixel size.
    if (!chain->fonts.load(static_cast<std::uint32_t>(' '), font_px, false).ok &&
        !chain->fonts.ready()) {
        return 0.f;
    }
    return chain->fonts.kerning(left_face, left_glyph, right_glyph);
}

void append_glyph_quads(GlyphOutlines& outlines, const paint::DisplayItem& it,
                        std::vector<GlyphVert>& verts) {
    if (it.font_px <= 0.f || it.text.empty()) {
        return;
    }
    const float px = it.font_px;
    // 1.25px of AA, plus the gaussian support when this draw is a blurred shadow. The pad is in
    // em; the matrix scales it by font_px, so the screen outset does not depend on baking px
    // into the outline.
    const float spread = it.blur > 0.f ? it.blur * 1.5f : 0.f;
    const float pad = (1.25f + spread) / px;
    const float cr = it.color.r / 255.f;
    const float cg = it.color.g / 255.f;
    const float cb = it.color.b / 255.f;
    const float ca = it.color.a / 255.f;
    float pen_x = it.rect.x;
    float pen_y = it.rect.y;
    const paint::TextGlyphOrient orient = it.text_orient;
    std::size_t i = 0;
    std::size_t advance_index = 0;
    const bool use_layout = !it.glyph_advances.empty();
    const bool use_ids = use_layout && it.glyph_ids.size() == it.glyph_advances.size();
    std::uint32_t prev_face = 0;
    std::uint32_t prev_glyph = 0;
    bool have_prev = false;
    while (i < it.text.size()) {
        const std::uint32_t cp = text::next_cp(it.text, i);
        if (cp == 0) {
            break;
        }
        const std::uint32_t paint_gid =
            use_ids && advance_index < it.glyph_ids.size() ? it.glyph_ids[advance_index] : 0u;
        if (paint_gid == text::kSkipPaintGlyph) {
            const float step = use_layout && advance_index < it.glyph_advances.size()
                                   ? it.glyph_advances[advance_index]
                                   : 0.f;
            ++advance_index;
            if (orient == paint::TextGlyphOrient::Upright) {
                pen_x += step;
            } else {
                pen_y += step;
            }
            continue;
        }
        ++g_glyph_paint.glyphs;
        const GlyphOutlines::Draw g = outlines.prepare(cp, px, it.font_family, it.font_weight,
                                                       it.font_italic, paint_gid);
        const bool from_layout = use_layout && advance_index < it.glyph_advances.size();
        float kern = 0.f;
        // Layout advances already include HarfBuzz kerning. Asking GPOS again walked the pair
        // table once per glyph and then discarded the result (`kern = 0` below).
        if (!from_layout && !it.font_kerning_none && have_prev && g.ok) {
            kern = outlines.kerning(it.font_family, it.font_weight, it.font_italic, px, prev_face,
                                    prev_glyph, g.face_key, g.glyph_index);
        }
        const float step =
            from_layout ? it.glyph_advances[advance_index]
                        : (g.ok ? g.advance + it.letter_spacing + kern : 0.f);
        ++advance_index;
        if (from_layout) {
            kern = 0.f;
        }
        if (orient == paint::TextGlyphOrient::Upright) {
            pen_x += kern;
        } else {
            pen_y += kern;
        }
        if (g.ok && g.count > 0) {
            const float minx = g.min_x - pad;
            const float maxx = g.max_x + pad;
            const float miny = g.min_y - pad;
            const float maxy = g.max_y + pad;
            const float emx[4] = {minx, maxx, maxx, minx};
            const float emy[4] = {maxy, maxy, miny, miny};
            const GlyphMatrix m =
                glyph_matrix(px, orient, pen_x, pen_y, it.has_xform, it.xform_a, it.xform_b,
                             it.xform_c, it.xform_d, it.xform_e, it.xform_f);
            const int order[6] = {0, 1, 2, 0, 2, 3};
            for (int k : order) {
                GlyphVert v;
                v.em_x = emx[k];
                v.em_y = emy[k];
                v.r = cr;
                v.g = cg;
                v.b = cb;
                v.a = ca;
                v.curve0 = g.curve0;
                v.count = g.count;
                v.m00 = m.m00;
                v.m01 = m.m01;
                v.m02 = m.m02;
                v.m10 = m.m10;
                v.m11 = m.m11;
                v.m12 = m.m12;
                verts.push_back(v);
            }
        }
        if (orient == paint::TextGlyphOrient::Upright) {
            pen_x += step;
        } else {
            pen_y += step;
        }
        if (g.ok) {
            prev_face = g.face_key;
            prev_glyph = g.glyph_index;
            have_prev = true;
        }
    }
}

}  // namespace backends
}  // namespace zui
