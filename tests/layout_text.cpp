#include <gtest/gtest.h>

#include "backends/draw_effect.h"
#include "backends/glyph_outlines.h"
#include "css/engine/engine.h"
#include "html/document.h"
#include "layout/engine.h"
#include "text/font_chain.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

bool near(float a, float b) { return std::fabs(a - b) < 0.6f; }

struct V2 {
    float x;
    float y;
};

float line_cross(V2 a, V2 b, V2 p) {
    const bool up = a.y <= p.y && b.y > p.y;
    const bool down = b.y <= p.y && a.y > p.y;
    if (!up && !down) {
        return 0.f;
    }
    const float t = (p.y - a.y) / (b.y - a.y);
    const float x = a.x + (b.x - a.x) * t;
    if (x <= p.x) {
        return 0.f;
    }
    return up ? 1.f : -1.f;
}

float quad_at_x(V2 p0, V2 p1, V2 p2, float t) {
    const float u = 1.f - t;
    return u * u * p0.x + 2.f * u * t * p1.x + t * t * p2.x;
}

float mono_quad(V2 p0, V2 p1, V2 p2, V2 p) {
    const bool up = p0.y <= p.y && p2.y > p.y;
    const bool down = p2.y <= p.y && p0.y > p.y;
    if (!up && !down) {
        return 0.f;
    }
    const float a = p0.y - 2.f * p1.y + p2.y;
    const float b = 2.f * (p1.y - p0.y);
    const float c = p0.y - p.y;
    float t = -1.f;
    if (std::fabs(a) < 1e-7f) {
        if (std::fabs(b) < 1e-7f) {
            return 0.f;
        }
        t = -c / b;
    } else {
        const float disc = std::max(b * b - 4.f * a * c, 0.f);
        const float s = std::sqrt(disc);
        const float inv = 0.5f / a;
        const float t0 = (-b - s) * inv;
        const float t1 = (-b + s) * inv;
        const bool in0 = t0 >= -1e-4f && t0 <= 1.f + 1e-4f;
        const bool in1 = t1 >= -1e-4f && t1 <= 1.f + 1e-4f;
        const float yp0 = 2.f * a * t0 + b;
        const float yp1 = 2.f * a * t1 + b;
        const bool sign0 = up ? yp0 >= 0.f : yp0 <= 0.f;
        const bool sign1 = up ? yp1 >= 0.f : yp1 <= 0.f;
        if (in0 && (!in1 || sign0 || !sign1)) {
            t = std::min(1.f, std::max(0.f, t0));
        } else if (in1) {
            t = std::min(1.f, std::max(0.f, t1));
        } else {
            return 0.f;
        }
    }
    if (quad_at_x(p0, p1, p2, t) <= p.x) {
        return 0.f;
    }
    return up ? 1.f : -1.f;
}

float quad_cross(V2 p0, V2 p1, V2 p2, V2 p) {
    const float a = p0.y - 2.f * p1.y + p2.y;
    const float b = 2.f * (p1.y - p0.y);
    if (std::fabs(a) >= 1e-7f) {
        const float ts = -b / (2.f * a);
        if (ts > 1e-4f && ts < 1.f - 1e-4f) {
            const V2 p01{p0.x + (p1.x - p0.x) * ts, p0.y + (p1.y - p0.y) * ts};
            const V2 p12{p1.x + (p2.x - p1.x) * ts, p1.y + (p2.y - p1.y) * ts};
            const V2 mid{p01.x + (p12.x - p01.x) * ts, p01.y + (p12.y - p01.y) * ts};
            return mono_quad(p0, p01, mid, p) + mono_quad(mid, p12, p2, p);
        }
    }
    return mono_quad(p0, p1, p2, p);
}

V2 cubic_at(V2 p0, V2 p1, V2 p2, V2 p3, float t) {
    const float u = 1.f - t;
    return V2{u * u * u * p0.x + 3.f * u * u * t * p1.x + 3.f * u * t * t * p2.x + t * t * t * p3.x,
              u * u * u * p0.y + 3.f * u * u * t * p1.y + 3.f * u * t * t * p2.y + t * t * t * p3.y};
}

float flat_quad(V2 p0, V2 p1, V2 p2, V2 p) {
    float w = 0.f;
    V2 prev = p0;
    for (int s = 1; s <= 32; ++s) {
        const float t = static_cast<float>(s) / 32.f;
        const float u = 1.f - t;
        const V2 cur{u * u * p0.x + 2.f * u * t * p1.x + t * t * p2.x,
                     u * u * p0.y + 2.f * u * t * p1.y + t * t * p2.y};
        w += line_cross(prev, cur, p);
        prev = cur;
    }
    return w;
}

float cubic_cross(V2 p0, V2 p1, V2 p2, V2 p3, V2 p) {
    float w = 0.f;
    V2 prev = p0;
    for (int s = 1; s <= 16; ++s) {
        const V2 cur = cubic_at(p0, p1, p2, p3, static_cast<float>(s) / 16.f);
        w += line_cross(prev, cur, p);
        prev = cur;
    }
    return w;
}

