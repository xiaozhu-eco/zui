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
/// The run's painted width. `glyph_advances` already include `letter-spacing` - the layout adds
/// it after every character including the last - so this is a plain sum.
void append_utf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

char32_t first_cp(std::string_view s) {
    std::size_t i = 0;
    if (i >= s.size()) {
        return 0;
    }
    return static_cast<char32_t>(text::next_cp(s, i));
}

/// Separators and punctuation do not take a mark. The symbols the spec lists as exceptions
/// (number sign, percent, ampersand, commercial at, and a few letterlike signs) still do.
bool skip_emphasis_cp(char32_t cp) {
    switch (cp) {
        case 0x0023:
        case 0x0025:
        case 0x0026:
        case 0x0040:
        case 0x00A7:
        case 0x00B6:
        case 0x0609:
        case 0x060A:
        case 0x066A:
        case 0x2030:
        case 0x2031:
        case 0x204A:
        case 0x204B:
        case 0x2053:
        case 0x303D:
            return false;
        default:
            break;
    }
    if (cp <= 0x20 || cp == 0xA0 || cp == 0x1680 || cp == 0x3000 ||
        (cp >= 0x2000 && cp <= 0x200A) || cp == 0x2028 || cp == 0x2029 || cp == 0x202F ||
        cp == 0x205F || cp == 0xFEFF) {
        return true;
    }
    if (cp < 0x80 && std::ispunct(static_cast<int>(cp)) != 0) {
        return true;
    }
    if (cp >= 0x7F && cp <= 0x9F) {
        return true;
    }
    if (cp >= 0x2000 && cp <= 0x206F) {
        return true;
    }
    if (cp >= 0x3001 && cp <= 0x303F) {
        return true;
    }
    if ((cp >= 0xFF01 && cp <= 0xFF0F) || (cp >= 0xFF1A && cp <= 0xFF20) ||
        (cp >= 0xFF3B && cp <= 0xFF40) || (cp >= 0xFF5B && cp <= 0xFF65)) {
        return true;
    }
    return false;
}

char32_t emphasis_mark_cp(bool open, std::string_view shape) {
    if (shape == "dot") {
        return open ? 0x25E6 : 0x2022;
    }
    if (shape == "circle") {
        return open ? 0x25CB : 0x25CF;
    }
    if (shape == "double-circle") {
        return open ? 0x25CE : 0x25C9;
    }
    if (shape == "triangle") {
        return open ? 0x25B3 : 0x25B2;
    }
    if (shape == "sesame") {
        return open ? 0xFE46 : 0xFE45;
    }
    return 0;
}

bool parse_emphasis_style(std::string_view raw, char32_t& cp, std::string& color_tok) {
    std::vector<std::string> tokens;
    std::string cur;
    int depth = 0;
    for (char ch : raw) {
        if (ch == '(') {
            ++depth;
            cur.push_back(ch);
            continue;
        }
        if (ch == ')') {
            if (depth > 0) {
                --depth;
            }
            cur.push_back(ch);
            continue;
        }
        if (depth == 0 && std::isspace(static_cast<unsigned char>(ch)) != 0) {
            if (!cur.empty()) {
                tokens.push_back(std::move(cur));
                cur.clear();
            }
            continue;
        }
        cur.push_back(ch);
    }
    if (!cur.empty()) {
        tokens.push_back(std::move(cur));
    }
    bool open = false;
    bool saw_fill = false;
    bool saw_none = false;
    std::string shape;
    char32_t literal = 0;
    for (const std::string& token : tokens) {
        if (token.size() >= 2 && (token.front() == '"' || token.front() == '\'')) {
            const char quote = token.front();
            std::string_view body(token);
            body.remove_prefix(1);
            if (!body.empty() && body.back() == quote) {
                body.remove_suffix(1);
            }
            literal = first_cp(body);
            continue;
        }
        const std::string word = lower_ascii(token);
        if (word == "none") {
            saw_none = true;
        } else if (word == "filled") {
            saw_fill = true;
            open = false;
        } else if (word == "open") {
            saw_fill = true;
            open = true;
        } else if (word == "dot" || word == "circle" || word == "double-circle" ||
                   word == "triangle" || word == "sesame") {
            shape = word;
        } else if (word != "over" && word != "under" && word != "left" && word != "right") {
            color_tok = token;
        }
    }
    if (literal != 0) {
        cp = literal;
        return true;
    }
    if (shape.empty() && saw_fill) {
        // Horizontal typographic mode: a fill with no shape is `circle`.
        shape = "circle";
    }
    if (shape.empty()) {
        return !saw_none && false;
    }
    cp = emphasis_mark_cp(open, shape);
    return cp != 0;
}

