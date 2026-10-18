#include "layout/cache/layout_context.h"

#include "perf/perf_budget.h"
#include "image/image.h"
#include "layout/cache/box_cache.h"
#include "layout/cache/flex_algo.h"
#include "layout/cache/flow_algo.h"
#include "layout/cache/grid_algo.h"
#include "layout/cache/layout_invalidation.h"
#include "layout/cache/multicol_algo.h"
#include "layout/cache/out_of_flow.h"
#include "layout/cache/quantum.h"
#include "layout/cache/table_algo.h"
#include "layout/cache/used_length.h"
#include "layout/freetype_text_metrics.h"
#include "layout/inline/shape.h"
#include "layout/style_table.h"
#include "log/log.h"
#include "text/font_chain.h"
#include "trace/trace.h"

#include <string_view>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <unordered_set>

namespace zui {
namespace layout {

LayoutContext::FontEm LayoutContext::font_em(const LayoutStyle& style) const {
    FontEm out;
    out.ascent = style.font_size * 0.8f;
    out.descent = style.font_size * 0.2f;
    out.x_height = style.font_size * 0.5f;
    if (metrics_ == nullptr || !metrics_->ready() || style.font_size <= 0.f) {
        return out;
    }
    text::FontChain& chain = metrics_->chain_for(style.font_family);
    chain.use_px(style.font_size);
    // Chromium quantizes the face's ascent and descent to whole pixels at the used size
    // and `line-height: normal` is their sum, so keep the same integers: line boxes and
    // baselines then land on the same pixels instead of drifting half a pixel per line.
    if (chain.ascent() > 0.f) {
        out.ascent = std::round(chain.ascent());
    }
    if (chain.descent() > 0.f) {
        out.descent = std::round(chain.descent());
    }
    if (chain.x_height() > 0.f) {
        out.x_height = chain.x_height();
    }
    return out;
}

float LayoutContext::text_font_box(const LayoutStyle& style, std::string_view text) const {
    if (metrics_ == nullptr || !metrics_->ready() || style.font_size <= 0.f) {
        return 0.f;
    }
    text::FontChain& chain =
        metrics_->chain_for(style.font_family, style.font_weight, style.font_italic);
    if (text.empty()) {
        chain.load(static_cast<std::uint32_t>(' '), style.font_size, false);
        if (chain.ascent() <= 0.f && chain.descent() <= 0.f) {
            return 0.f;
        }
        return std::round(chain.ascent()) + std::round(chain.descent());
    }
    float box = 0.f;
    std::size_t index = 0;
    while (index < text.size()) {
        const std::uint32_t cp = text::next_cp(text, index);
        if (cp == 0) {
            break;
        }
        box = std::max(box, chain.glyph_font_box(cp, style.font_size));
    }
    return box;
}

float LayoutContext::text_field_preferred_content(const LayoutStyle& style, int size) const {
    if (size <= 0) {
        size = 20;
    }
    if (metrics_ == nullptr || !metrics_->ready() || style.font_size <= 0.f) {
        // Fallback when fonts are not loaded yet: 0.5em per character matches a typical Latin
        // average and keeps shrink-to-fit from collapsing to zero.
        return static_cast<float>(size) * style.font_size * 0.5f;
    }
    text::FontChain& chain =
        metrics_->chain_for(style.font_family, style.font_weight, style.font_italic);
    chain.load(static_cast<std::uint32_t>('0'), style.font_size, false);
    float avg = chain.avg_char_width();
    float max_w = chain.max_char_width();
    // Blink `HasValidAvgCharWidth`: when OS/2 avg looks like a CJK full-width metric (much
    // wider than '0'), fall back to the advance of '0' and skip the IE max-width bump.
    const float zero = chain.load(static_cast<std::uint32_t>('0'), style.font_size, false).advance;
    if (avg <= 0.f || (zero > 0.f && avg > zero * 1.7f)) {
        avg = zero > 0.f ? zero : style.font_size * 0.5f;
        max_w = 0.f;
    } else {
        // Older Blink path that Edge still matches for Noto Sans: round avg and max to whole
        // pixels before the size multiply (modern `max(avg, round(avg))` is wider by ~7px).
        avg = std::round(avg);
        if (max_w > 0.f) {
            max_w = std::round(max_w);
        }
    }
    float result = avg * static_cast<float>(size);
    if (max_w > 0.f) {
        result += max_w - avg;
    }
    return std::ceil(result);
}

std::vector<ShapedRun> LayoutContext::shape(std::string_view text, const LayoutStyle& style,
                                            Direction direction) {
    // Section 9 step E: a text box that is asked more than once in a frame used to pay for its
    // whole string again - decode, bidi, and a per-glyph string. The key is exactly what the
    // shaper reads (see `ShapeMemoKey`), so a hit is only possible where the shaper would return
    // the same runs.
    ShapeMemoKey key;
    key.text.assign(text);
    key.font_family = style.font_family;
    key.direction = direction;
    key.white_space = style.white_space;
    key.word_break = style.word_break;
    key.overflow_wrap = style.overflow_wrap;
    key.text_transform = style.text_transform;
    key.font_size = style.font_size;
    key.letter_spacing = style.letter_spacing;
    key.font_weight = style.font_weight;
    key.font_italic = style.font_italic;
    key.font_kerning_none = style.font_kerning_none;
    const auto found = shape_memo_.find(key);
    if (found != shape_memo_.end()) {
        ++stats_.shape_memo_hits;
        return found->second;
    }
    ++stats_.shape_calls;
    stats_.shape_bytes += text.size();
    perf::note_text_shape();
    std::vector<ShapedRun> shaped;
    {
        // Q2 of `布局成本结构性修复方案.md`: how much of a text-heavy frame is the shaper itself,
        // as opposed to the line and fragment bookkeeping around it. Only the uncached path is
        // zoned, so the slice count is `shapes=` minus the memo hits.
        const auto started = perf_ ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        zui::trace::Zone zone("shape", zui::trace::Category::Text);
        shaped = inline_text::shape(text, style, direction, metrics_);
        if (perf_) {
            stats_.shape_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        }
    }
    shape_memo_.emplace(std::move(key), shaped);
    return shaped;
}

ReplacedMeasurement LayoutContext::measure_replaced(const Box& box, const ReplacedRequest&) {
    ReplacedMeasurement measured;
    if (box.replaced_src.empty()) {
        measured.resource_ready = true;
        measured.failed = true;
        return measured;
    }
    const std::uint64_t decodes_before = image::decode_count();
    int intrinsic_w = 0;
    int intrinsic_h = 0;
    {
        // SVG viewport only. A full raster here decoded every album thumbnail at its
        // document size (1200×800) before paint knew the tile was ~200px.
        zui::trace::Zone zone("image.load", zui::trace::Category::Image);
        if (!image::intrinsic_size(box.replaced_src, intrinsic_w, intrinsic_h)) {
            intrinsic_w = 0;
            intrinsic_h = 0;
        }
    }
    if (image::decode_count() != decodes_before) {
        perf::note_image_decode();
    }
    if (intrinsic_w <= 0 || intrinsic_h <= 0) {
        // Decode is synchronous. A missing file will not become ready on a later frame, so
        // leaving `resource_ready` false made every broken image keep the 300x150 placeholder.
        ZUI_WARN("[image] replaced source not decodable: {}", box.replaced_src);
        measured.resource_ready = true;
        measured.failed = true;
        return measured;
    }
    if (!box.replaced_seen) {
        // LAYOUT.md section 4: the first observation still measures 300x150 and only bumps the
        // content version; the intrinsic size lands on a later frame (a driver settles frames,
        // and a browser reaches that state before it stops). Reporting the intrinsic size here
        // made `resource_ready` true on the first frame and skipped that protocol entirely.
        Box* self = const_cast<Box*>(&box);
        if (std::find(pending_decode_.begin(), pending_decode_.end(), self) ==
            pending_decode_.end()) {
            pending_decode_.push_back(self);
        }
        return measured;
    }
    measured.resource_ready = true;
    measured.has_intrinsic_width = true;
    measured.has_intrinsic_height = true;
    measured.has_intrinsic_ratio = true;
    measured.intrinsic_width = static_cast<float>(intrinsic_w);
    measured.intrinsic_height = static_cast<float>(intrinsic_h);
    measured.intrinsic_ratio =
        measured.intrinsic_height == 0 ? 0 : measured.intrinsic_width / measured.intrinsic_height;
    return measured;
}


}  // namespace layout
}  // namespace zui