bool inside_shader(const std::vector<zui::text::OutlineCurve>& curves, V2 p) {
    float w = 0.f;
    for (const zui::text::OutlineCurve& c : curves) {
        const V2 p0{c.x0, c.y0};
        const V2 p1{c.x1, c.y1};
        if (c.kind == 1) {
            w += line_cross(p0, p1, p);
        } else if (c.kind == 2) {
            w += quad_cross(p0, p1, V2{c.x2, c.y2}, p);
        } else if (c.kind == 3) {
            w += cubic_cross(p0, p1, V2{c.x2, c.y2}, V2{c.x3, c.y3}, p);
        }
    }
    return std::fabs(w) > 0.5f;
}

bool inside_flat(const std::vector<zui::text::OutlineCurve>& curves, V2 p) {
    float w = 0.f;
    for (const zui::text::OutlineCurve& c : curves) {
        const V2 p0{c.x0, c.y0};
        const V2 p1{c.x1, c.y1};
        if (c.kind == 1) {
            w += line_cross(p0, p1, p);
        } else if (c.kind == 2) {
            w += flat_quad(p0, p1, V2{c.x2, c.y2}, p);
        } else if (c.kind == 3) {
            w += cubic_cross(p0, p1, V2{c.x2, c.y2}, V2{c.x3, c.y3}, p);
        }
    }
    return std::fabs(w) > 0.5f;
}

// The old t-in-[0,1) quadratic test fills a whole scanline when the sample y is a shared
// vertex. The shader rule and a line flattening of the same outline must agree there.
bool winding_matches_lines(const zui::text::OutlineGlyph& glyph) {
    float min_x = glyph.min_x;
    float max_x = glyph.max_x;
    std::vector<float> ys;
    ys.reserve(glyph.curves.size() * 2);
    for (const zui::text::OutlineCurve& c : glyph.curves) {
        ys.push_back(c.y0);
        if (c.kind == 1) {
            ys.push_back(c.y1);
        } else if (c.kind == 2) {
            ys.push_back(c.y2);
        } else if (c.kind == 3) {
            ys.push_back(c.y3);
        }
    }
    constexpr int kCols = 48;
    for (float y : ys) {
        for (int i = 0; i < kCols; ++i) {
            const float x = min_x + (max_x - min_x) * (static_cast<float>(i) + 0.5f) / kCols;
            const V2 p{x, y};
            if (inside_shader(glyph.curves, p) != inside_flat(glyph.curves, p)) {
                return false;
            }
        }
    }
    return true;
}

float advance_span(const zui::layout::Box* box) {
    if (box == nullptr) {
        return 0.f;
    }
    float sum = 0.f;
    for (const zui::layout::TextRun& run : box->geo.runs) {
        for (float advance : run.glyph_advances) {
            sum += advance;
        }
    }
    for (const zui::layout::Box* child : box->children) {
        sum += advance_span(child);
    }
    return sum;
}

zui::layout::Box* text_child(zui::layout::Box* box) {
    if (box == nullptr) {
        return nullptr;
    }
    for (zui::layout::Box* ch : box->children) {
        if (ch->role == zui::layout::BoxRole::Text) {
            return ch;
        }
    }
    return nullptr;
}

}  // namespace