void emit_text_emphasis(DisplayList& out, const layout::Box& box, float text_x, float text_y,
                        const layout::TextRun& run, float opacity) {
    if (run.text.empty() || box.node == nullptr) {
        return;
    }
    const std::string* style_v = computed(box, "text-emphasis-style");
    const std::string* shorthand = computed(box, "text-emphasis");
    const bool style_set = style_v != nullptr && !style_v->empty();
    const bool short_set = shorthand != nullptr && !shorthand->empty();
    if (!style_set && !short_set) {
        return;
    }
    char32_t mark = 0;
    std::string color_tok;
    const std::string& source = style_set ? *style_v : *shorthand;
    if (!parse_emphasis_style(source, mark, color_tok)) {
        return;
    }
    if (!style_set && short_set) {
        // The shorthand's color token was parsed above. A separate longhand still wins.
    }
    const std::string* color_long = computed(box, "text-emphasis-color");
    Color color{0, 0, 0, 255};
    if (color_long != nullptr && !color_long->empty()) {
        color = paint_color(box, color_long);
    } else if (!color_tok.empty()) {
        color = paint_color(box, &color_tok);
    } else if (const std::string* fg = computed(box, "color")) {
        color = paint_color(box, fg);
    }
    bool under = false;
    if (const std::string* pos = computed(box, "text-emphasis-position")) {
        std::string_view raw(*pos);
        std::size_t i = 0;
        while (i < raw.size()) {
            while (i < raw.size() && std::isspace(static_cast<unsigned char>(raw[i])) != 0) {
                ++i;
            }
            const std::size_t start = i;
            while (i < raw.size() && std::isspace(static_cast<unsigned char>(raw[i])) == 0) {
                ++i;
            }
            if (start == i) {
                break;
            }
            if (lower_ascii(raw.substr(start, i - start)) == "under") {
                under = true;
            }
        }
    }
    const float font_px = box.style().font_size;
    const float mark_px = std::max(1.f, font_px * 0.5f);
    // `over` hangs the mark off the em-box top. Edge's filled dot then sits 1/16 em
    // lower than that edge (measured on Noto Sans at 32px: rows 54-57, not 52-55).
    // `under` hangs off the em-box bottom by the same gap. Edge also grows the block
    // for `under`; that layout stays out until the block-size files are free.
    const float gap = font_px * 0.0625f;
    const float baseline = under ? text_y + font_px + gap : text_y - font_px + gap;
    float mark_adv = mark_px;
    {
        // One unrendered load, only after a mark was requested, so the pen can center
        // the glyph's real advance. A bullet is much narrower than the 50% em.
        text::FontChain chain;
        if (chain.init(box.style().font_family, box.style().font_weight, box.style().font_italic)) {
            const text::LoadedGlyph glyph =
                chain.load(static_cast<std::uint32_t>(mark), mark_px, false);
            if (glyph.advance > 0.f) {
                mark_adv = glyph.advance;
            }
        }
    }
    std::string mark_utf8;
    append_utf8(mark_utf8, mark);
    const std::vector<TextShadowLayer> no_shadows;
    float pen = text_x;
    std::size_t byte = 0;
    std::size_t glyph = 0;
    const float fallback_adv = font_px;
    while (byte < run.text.size()) {
        const char32_t cp = static_cast<char32_t>(text::next_cp(run.text, byte));
        float adv = fallback_adv;
        if (glyph < run.glyph_advances.size()) {
            adv = run.glyph_advances[glyph];
        }
        ++glyph;
        if (skip_emphasis_cp(cp) || adv <= 0.f) {
            pen += adv;
            continue;
        }
        const float mark_x = pen + (adv - mark_adv) * 0.5f;
        emit_text_with_shadows(out, mark_x, baseline, color, mark_utf8, mark_px,
                               box.style().font_family, box.style().font_weight, owner_node(box),
                               0.f, box.style().font_italic, box.style().font_kerning_none, opacity,
                               no_shadows, TextGlyphOrient::Upright, false, 1.f, 0.f, 0.f, 1.f, 0.f,
                               0.f, nullptr, nullptr);
        pen += adv;
    }
}

}  // namespace painter_detail
}  // namespace paint
}  // namespace zui
