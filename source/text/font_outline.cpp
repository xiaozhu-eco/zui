#include "text/font_chain.h"
#include "text/font_chain_internal.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_SYNTHESIS_H

#include <vector>

namespace zui {
namespace text {

namespace {

struct OutlineSink {
    std::vector<OutlineCurve>* curves = nullptr;
    float scale = 0;
    float sx = 0;
    float sy = 0;
    float cx = 0;
    float cy = 0;
    bool open = false;
    bool any = false;
    float min_x = 0;
    float min_y = 0;
    float max_x = 0;
    float max_y = 0;

    float em(FT_Pos v) const { return static_cast<float>(v) * scale; }
    void include(float x, float y) {
        if (!any) {
            min_x = max_x = x;
            min_y = max_y = y;
            any = true;
            return;
        }
        min_x = std::min(min_x, x);
        min_y = std::min(min_y, y);
        max_x = std::max(max_x, x);
        max_y = std::max(max_y, y);
    }
    void line_to(float x, float y) {
        OutlineCurve c;
        c.kind = 1;
        c.x0 = cx;
        c.y0 = cy;
        c.x1 = x;
        c.y1 = y;
        curves->push_back(c);
        include(x, y);
        cx = x;
        cy = y;
    }
    void close() {
        if (!open) {
            return;
        }
        if (cx != sx || cy != sy) {
            line_to(sx, sy);
        }
        open = false;
    }
};

int outline_move(const FT_Vector* to, void* user) {
    auto* sink = static_cast<OutlineSink*>(user);
    sink->close();
    sink->cx = sink->sx = sink->em(to->x);
    sink->cy = sink->sy = sink->em(to->y);
    sink->include(sink->cx, sink->cy);
    sink->open = true;
    return 0;
}

int outline_line(const FT_Vector* to, void* user) {
    auto* sink = static_cast<OutlineSink*>(user);
    sink->line_to(sink->em(to->x), sink->em(to->y));
    return 0;
}

int outline_conic(const FT_Vector* control, const FT_Vector* to, void* user) {
    auto* sink = static_cast<OutlineSink*>(user);
    OutlineCurve c;
    c.kind = 2;
    c.x0 = sink->cx;
    c.y0 = sink->cy;
    c.x1 = sink->em(control->x);
    c.y1 = sink->em(control->y);
    c.x2 = sink->em(to->x);
    c.y2 = sink->em(to->y);
    sink->curves->push_back(c);
    sink->include(c.x1, c.y1);
    sink->include(c.x2, c.y2);
    sink->cx = c.x2;
    sink->cy = c.y2;
    return 0;
}

int outline_cubic(const FT_Vector* c1, const FT_Vector* c2, const FT_Vector* to, void* user) {
    auto* sink = static_cast<OutlineSink*>(user);
    OutlineCurve c;
    c.kind = 3;
    c.x0 = sink->cx;
    c.y0 = sink->cy;
    c.x1 = sink->em(c1->x);
    c.y1 = sink->em(c1->y);
    c.x2 = sink->em(c2->x);
    c.y2 = sink->em(c2->y);
    c.x3 = sink->em(to->x);
    c.y3 = sink->em(to->y);
    sink->curves->push_back(c);
    sink->include(c.x1, c.y1);
    sink->include(c.x2, c.y2);
    sink->include(c.x3, c.y3);
    sink->cx = c.x3;
    sink->cy = c.y3;
    return 0;
}

}  // namespace

namespace {

struct OutlineCaptureClock {
    double load_ms = 0;
    double ft_load_ms = 0;
    double decompose_ms = 0;
    std::uint64_t calls = 0;
};

OutlineCaptureClock g_outline_capture;

}  // namespace

OutlineCaptureDiag take_outline_capture_diag() {
    OutlineCaptureDiag out;
    out.load_ms = g_outline_capture.load_ms;
    out.ft_load_ms = g_outline_capture.ft_load_ms;
    out.decompose_ms = g_outline_capture.decompose_ms;
    out.calls = g_outline_capture.calls;
    g_outline_capture = {};
    return out;
}

bool FontChain::capture_outline(std::uint32_t cp, float px, std::uint32_t glyph_id, OutlineGlyph& out) {
    out = {};
    const LoadedGlyph measured = load(cp, px, false, -1, false, false, glyph_id);
    ++g_outline_capture.calls;
    if (!measured.ok || px <= 0.f || measured.glyph_index == 0) {
        return false;
    }
    Face* chosen = nullptr;
    for (Face& face : faces_) {
        if (face.key == measured.face_key && face.face != nullptr) {
            chosen = &face;
            break;
        }
    }
    if (chosen == nullptr) {
        return false;
    }
    if (FT_Load_Glyph(chosen->face, measured.glyph_index, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) !=
        0) {
        return false;
    }
    FT_GlyphSlot slot = chosen->face->glyph;
    if (chosen->synthetic_bold && slot->format == FT_GLYPH_FORMAT_OUTLINE) {
        FT_GlyphSlot_Embolden(slot);
    }
    out.ok = true;
    out.advance = measured.advance;
    out.face_key = measured.face_key;
    out.glyph_index = measured.glyph_index;
    if (slot->format != FT_GLYPH_FORMAT_OUTLINE || slot->outline.n_points <= 0) {
        return true;
    }
    const float sized = px_q_ > 0 ? static_cast<float>(px_q_) / 64.f : px;
    OutlineSink sink;
    sink.curves = &out.curves;
    sink.scale = 1.f / (64.f * sized);
    FT_Outline_Funcs funcs{};
    // The callbacks are C++ functions. FreeType's typedefs have C linkage; the calling
    // convention matches, so the cast is the assignment clang-cl accepts.
    funcs.move_to = reinterpret_cast<FT_Outline_MoveToFunc>(outline_move);
    funcs.line_to = reinterpret_cast<FT_Outline_LineToFunc>(outline_line);
    funcs.conic_to = reinterpret_cast<FT_Outline_ConicToFunc>(outline_conic);
    funcs.cubic_to = reinterpret_cast<FT_Outline_CubicToFunc>(outline_cubic);
    if (FT_Outline_Decompose(&slot->outline, &funcs, &sink) != 0) {
        out.curves.clear();
        return false;
    }
    sink.close();
    out.min_x = sink.min_x;
    out.min_y = sink.min_y;
    out.max_x = sink.max_x;
    out.max_y = sink.max_y;
    return true;
}

}  // namespace text
}  // namespace zui