TEST(LayoutText, FallbackAndWrap) {
    zui::text::FontChain latin_chain;
    if (!latin_chain.init("sans-serif")) {
        FAIL() << "no font face opened\n";
    }
    const zui::text::LoadedGlyph latin =
        latin_chain.load(static_cast<uint32_t>('A'), 16.0f, false, -1, false, /*want_path=*/true);
    const zui::text::LoadedGlyph cjk =
        latin_chain.load(0x4E2D, 16.0f, false, -1, false, /*want_path=*/true);
    if (!latin.ok || latin.advance <= 0) {
        FAIL() << "latin glyph missing\n";
    }
    const bool have_arial = std::filesystem::exists("C:\\Windows\\Fonts\\arial.ttf");
    const bool have_yahei = std::filesystem::exists("C:\\Windows\\Fonts\\msyh.ttc") ||
                            std::filesystem::exists("C:\\Windows\\Fonts\\msyh.ttf");
    // The generic `sans-serif` is resolved the way the platform browser resolves it, and on this
    // machine that is a CJK face which covers Latin as well, so the two may share one face. What
    // has to hold is that both scripts are found.
    if (latin.path.empty() || cjk.path.empty()) {
        FAIL() << "sans-serif did not resolve both scripts: " << latin.path << " / " << cjk.path
               << "\n";
    }
    // A Latin-only family still has to reach a CJK face through the fallback chain.
    if (have_arial) {
        zui::text::FontChain arial_only;
        if (arial_only.init("arial")) {
            const zui::text::LoadedGlyph a = arial_only.load(static_cast<uint32_t>('A'), 16.0f, false,
                                                             -1, false, /*want_path=*/true);
            const zui::text::LoadedGlyph c =
                arial_only.load(0x4E2D, 16.0f, false, -1, false, /*want_path=*/true);
            if (!a.ok || a.path.find("arial") == std::string::npos) {
                FAIL() << "arial resolved A to " << a.path << "\n";
            }
            if (!c.ok || c.path == a.path) {
                FAIL() << "arial did not fall back for CJK: " << c.path << "\n";
            }
        }
    }

    zui::text::FontChain yahei;
    if (have_yahei && yahei.init("\"Microsoft YaHei\", sans-serif")) {
        const zui::text::LoadedGlyph a =
            yahei.load(static_cast<uint32_t>('A'), 16.0f, false, -1, false, /*want_path=*/true);
        if (!a.ok || a.path.find("msyh") == std::string::npos) {
            FAIL() << "YaHei family resolved A to " << a.path << "\n";
        }
    }

    // Kanban's column grip is U+22EE VERTICAL ELLIPSIS. YaHei and Noto Sans SC do not carry
    // that operator, and glyph 0 of the first face paints as two empty boxes. The chain has
    // to answer with a face that actually has the character.
    const bool have_symbol = std::filesystem::exists("C:\\Windows\\Fonts\\seguisym.ttf");
    if (have_symbol) {
        zui::text::FontChain ui;
        if (!ui.init("\"Microsoft YaHei\", \"Noto Sans SC\", \"Segoe UI\", sans-serif")) {
            FAIL() << "UI font chain did not open\n";
        }
        const zui::text::LoadedGlyph grip =
            ui.load(0x22EE, 15.0f, false, -1, false, /*want_path=*/true);
        if (!grip.ok || grip.glyph_index == 0 || grip.advance <= 0.f ||
            grip.path.find("seguisym") == std::string::npos) {
            FAIL() << "U+22EE resolved to glyph " << grip.glyph_index << " path " << grip.path
                   << "\n";
        }
    }

    zui::layout::FreeTypeTextMetrics metrics;
    const auto m_a = metrics.measure("A", 16.0f, "sans-serif");
    const auto m_c = metrics.measure("\xE4\xB8\xAD", 16.0f, "sans-serif");
    const auto m_both = metrics.measure("A\xE4\xB8\xAD", 16.0f, "sans-serif");
    if (!near(m_both.width, m_a.width + m_c.width)) {
        FAIL() << "advance sum " << m_both.width << " != " << m_a.width << " + " << m_c.width
               << "\n";
    }

    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/text_wrap.html", doc, error)) {
        FAIL() << error << "\n";
    }
    zui::css::Engine css;
    if (!css.load_sheets(doc, error)) {
        FAIL() << error << "\n";
    }
    css.cascade(doc);
    zui::layout::LayoutEngine layout(metrics);
    layout.layout(doc, zui::layout::Viewport{400.0f, 300.0f});

    // LAYOUT.md section 4: line boxes and glyph runs live on the inline formatting context
    // container (the `<div>` here), while the leaf Text box carries the whole text's own advance.
    // Runs and line-box heights are asserted on the container, the text extent on the leaf.
    zui::layout::Box* wrap = layout.principal_box(zui::html::find(doc, "wrap"));
    zui::layout::Box* nowrap = layout.principal_box(zui::html::find(doc, "nowrap"));
    zui::layout::Box* cjk_box = layout.principal_box(zui::html::find(doc, "cjk"));
    zui::layout::Box* word = layout.principal_box(zui::html::find(doc, "word"));
    if (wrap == nullptr || nowrap == nullptr || cjk_box == nullptr || word == nullptr) {
        FAIL() << "missing boxes\n";
    }
    zui::layout::Box* wrap_text = text_child(wrap);
    zui::layout::Box* nowrap_text = text_child(nowrap);
    zui::layout::Box* word_text = text_child(word);
    if (wrap_text == nullptr || nowrap_text == nullptr || word_text == nullptr) {
        FAIL() << "missing text boxes\n";
    }
    if (wrap->geo.runs.size() < 2 || wrap->geo.height <= nowrap->geo.height + 0.5f) {
        FAIL() << "wrap runs=" << wrap->geo.runs.size() << " h=" << wrap->geo.height
               << " nowrap h=" << nowrap->geo.height << "\n";
    }
    for (const zui::layout::TextRun& run : wrap->geo.runs) {
        const float width = metrics.measure(run.text, 16.0f, "sans-serif").width;
        if (width > 80.5f) {
            FAIL() << "wrapped run wider than 80: " << width << " [" << run.text << "]\n";
        }
    }
    if (nowrap->geo.runs.size() != 1 || nowrap_text->geo.width <= 80.5f) {
        FAIL() << "nowrap text width " << nowrap_text->geo.width << " runs "
               << nowrap->geo.runs.size() << "\n";
    }
    if (have_yahei) {
        if (cjk_box->geo.runs.size() < 2) {
            FAIL() << "cjk runs=" << cjk_box->geo.runs.size() << " w=" << cjk_box->geo.width
                   << "\n";
        }
        for (const zui::layout::TextRun& run : cjk_box->geo.runs) {
            const float width = metrics.measure(run.text, 16.0f, "sans-serif").width;
            if (width > 80.5f || run.text.empty()) {
                FAIL() << "cjk run " << width << " [" << run.text << "]\n";
            }
        }
    }
    if (word->geo.runs.size() != 1 || word_text->geo.width <= 40.5f) {
        FAIL() << "long word was split: runs=" << word->geo.runs.size()
               << " w=" << word_text->geo.width << "\n";
    }
}

