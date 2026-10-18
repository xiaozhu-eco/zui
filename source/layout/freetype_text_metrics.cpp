#include "layout/freetype_text_metrics.h"

#include <string>
#include <utility>

namespace zui {
namespace layout {

bool FreeTypeTextMetrics::init(std::string_view families) { return chain_.init(families); }

text::FontChain& FreeTypeTextMetrics::chain_for(const std::string& families, int weight,
                                                bool italic) {
    if (families.empty() && weight == 400 && !italic) {
        return chain_;
    }
    if (last_chain_ != nullptr && weight == last_weight_ && italic == last_italic_ &&
        families == last_families_) {
        return *last_chain_;
    }
    const std::string key = families + "|" + std::to_string(weight) + (italic ? "|i" : "|n");
    auto it = chains_.find(key);
    if (it != chains_.end()) {
        last_families_ = families;
        last_weight_ = weight;
        last_italic_ = italic;
        last_chain_ = it->second.get();
        return *it->second;
    }
    auto created = std::make_unique<text::FontChain>();
    if (!created->init(families.empty() ? "sans-serif" : families, weight, italic)) {
        return chain_;
    }
    text::FontChain& stored = *created;
    chains_.emplace(key, std::move(created));
    last_families_ = families;
    last_weight_ = weight;
    last_italic_ = italic;
    last_chain_ = &stored;
    return stored;
}

MeasuredText FreeTypeTextMetrics::measure(std::string_view text, float font_size,
                                          const std::string& family) {
    MeasuredText out;
    text::FontChain& chain = chain_for(family);
    size_t i = 0;
    // Kerning between adjacent glyphs of one face: without it a string like `AVAV` measures
    // wider than the same string in a browser, and every width derived from this path (the L0
    // advance probe included) inherits that.
    text::LoadedGlyph previous;
    bool have_previous = false;
    while (i < text.size()) {
        const uint32_t cp = text::next_cp(text, i);
        const text::LoadedGlyph glyph = chain.load(cp, font_size, false);
        if (have_previous && glyph.ok && previous.ok && glyph.face_key == previous.face_key) {
            out.width += chain.kerning(glyph.face_key, previous.glyph_index, glyph.glyph_index);
        }
        out.width += glyph.advance;
        previous = glyph;
        have_previous = true;
    }
    // One ceil over the whole string, matching what the browser reports for a single text run.
    out.width = text::ceil_advance(out.width);
    return out;
}

}  // namespace layout
}  // namespace zui
