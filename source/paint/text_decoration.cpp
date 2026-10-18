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
void emit_text_with_shadows(DisplayList& out, float text_x, float text_y, Color fg,
                            std::string_view text, float font_px, std::string_view family,
                            int font_weight, const html::Node* debug_owner, float letter_spacing,
                            bool font_italic, bool font_kerning_none, float opacity,
                            const std::vector<TextShadowLayer>& shadows, TextGlyphOrient orient,
                            bool has_xform, float xa, float xb, float xc, float xd, float xe,
                            float xf, const std::vector<float>* glyph_advances,
                            const std::vector<std::uint32_t>* glyph_ids) {
    // Front-to-back: first listed shadow ends on top, so paint last → first, then the fill.
    auto emit = [&](float x, float y, Color c, float blur) {
        if (has_xform) {
            out.draw_text_xform(x, y, c, text, font_px, family, xa, xb, xc, xd, xe, xf, font_weight,
                                debug_owner, letter_spacing, font_italic, font_kerning_none, blur,
                                orient, glyph_advances, glyph_ids);
        } else {
            out.draw_text(x, y, c, text, font_px, family, font_weight, debug_owner, letter_spacing,
                          font_italic, font_kerning_none, blur, orient, glyph_advances, glyph_ids);
        }
    };
    for (std::size_t i = shadows.size(); i > 0; --i) {
        const TextShadowLayer& s = shadows[i - 1];
        emit(text_x + s.ox, text_y + s.oy, apply_opacity(s.color, opacity), s.blur);
    }
    emit(text_x, text_y, fg, 0.f);
}

TextGlyphOrient glyph_orient_for(layout::WritingMode mode) {
    using layout::WritingMode;
    if (mode == WritingMode::VerticalRl || mode == WritingMode::SidewaysRl) {
        return TextGlyphOrient::SidewaysRl;
    }
    if (mode == WritingMode::VerticalLr || mode == WritingMode::SidewaysLr) {
        return TextGlyphOrient::SidewaysLr;
    }
    return TextGlyphOrient::Upright;
}