// A standalone Text box (an element whose own text sits beside a block-level child, as the reader's
// `<button>` with its `::before` rule) lays its text out in one line, and `shape` splits that line
// into one run per face. Those runs used to be stored without an origin, so every one of them was
// painted at the line start: the reader's "下一章 · 第N章 ..." came out as three runs stacked on the
// first character. The runs have to advance, and the box itself has to honour `text-align`.
TEST(LayoutText, StandaloneTextBoxAdvancesItsRuns) {
    zui::layout::FreeTypeTextMetrics metrics;
    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/reader_title.html", doc, error)) {
        FAIL() << error << "\n";
    }
    zui::css::Engine css;
    if (!css.load_sheets(doc, error)) {
        FAIL() << error << "\n";
    }
    css.cascade(doc, 1100.0f, 4000.0f);
    zui::layout::LayoutEngine layout(metrics);
    layout.layout(doc, zui::layout::Viewport{1100.0f, 4000.0f});

    zui::layout::Box* next = layout.principal_box(zui::html::find(doc, "chapter_next"));
    if (next == nullptr) {
        FAIL() << "no #chapter_next box\n";
    }
    zui::layout::Box* text = text_child(next);
    if (text == nullptr) {
        FAIL() << "the button's text should be its own anonymous box\n";
    }
    const std::vector<zui::layout::TextRun>& runs = text->geo.runs;
    if (runs.size() < 2) {
        FAIL() << "the fixture shaped into " << runs.size() << " run(s)\n";
    }
    if (!near(runs.front().x, 0.0f)) {
        FAIL() << "first run starts at " << runs.front().x << "\n";
    }
    for (std::size_t i = 1; i < runs.size(); ++i) {
        if (!(runs[i].x > runs[i - 1].x + 1.0f)) {
            FAIL() << "run " << i << " at " << runs[i].x << " does not follow run " << i - 1
                   << " at " << runs[i - 1].x << " [" << runs[i].text << "]\n";
        }
    }
    // `.chapter-next` is centred, so the shrink-to-fit text box has to sit inside the button rather
    // than against its left edge.
    if (!(text->geo.world_x > next->geo.world_x + 1.0f)) {
        FAIL() << "text box at " << text->geo.world_x << " was not aligned inside the button at "
               << next->geo.world_x << "\n";
    }
}

// PERF.md §3: a font-size change stays inside the style closure. The text run is interned from
// the element (its node is `#text` or null), so the closure has to re-intern that run. Leaving
// it stale paints the new face on the old advances and the glyphs overlap.
TEST(LayoutText, FontSizeChangeScalesGlyphAdvances) {
    zui::layout::FreeTypeTextMetrics metrics;
    if (!metrics.ready()) {
        FAIL() << "no font\n";
    }
    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/tests/fixtures/font_advance.html", doc, error)) {
        FAIL() << error << "\n";
    }
    zui::css::Engine css;
    if (!css.load_sheets(doc, error)) {
        FAIL() << error << "\n";
    }
    css.cascade(doc, 400.f, 200.f);
    zui::layout::LayoutEngine layout(metrics);
    layout.layout(doc, zui::layout::Viewport{400.f, 200.f});

    zui::html::Node* paragraph = zui::html::find(doc, "p");
    zui::layout::Box* box = layout.principal_box(paragraph);
    zui::layout::Box* text = text_child(box);
    if (paragraph == nullptr || box == nullptr || text == nullptr || text->style_ptr == nullptr) {
        FAIL() << "missing paragraph text box\n";
    }
    const float small_font = text->style().font_size;
    const float small_advances = advance_span(box);
    if (std::fabs(small_font - 16.f) > 0.5f || !(small_advances > 8.f)) {
        FAIL() << "16px run font=" << small_font << " advances=" << small_advances << "\n";
    }

    if (!zui::html::add_class(doc, "p", "big")) {
        FAIL() << "add_class failed\n";
    }
    css.cascade(doc, 400.f, 200.f);
    layout.mark_needs_layout(paragraph);
    text = text_child(layout.principal_box(paragraph));
    if (text == nullptr || text->style_ptr == nullptr) {
        FAIL() << "text box disappeared\n";
    }
    if (std::fabs(text->style().font_size - 32.f) > 0.5f) {
        FAIL() << "text run kept font " << text->style().font_size << "\n";
    }
    layout.layout(doc, zui::layout::Viewport{400.f, 200.f});
    const float grown = advance_span(layout.principal_box(paragraph));
    if (!(grown > small_advances * 1.6f)) {
        FAIL() << "advances " << grown << " did not follow 16px -> 32px (was " << small_advances
               << ")\n";
    }
}

