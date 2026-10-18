#include "paint/painter_internal.h"

#include "paint/display_list.h"

#include "perf/perf_budget.h"
#include "css/transition/transition.h"
#include "image/image.h"
#include "layout/clip_path.h"
#include "layout/overflow_clip.h"
#include "layout/paint_map.h"
#include "layout/paint_transform.h"
#include "layout/scrollbar_h.h"
#include "layout/scrollbar_v.h"
#include "layout/stacking.h"

#include "text/font_chain.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zui {
namespace paint {
namespace painter_detail {
namespace {

struct InkSpan {
    float x0 = 0.f;
    float x1 = 0.f;
};

/// A filled outline crosses scanline `y` (em, y up). Horizontal edges do not change winding.
void add_crossing(std::vector<std::pair<float, int>>& hits, float x0, float y0, float x1, float y1,
                  float y) {
    const bool rising = y0 <= y && y < y1;
    const bool falling = y1 <= y && y < y0;
    if (!rising && !falling) {
        return;
    }
    const float dy = y1 - y0;
    if (dy == 0.f) {
        return;
    }
    const float t = (y - y0) / dy;
    hits.emplace_back(x0 + t * (x1 - x0), rising ? 1 : -1);
}

void flatten_cross(const text::OutlineCurve& curve, float y,
                   std::vector<std::pair<float, int>>& hits) {
    if (curve.kind == 1) {
        add_crossing(hits, curve.x0, curve.y0, curve.x1, curve.y1, y);
        return;
    }
    constexpr int kSteps = 8;
    float px = curve.x0;
    float py = curve.y0;
    for (int i = 1; i <= kSteps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSteps);
        const float u = 1.f - t;
        float x = 0.f;
        float yy = 0.f;
        if (curve.kind == 2) {
            x = u * u * curve.x0 + 2.f * u * t * curve.x1 + t * t * curve.x2;
            yy = u * u * curve.y0 + 2.f * u * t * curve.y1 + t * t * curve.y2;
        } else {
            x = u * u * u * curve.x0 + 3.f * u * u * t * curve.x1 + 3.f * u * t * t * curve.x2 +
                t * t * t * curve.x3;
            yy = u * u * u * curve.y0 + 3.f * u * u * t * curve.y1 + 3.f * u * t * t * curve.y2 +
                 t * t * t * curve.y3;
        }
        add_crossing(hits, px, py, x, yy, y);
        px = x;
        py = yy;
    }
}

void inside_at(const std::vector<text::OutlineCurve>& curves, float y, std::vector<InkSpan>& out) {
    std::vector<std::pair<float, int>> hits;
    hits.reserve(curves.size());
    for (const text::OutlineCurve& curve : curves) {
        flatten_cross(curve, y, hits);
    }
    if (hits.size() < 2) {
        return;
    }
    std::sort(hits.begin(), hits.end(),
              [](const std::pair<float, int>& a, const std::pair<float, int>& b) {
                  return a.first < b.first;
              });
    int wind = 0;
    float open_x = 0.f;
    bool open = false;
    for (const std::pair<float, int>& hit : hits) {
        const int prev = wind;
        wind += hit.second;
        const bool was = prev != 0;
        const bool now = wind != 0;
        if (!was && now) {
            open_x = hit.first;
            open = true;
        } else if (was && !now && open) {
            if (hit.first > open_x) {
                out.push_back(InkSpan{open_x, hit.first});
            }
            open = false;
        }
    }
}

void merge_spans(std::vector<InkSpan>& spans) {
    if (spans.size() < 2) {
        return;
    }
    std::sort(spans.begin(), spans.end(),
              [](const InkSpan& a, const InkSpan& b) { return a.x0 < b.x0; });
    std::vector<InkSpan> merged;
    merged.push_back(spans.front());
    for (std::size_t i = 1; i < spans.size(); ++i) {
        if (spans[i].x0 <= merged.back().x1) {
            merged.back().x1 = std::max(merged.back().x1, spans[i].x1);
        } else {
            merged.push_back(spans[i]);
        }
    }
    spans.swap(merged);
}

/// Device-x holes where the run's ink crosses the decoration band. Called only for a line that
/// `text-decoration-skip-ink` skips. The outline is em units, y up, origin on the glyph pen.
/// `include_cjk` is false for `auto` (Chromium skips descenders but not CJK) and true for `all`.
void ink_skip_spans(const layout::TextRun& run, const layout::Box& font_box, float text_x,
                    float text_y, float run_w, float font_px, float y, float thickness,
                    bool include_cjk, std::vector<InkSpan>& device) {
    if (run.text.empty() || font_px <= 0.f || thickness <= 0.f || font_box.style_ptr == nullptr) {
        return;
    }
    text::FontChain chain;
    if (!chain.init(font_box.style().font_family, font_box.style().font_weight,
                    font_box.style().font_italic)) {
        return;
    }
    // Chromium dilates each intercept by min(thickness, 13px). Past that the gap stays put,
    // so a thick stroke does not swallow the letters beside the descender.
    const float gap = std::min(thickness, 13.f);
    // Inset the stripe by 0.5px so a contour that only grazes the edge is not a hit.
    const float inset = thickness > 1.f ? 0.5f : 0.f;
    const float inner = std::max(thickness - inset * 2.f, 1.f);
    const int samples = std::max(1, static_cast<int>(std::ceil(inner)));
    const float limit = text_x + run_w;
    const float band_a = (text_y - (y + inset)) / font_px;
    const float band_b = (text_y - (y + thickness - inset)) / font_px;
    const float band_lo = std::min(band_a, band_b);
    const float band_hi = std::max(band_a, band_b);
    float pen = text_x;
    std::size_t byte = 0;
    std::size_t glyph = 0;
    while (byte < run.text.size() && pen < limit - 0.01f) {
        const char32_t cp = static_cast<char32_t>(text::next_cp(run.text, byte));
        float adv = font_px * 0.5f;
        if (glyph < run.glyph_advances.size()) {
            adv = run.glyph_advances[glyph];
        } else {
            const text::LoadedGlyph measured =
                chain.load(static_cast<std::uint32_t>(cp), font_px, false);
            if (measured.advance > 0.f) {
                adv = measured.advance;
            }
        }
        std::uint32_t gid = 0;
        bool skip_paint = false;
        if (glyph < run.glyph_ids.size()) {
            gid = run.glyph_ids[glyph];
            skip_paint = gid == text::kSkipPaintGlyph;
        }
        ++glyph;
        const bool skip_cjk = !include_cjk && text::is_cjk(static_cast<std::uint32_t>(cp));
        if (!skip_paint && adv > 0.f && cp != 0 && !skip_cjk) {
            text::OutlineGlyph outline;
            if (chain.capture_outline(static_cast<std::uint32_t>(cp), font_px, gid, outline) &&
                !outline.curves.empty() && outline.max_y >= band_lo && outline.min_y <= band_hi) {
                std::vector<InkSpan> local;
                for (int s = 0; s < samples; ++s) {
                    const float device_y =
                        y + inset + inner * (static_cast<float>(s) + 0.5f) / static_cast<float>(samples);
                    inside_at(outline.curves, (text_y - device_y) / font_px, local);
                }
                for (const InkSpan& span : local) {
                    const float x0 = pen + span.x0 * font_px - gap;
                    const float x1 = pen + span.x1 * font_px + gap;
                    if (x1 > text_x && x0 < limit) {
                        device.push_back(InkSpan{std::max(text_x, x0), std::min(limit, x1)});
                    }
                }
            }
        }
        pen += adv;
    }
    merge_spans(device);
}

}  // namespace

void emit_text_decoration(DisplayList& out, float text_x, float text_y, float run_w, float ascent,
                          float font_px, float descent, const TextDecoration& dec, float opacity,
                          const layout::TextRun* run, const layout::Box* font_box) {
    if (run_w <= 0.f) {
        return;
    }
    const Color c = apply_opacity(dec.color, opacity);
    float thickness = dec.thickness;
    if (thickness <= 0.f) {
        thickness = std::max(1.f, std::floor(font_px / 16.f + 0.5f));
    }
    auto paint_line = [&](float y, bool through) {
        const bool skip = dec.skip_ink == SkipInk::All || (dec.skip_ink == SkipInk::Auto && !through);
        std::vector<InkSpan> holes;
        if (skip && run != nullptr && font_box != nullptr) {
            ink_skip_spans(*run, *font_box, text_x, text_y, run_w, font_px, y, thickness,
                           dec.skip_ink == SkipInk::All, holes);
        }
        const float end = text_x + run_w;
        if (holes.empty()) {
            stroke_decor_segment(out, text_x, end, y, thickness, c, dec.style);
            return;
        }
        float cursor = text_x;
        for (const InkSpan& hole : holes) {
            if (hole.x0 > cursor) {
                stroke_decor_segment(out, cursor, hole.x0, y, thickness, c, dec.style);
            }
            cursor = std::max(cursor, hole.x1);
        }
        if (cursor < end) {
            stroke_decor_segment(out, cursor, end, y, thickness, c, dec.style);
        }
    };
    if (dec.underline) {
        // The alphabetic underline's top is the baseline plus text-underline-offset.
        // A thickness-dependent gap sits the bar below Edge (a 4px stroke was 2px low).
        // `under` uses the line box's under edge (the content box bottom on a single
        // line), which is `descent` here: line-block minus the baseline offset.
        float y = text_y + dec.underline_offset;
        if (dec.underline_under) {
            const float drop = descent > 0.f ? descent : font_px * 0.2f;
            y += drop;
        }
        paint_line(y, false);
    }
    if (dec.overline) {
        paint_line(text_y - ascent, false);
    }
    if (dec.line_through) {
        paint_line(text_y - ascent * 0.4f, true);
    }
}

}  // namespace painter_detail
}  // namespace paint
}  // namespace zui
