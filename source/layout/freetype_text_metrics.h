#ifndef ZUI_LAYOUT_FREETYPE_TEXT_METRICS_H
#define ZUI_LAYOUT_FREETYPE_TEXT_METRICS_H

#include "layout/measured_text.h"

#include "text/font_chain.h"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace zui {
namespace layout {

class FreeTypeTextMetrics {
public:
    FreeTypeTextMetrics() { init("sans-serif"); }
    bool init(std::string_view families = "sans-serif");
    bool ready() const { return chain_.ready(); }
    text::FontChain& chain() { return chain_; }
    /// One chain per CSS font-family list, so `font-family: arial` really shapes with Arial
    /// instead of whichever family the engine happened to open first.
    text::FontChain& chain_for(const std::string& families, int weight = 400, bool italic = false);
    MeasuredText measure(std::string_view text, float font_size, const std::string& family);

private:
    text::FontChain chain_;
    std::unordered_map<std::string, std::unique_ptr<text::FontChain>> chains_;
    /// The style of the last `chain_for` lookup and the chain it resolved to. Building the map key
    /// allocates a string, and these calls arrive in runs of one style - every box of a list, every
    /// line of a paragraph, every glyph of a run - so one entry removes almost all of that work.
    std::string last_families_;
    int last_weight_ = 0;
    bool last_italic_ = false;
    text::FontChain* last_chain_ = nullptr;
};

}  // namespace layout
}  // namespace zui

#endif