TEST(LayoutText, OutlineCapture) {
    zui::text::FontChain chain;
    if (!chain.init("sans-serif")) {
        FAIL() << "no font face opened\n";
    }
    zui::text::OutlineGlyph latin;
    if (!chain.capture_outline(static_cast<uint32_t>('A'), 16.0f, 0, latin) || !latin.ok) {
        FAIL() << "latin outline missing\n";
    }
    if (latin.curves.empty() || !(latin.advance > 0.0f)) {
        FAIL() << "latin outline has no ink\n";
    }
    if (!(latin.max_x > latin.min_x) || !(latin.max_y > latin.min_y)) {
        FAIL() << "latin outline box is empty\n";
    }
    for (const zui::text::OutlineCurve& curve : latin.curves) {
        if (curve.kind < 1 || curve.kind > 3) {
            FAIL() << "curve kind " << curve.kind << "\n";
        }
    }
    zui::text::OutlineGlyph cjk;
    if (!chain.capture_outline(0x4E2D, 19.0f, 0, cjk) || !cjk.ok || cjk.curves.empty()) {
        FAIL() << "cjk outline missing\n";
    }
    if (!(cjk.max_x > cjk.min_x) || !(cjk.max_y > cjk.min_y)) {
        FAIL() << "cjk outline box is empty\n";
    }
}

TEST(LayoutText, OutlineWindingAtVertices) {
    zui::text::FontChain chain;
    if (!chain.init("sans-serif")) {
        FAIL() << "no font face opened\n";
    }
    const std::uint32_t cps[] = {static_cast<std::uint32_t>('A'), 0x4E2D, 0x4ED6, 0x53EF, 0x4EBA};
    for (std::uint32_t cp : cps) {
        zui::text::OutlineGlyph glyph;
        if (!chain.capture_outline(cp, 19.0f, 0, glyph) || !glyph.ok || glyph.curves.empty()) {
            FAIL() << "outline missing for U+" << std::hex << cp << "\n";
        }
        if (!winding_matches_lines(glyph)) {
            FAIL() << "winding disagrees with the flattened outline at a vertex y, U+" << std::hex
                   << cp << "\n";
        }
    }
}

TEST(LayoutText, GlyphMatrixTwoSizes) {
    zui::backends::GlyphOutlines outlines;
    zui::paint::DisplayItem item;
    item.op = zui::paint::DisplayOp::DrawText;
    item.text = "A";
    item.font_family = "sans-serif";
    item.font_px = 16.f;
    item.rect = {10.f, 20.f, 0.f, 0.f};
    item.color = {0, 0, 0, 255};
    std::vector<zui::backends::GlyphVert> small;
    zui::backends::append_glyph_quads(outlines, item, small);
    if (small.empty()) {
        FAIL() << "glyph append produced no vertices\n";
    }
    const std::vector<zui::backends::GpuGlyphCurve> first = outlines.curves();
    if (first.empty()) {
        FAIL() << "curve shelf is empty\n";
    }
    item.font_px = 32.f;
    std::vector<zui::backends::GlyphVert> large;
    zui::backends::append_glyph_quads(outlines, item, large);
    const std::vector<zui::backends::GpuGlyphCurve>& second = outlines.curves();
    if (second.size() != first.size() ||
        std::memcmp(first.data(), second.data(), first.size() * sizeof(first[0])) != 0) {
        FAIL() << "curve payload changed at the second font size\n";
    }
    const float scale_small =
        std::hypot(small[0].m00, small[0].m10);
    const float scale_large =
        std::hypot(large[0].m00, large[0].m10);
    if (!(scale_small > 0.f) || std::fabs(scale_large / scale_small - 2.f) > 1e-4f) {
        FAIL() << "scale ratio " << scale_large / scale_small << "\n";
    }
    const auto screen_of = [](const zui::backends::GlyphVert& v) {
        return std::pair<float, float>{v.m00 * v.em_x + v.m01 * v.em_y + v.m02,
                                       v.m10 * v.em_x + v.m11 * v.em_y + v.m12};
    };
    const auto got = screen_of(small[0]);
    const float want_x = 10.f + small[0].em_x * 16.f;
    const float want_y = 20.f - small[0].em_y * 16.f;
    if (std::fabs(got.first - want_x) > 1e-3f || std::fabs(got.second - want_y) > 1e-3f) {
        FAIL() << "upright screen " << got.first << "," << got.second << " want " << want_x << ","
               << want_y << "\n";
    }
    const std::size_t curves_before_side = outlines.curves().size();
    item.font_px = 16.f;
    item.text_orient = zui::paint::TextGlyphOrient::SidewaysRl;
    item.rect = {3.f, 4.f, 0.f, 0.f};
    std::vector<zui::backends::GlyphVert> side;
    zui::backends::append_glyph_quads(outlines, item, side);
    if (side.empty() || outlines.curves().size() != curves_before_side) {
        FAIL() << "sideways append changed the curve shelf\n";
    }
    const auto side_got = screen_of(side[0]);
    const float side_x = 3.f + side[0].em_y * 16.f;
    const float side_y = 4.f + side[0].em_x * 16.f;
    if (std::fabs(side_got.first - side_x) > 1e-3f || std::fabs(side_got.second - side_y) > 1e-3f) {
        FAIL() << "sideways screen " << side_got.first << "," << side_got.second << "\n";
    }
    item.text_orient = zui::paint::TextGlyphOrient::Upright;
    item.rect = {10.f, 20.f, 0.f, 0.f};
    item.has_xform = true;
    item.xform_a = 2.f;
    item.xform_e = 2.f;
    item.xform_c = 5.f;
    item.xform_f = 7.f;
    std::vector<zui::backends::GlyphVert> aff;
    zui::backends::append_glyph_quads(outlines, item, aff);
    if (aff.empty() || outlines.curves().size() != curves_before_side) {
        FAIL() << "affine append changed the curve shelf\n";
    }
    const float lx = 10.f + aff[0].em_x * 16.f;
    const float ly = 20.f - aff[0].em_y * 16.f;
    const auto aff_got = screen_of(aff[0]);
    if (std::fabs(aff_got.first - (2.f * lx + 5.f)) > 1e-3f ||
        std::fabs(aff_got.second - (2.f * ly + 7.f)) > 1e-3f) {
        FAIL() << "affine screen " << aff_got.first << "," << aff_got.second << "\n";
    }
    std::cout << "curves " << first.size() << " scale " << scale_small << " " << scale_large
              << " ratio " << scale_large / scale_small << " upright " << got.first << " "
              << got.second << " sideways " << side_got.first << " " << side_got.second
              << " affine " << aff_got.first << " " << aff_got.second << "\n";
}