bool parse_text_decoration(const layout::Box& box, TextDecoration& out) {
    std::string line;
    std::string style;
    std::string color;
    std::string thickness;
    if (const std::string* v = computed(box, "text-decoration-line")) {
        line = *v;
    }
    if (const std::string* v = computed(box, "text-decoration-style")) {
        style = *v;
    }
    if (const std::string* v = computed(box, "text-decoration-color")) {
        color = *v;
    }
    if (const std::string* v = computed(box, "text-decoration-thickness")) {
        thickness = *v;
    }
    if (const std::string* v = computed(box, "text-decoration")) {
        // Shorthand may be the only stored form (lexbor serializes the combined value).
        if (line.empty() && style.empty() && color.empty() && thickness.empty()) {
            std::string cur;
            int depth = 0;
            std::vector<std::string> parts;
            for (char ch : *v) {
                if (ch == '(') {
                    ++depth;
                    cur.push_back(ch);
                    continue;
                }
                if (ch == ')') {
                    depth = std::max(0, depth - 1);
                    cur.push_back(ch);
                    continue;
                }
                if (depth == 0 && std::isspace(static_cast<unsigned char>(ch))) {
                    if (!cur.empty()) {
                        parts.push_back(cur);
                        cur.clear();
                    }
                    continue;
                }
                cur.push_back(ch);
            }
            if (!cur.empty()) {
                parts.push_back(cur);
            }
            for (const std::string& p : parts) {
                const std::string l = lower_ascii(p);
                if (l == "none") {
                    return false;
                }
                if (l == "underline" || l == "overline" || l == "line-through" || l == "blink") {
                    if (line.empty()) {
                        line = l;
                    } else {
                        line.push_back(' ');
                        line += l;
                    }
                    continue;
                }
                if (l == "solid" || l == "double" || l == "dotted" || l == "dashed" ||
                    l == "wavy") {
                    style = l;
                    continue;
                }
                bool bad = false;
                float px = 0.f;
                if (p == "0" || (parse_bg_length(p, 0.f, px, bad) && !bad)) {
                    thickness = p;
                    continue;
                }
                if (l == "auto" || l == "from-font") {
                    thickness = l;
                    continue;
                }
                if (color.empty()) {
                    color = p;
                }
            }
        }
    }
    if (line.empty() || lower_ascii(line) == "none") {
        return false;
    }
    TextDecoration dec;
    {
        std::string tok;
        for (char ch : line) {
            if (std::isspace(static_cast<unsigned char>(ch))) {
                if (!tok.empty()) {
                    const std::string l = lower_ascii(tok);
                    if (l == "underline") {
                        dec.underline = true;
                    } else if (l == "overline") {
                        dec.overline = true;
                    } else if (l == "line-through") {
                        dec.line_through = true;
                    }
                    tok.clear();
                }
            } else {
                tok.push_back(ch);
            }
        }
        if (!tok.empty()) {
            const std::string l = lower_ascii(tok);
            if (l == "underline") {
                dec.underline = true;
            } else if (l == "overline") {
                dec.overline = true;
            } else if (l == "line-through") {
                dec.line_through = true;
            }
        }
    }
    if (!dec.underline && !dec.overline && !dec.line_through) {
        return false;
    }
    const std::string style_l = lower_ascii(style);
    if (style_l == "double") {
        dec.style = TextDecorStyle::Double;
    } else if (style_l == "dotted") {
        dec.style = TextDecorStyle::Dotted;
    } else if (style_l == "dashed") {
        dec.style = TextDecorStyle::Dashed;
    } else if (style_l == "wavy") {
        dec.style = TextDecorStyle::Wavy;
    } else {
        dec.style = TextDecorStyle::Solid;
    }
    if (color.empty() || lower_ascii(color) == "currentcolor") {
        dec.color = paint_color(box, computed(box, "color"));
    } else {
        const std::string* fake = &color;
        dec.color = paint_color(box, fake);
    }
    if (!thickness.empty() && lower_ascii(thickness) != "auto" &&
        lower_ascii(thickness) != "from-font") {
        bool bad = false;
        float px = 0.f;
        if (parse_bg_length(thickness, 0.f, px, bad) && !bad && px > 0.f) {
            dec.thickness = px;
        }
    }
    // CSS Text Decoration 4: the offset is from the automatic underline position. Percentages
    // are of 1em. Only an underline reads it; overline and line-through stay put.
    if (dec.underline) {
        if (const std::string* off = computed(box, "text-underline-offset")) {
            float em = 16.f;
            if (box.style_ptr != nullptr && box.style().font_size > 0.f) {
                em = box.style().font_size;
            }
            std::string_view raw(*off);
            while (!raw.empty() &&
                   std::isspace(static_cast<unsigned char>(raw.front()))) {
                raw.remove_prefix(1);
            }
            while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.back()))) {
                raw.remove_suffix(1);
            }
            const std::string token = lower_ascii(raw);
            if (!token.empty() && token != "auto") {
                const bool em_unit =
                    token.size() > 2 && token.compare(token.size() - 2, 2, "em") == 0 &&
                    token.compare(token.size() - 3, 3, "rem") != 0;
                if (em_unit) {
                    const float n = std::strtof(token.c_str(), nullptr);
                    dec.underline_offset = n * em;
                } else {
                    bool bad = false;
                    float px = 0.f;
                    if (parse_bg_length(raw, em, px, bad) && !bad) {
                        dec.underline_offset = px;
                    }
                }
            }
        }
        // CSS Text Decoration 4: `under` moves the automatic position to the font's
        // under edge (below descenders). `from-font` and vertical `left`/`right` stay
        // on the alphabetic position. Read only when an underline is already requested.
        if (const std::string* pos = computed(box, "text-underline-position")) {
            std::string_view raw(*pos);
            std::size_t i = 0;
            while (i < raw.size()) {
                while (i < raw.size() && std::isspace(static_cast<unsigned char>(raw[i]))) {
                    ++i;
                }
                const std::size_t start = i;
                while (i < raw.size() && !std::isspace(static_cast<unsigned char>(raw[i]))) {
                    ++i;
                }
                if (start == i) {
                    break;
                }
                const std::string word = lower_ascii(raw.substr(start, i - start));
                if (word == "under") {
                    dec.underline_under = true;
                }
            }
        }
    }
    // Initial value is `auto`. Read only after a line was requested.
    if (const std::string* skip = computed(box, "text-decoration-skip-ink")) {
        const std::string word = lower_ascii(*skip);
        if (word == "none") {
            dec.skip_ink = SkipInk::None;
        } else if (word == "all") {
            dec.skip_ink = SkipInk::All;
        } else {
            dec.skip_ink = SkipInk::Auto;
        }
    }
    out = dec;
    return !color_is_transparent(out.color);
}

void stroke_decor_segment(DisplayList& out, float x0, float x1, float y, float thickness,
                          Color color, TextDecorStyle style) {
    if (x1 <= x0 || thickness <= 0.f) {
        return;
    }
    const float t = thickness;
    if (style == TextDecorStyle::Solid) {
        out.fill_rect(Rect{x0, y, x1 - x0, t}, color);
        return;
    }
    if (style == TextDecorStyle::Double) {
        out.fill_rect(Rect{x0, y, x1 - x0, t}, color);
        out.fill_rect(Rect{x0, y + t * 2.f, x1 - x0, t}, color);
        return;
    }
    if (style == TextDecorStyle::Dotted) {
        const float step = std::max(t * 2.f, 2.f);
        for (float x = x0; x < x1; x += step) {
            const float w = std::min(t, x1 - x);
            out.fill_rect(Rect{x, y, w, t}, color, t * 0.5f);
        }
        return;
    }
    if (style == TextDecorStyle::Dashed) {
        const float dash = std::max(t * 3.f, 3.f);
        const float gap = std::max(t * 2.f, 2.f);
        for (float x = x0; x < x1; x += dash + gap) {
            const float w = std::min(dash, x1 - x);
            if (w > 0.f) {
                out.fill_rect(Rect{x, y, w, t}, color);
            }
        }
        return;
    }
    // Wavy: sampled sine; amplitude and period scale with thickness.
    const float amp = std::max(t * 1.25f, 1.f);
    const float period = std::max(t * 5.f, 6.f);
    const float step = std::max(1.f, t * 0.5f);
    for (float x = x0; x < x1; x += step) {
        const float phase = (x - x0) * (6.2831853f / period);
        const float yy = y + amp * (0.5f + 0.5f * std::sin(phase));
        const float w = std::min(step + 0.5f, x1 - x);
        out.fill_rect(Rect{x, yy, w, t}, color);
    }
}

}  // namespace painter_detail
}  // namespace paint
}  // namespace zui