TEST(LayoutText, EffectPrograms) {
    zui::paint::DisplayItem fill;
    fill.op = zui::paint::DisplayOp::FillRect;
    zui::paint::DisplayItem image;
    image.op = zui::paint::DisplayOp::DrawImage;
    zui::paint::DisplayItem shadow;
    shadow.op = zui::paint::DisplayOp::ShadowRect;
    shadow.blur = 4.f;
    zui::paint::DisplayItem glyph;
    glyph.op = zui::paint::DisplayOp::DrawText;
    glyph.blur = 0.f;
    zui::paint::DisplayItem soft;
    soft.op = zui::paint::DisplayOp::DrawText;
    soft.blur = 3.f;
    const zui::backends::EffectChoice choices[] = {
        zui::backends::effect_for(fill), zui::backends::effect_for(image),
        zui::backends::effect_for(shadow), zui::backends::effect_for(glyph),
        zui::backends::effect_for(soft)};
    const int ids[] = {static_cast<int>(choices[0].program), static_cast<int>(choices[1].program),
                       static_cast<int>(choices[2].program), static_cast<int>(choices[3].program)};
    for (int i = 0; i < 4; ++i) {
        for (int j = i + 1; j < 4; ++j) {
            if (ids[i] == ids[j]) {
                FAIL() << "programs " << i << " and " << j << " are both " << ids[i] << "\n";
            }
        }
    }
    if (choices[0].program != zui::backends::GpuProgram::Fill ||
        choices[1].program != zui::backends::GpuProgram::Image ||
        choices[2].program != zui::backends::GpuProgram::Shadow || !(choices[2].blur > 0.f) ||
        choices[3].program != zui::backends::GpuProgram::Glyph ||
        choices[4].program != zui::backends::GpuProgram::Shadow || !(choices[4].blur > 0.f)) {
        FAIL() << "effect selection mismatch\n";
    }
    const auto desktop = [](int mode) { return zui::backends::program_for_batch(mode, false); };
    const auto es = [](int mode) { return zui::backends::program_for_batch(mode, true); };
    if (desktop(zui::backends::kBatchFill) != zui::backends::GpuProgram::Fill ||
        desktop(zui::backends::kBatchFillShape) != zui::backends::GpuProgram::Fill ||
        desktop(zui::backends::kBatchImage) != zui::backends::GpuProgram::Image ||
        desktop(zui::backends::kBatchGlyph) != zui::backends::GpuProgram::Glyph ||
        desktop(zui::backends::kBatchShadow) != zui::backends::GpuProgram::Shadow ||
        desktop(zui::backends::kBatchShadowGauss) != zui::backends::GpuProgram::Blur ||
        desktop(zui::backends::kBatchShadowBlit) != zui::backends::GpuProgram::Layer ||
        desktop(zui::backends::kBatchLayer) != zui::backends::GpuProgram::Layer ||
        desktop(zui::backends::kBatchShadowGlyph) != zui::backends::GpuProgram::Glyph ||
        zui::backends::batch_for(fill) != zui::backends::kBatchFill ||
        zui::backends::batch_for(image) != zui::backends::kBatchImage ||
        zui::backends::batch_for(shadow) != zui::backends::kBatchShadow ||
        zui::backends::batch_for(glyph) != zui::backends::kBatchGlyph ||
        zui::backends::batch_for(soft) != zui::backends::kBatchShadowGlyph) {
        FAIL() << "submit batch table mismatch\n";
    }
    if (es(zui::backends::kBatchFill) != zui::backends::GpuProgram::Fill ||
        es(zui::backends::kBatchImage) != zui::backends::GpuProgram::Image ||
        es(zui::backends::kBatchShadow) != zui::backends::GpuProgram::Shadow ||
        es(zui::backends::kBatchShadowGauss) != zui::backends::GpuProgram::Blur ||
        es(zui::backends::kBatchLayer) != zui::backends::GpuProgram::Layer ||
        es(zui::backends::kBatchFill) == zui::backends::GpuProgram::Glyph ||
        es(zui::backends::kBatchImage) == zui::backends::GpuProgram::Glyph ||
        es(zui::backends::kBatchShadow) == zui::backends::GpuProgram::Glyph ||
        es(zui::backends::kBatchGlyph) == zui::backends::GpuProgram::Glyph ||
        es(zui::backends::kBatchShadowGlyph) == zui::backends::GpuProgram::Glyph) {
        FAIL() << "ES program selection returned a glyph program\n";
    }
    const int blur_id = static_cast<int>(desktop(zui::backends::kBatchShadowGauss));
    const int layer_id = static_cast<int>(desktop(zui::backends::kBatchLayer));
    std::cout << "programs " << ids[0] << " " << ids[1] << " " << ids[2] << " " << ids[3]
              << " blur " << blur_id << " layer " << layer_id << " shadow-blur " << choices[2].blur
              << " text-blur " << choices[4].blur << " es-fill "
              << static_cast<int>(es(zui::backends::kBatchFill)) << " es-image "
              << static_cast<int>(es(zui::backends::kBatchImage)) << " es-shadow "
              << static_cast<int>(es(zui::backends::kBatchShadow)) << " es-glyph "
              << static_cast<int>(es(zui::backends::kBatchGlyph)) << "\n";
}

namespace {

std::string read_text(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string gl_body(const std::string& src, const char* name) {
    const std::string marker = std::string("// zui-shader-body: ") + name;
    std::size_t at = 0;
    for (;;) {
        at = src.find(marker, at);
        if (at == std::string::npos) {
            return {};
        }
        const std::size_t end = at + marker.size();
        if (end >= src.size() || src[end] == '\n' || src[end] == '\r') {
            const auto next = src.find("// zui-shader-body:", end);
            return src.substr(at, next == std::string::npos ? std::string::npos : next - at);
        }
        at = end;
    }
}

bool has_mode_switch(const std::string& body) {
    return body.find("uMode==") != std::string::npos || body.find("uMode ==") != std::string::npos ||
           body.find("pc.mode ==") != std::string::npos || body.find("pc.mode==") != std::string::npos;
}

}  // namespace

TEST(LayoutText, ShaderBodies) {
    const std::filesystem::path root = ZUI_SOURCE_DIR;
    const auto shaders = root / "source" / "backends" / "shaders";
    const std::string fill = read_text(shaders / "ui.frag");
    const std::string image = read_text(shaders / "image.frag");
    const std::string shadow = read_text(shaders / "shadow.frag");
    const std::string blur = read_text(shaders / "blur.frag");
    const std::string layer = read_text(shaders / "layer.frag");
    const std::string glyph = read_text(shaders / "glyph.frag");
    const std::string cover = read_text(shaders / "glyph_cover.glsl");
    const std::string glyph_vs = read_text(shaders / "glyph.vert");
    const std::string ui_vs = read_text(shaders / "ui.vert");
    const std::string screen_vs = read_text(shaders / "screen.vert");
    const std::string gl = read_text(root / "source" / "backends" / "opengl" / "gl_programs.cpp");
    const std::string pipes = read_text(root / "source" / "backends" / "vulkan" / "pipelines.cpp");
    if (fill.empty() || image.empty() || shadow.empty() || blur.empty() || layer.empty() ||
        glyph.empty() || cover.empty() || glyph_vs.empty() || ui_vs.empty() || screen_vs.empty() ||
        gl.empty() || pipes.empty()) {
        FAIL() << "shader source missing\n";
    }
    const std::string gl_fill = gl_body(gl, "fill");
    const std::string gl_image = gl_body(gl, "image");
    const std::string gl_shadow = gl_body(gl, "shadow");
    const std::string gl_blur = gl_body(gl, "blur");
    const std::string gl_layer = gl_body(gl, "layer");
    const std::string gl_glyph = gl_body(gl, "glyph");
    const std::string gl_glyph_vs = gl_body(gl, "glyph-vert");
    const std::string gl_ui_vs = gl_body(gl, "ui-vert");
    if (gl_fill.empty() || gl_image.empty() || gl_shadow.empty() || gl_blur.empty() ||
        gl_layer.empty() || gl_glyph.empty() || gl_glyph_vs.empty() || gl_ui_vs.empty()) {
        FAIL() << "GL shader body marker missing\n";
    }
    auto forbid = [](const std::string& body, const char* what, const char* token) {
        if (body.find(token) != std::string::npos) {
            ADD_FAILURE() << what << " contains " << token << "\n";
        }
    };
    auto need = [](const std::string& body, const char* what, const char* token) {
        if (body.find(token) == std::string::npos) {
            ADD_FAILURE() << what << " missing " << token << "\n";
        }
    };
    need(shadow, "vulkan shadow", "shadow_cover");
    need(gl_shadow, "gl shadow", "shadow_cover");
    forbid(shadow, "vulkan shadow", "texture(");
    forbid(shadow, "vulkan shadow", "texelFetch");
    forbid(shadow, "vulkan shadow", "mono_quad");
    forbid(gl_shadow, "gl shadow", "texture(");
    forbid(gl_shadow, "gl shadow", "texelFetch");
    forbid(gl_shadow, "gl shadow", "mono_quad");
    if (has_mode_switch(shadow) || has_mode_switch(gl_shadow)) {
        FAIL() << "shadow body selects an effect with a mode uniform\n";
    }
    need(blur, "vulkan blur", "exp(-0.5");
    need(gl_blur, "gl blur", "exp(-0.5");
    forbid(blur, "vulkan blur", "shadow_cover");
    forbid(blur, "vulkan blur", "mono_quad");
    forbid(gl_blur, "gl blur", "shadow_cover");
    forbid(gl_blur, "gl blur", "mono_quad");
    if (has_mode_switch(blur) || has_mode_switch(gl_blur)) {
        FAIL() << "blur body selects an effect with a mode uniform\n";
    }
    need(layer, "vulkan layer", "texelFetch");
    need(gl_layer, "gl layer", "texelFetch");
    forbid(layer, "vulkan layer", "shadow_cover");
    forbid(layer, "vulkan layer", "mono_quad");
    forbid(gl_layer, "gl layer", "shadow_cover");
    forbid(gl_layer, "gl layer", "mono_quad");
    if (has_mode_switch(layer) || has_mode_switch(gl_layer)) {
        FAIL() << "layer body selects an effect with a mode uniform\n";
    }
    need(image, "vulkan image", "texture(");
    need(gl_image, "gl image", "texture(");
    forbid(image, "vulkan image", "shadow_cover");
    forbid(image, "vulkan image", "mono_quad");
    forbid(gl_image, "gl image", "shadow_cover");
    forbid(gl_image, "gl image", "mono_quad");
    if (has_mode_switch(image) || has_mode_switch(gl_image)) {
        FAIL() << "image body selects an effect with a mode uniform\n";
    }
    need(fill, "vulkan fill", "pc.mode == 2");
    need(gl_fill, "gl fill", "uMode==2");
    forbid(fill, "vulkan fill", "shadow_cover");
    forbid(fill, "vulkan fill", "mono_quad");
    forbid(fill, "vulkan fill", "texture(");
    forbid(gl_fill, "gl fill", "shadow_cover");
    forbid(gl_fill, "gl fill", "mono_quad");
    forbid(gl_fill, "gl fill", "uMode==9");
    forbid(gl_fill, "gl fill", "uMode==5");
    forbid(gl_fill, "gl fill", "uMode==7");
    forbid(gl_fill, "gl fill", "uMode==3");
    if (fill.find("pc.mode == 5") != std::string::npos ||
        fill.find("pc.mode == 7") != std::string::npos ||
        fill.find("pc.mode == 9") != std::string::npos ||
        fill.find("pc.mode == 3") != std::string::npos) {
        FAIL() << "fill body selects another effect with a mode uniform\n";
    }
    need(glyph, "vulkan glyph", "#include \"glyph_cover.glsl\"");
    need(cover, "glyph cover", "mono_quad");
    need(cover, "glyph cover", "float inside(");
    need(gl_glyph, "gl glyph", "kGlyphCoverGlsl");
    forbid(glyph, "vulkan glyph", "shadow_cover");
    forbid(gl_glyph, "gl glyph", "shadow_cover");
    if (has_mode_switch(glyph) || has_mode_switch(gl_glyph)) {
        FAIL() << "glyph body selects an effect with a mode uniform\n";
    }
    need(glyph_vs, "vulkan glyph vert", "aM0");
    need(glyph_vs, "vulkan glyph vert", "aEm");
    need(gl_glyph_vs, "gl glyph vert", "aM0");
    need(gl_glyph_vs, "gl glyph vert", "aEm");
    forbid(glyph_vs, "vulkan glyph vert", "font_px");
    forbid(gl_glyph_vs, "gl glyph vert", "font_px");
    need(screen_vs, "screen vert", "gl_VertexIndex");
    forbid(ui_vs, "ui vert", "aM0");
    need(pipes, "pipelines", "kUiFragSpv");
    need(pipes, "pipelines", "kImageFragSpv");
    need(pipes, "pipelines", "kShadowFragSpv");
    need(pipes, "pipelines", "kBlurFragSpv");
    need(pipes, "pipelines", "kLayerFragSpv");
    need(pipes, "pipelines", "kGlyphFragSpv");
    need(pipes, "pipelines", "kGlyphVertSpv");
    if (pipes.find("kBlurFragSpv, sizeof(kBlurFragSpv)") == std::string::npos ||
        pipes.find("kLayerFragSpv, sizeof(kLayerFragSpv)") == std::string::npos) {
        FAIL() << "blur or layer pipeline does not compile its own fragment shader\n";
    }
    std::cout << "bodies fill " << fill.size() << " image " << image.size() << " shadow "
              << shadow.size() << " blur " << blur.size() << " layer " << layer.size() << " glyph "
              << glyph.size() << " cover " << cover.size() << " gl-fill " << gl_fill.size()
              << " gl-shadow " << gl_shadow.size() << " gl-blur " << gl_blur.size() << " gl-layer "
              << gl_layer.size() << "\n";
}
