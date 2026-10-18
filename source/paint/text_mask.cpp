#include "paint/painter_internal.h"

#include "paint/display_list.h"
#include "layout/overflow_clip.h"
#include "text/font_chain.h"

#include <algorithm>
#include <cmath>

namespace zui {
namespace paint {
namespace painter_detail {

/// Glyph coverage for `background-clip: text`. Pens match `paint_box_text_runs` for upright runs
/// on this box (inline descendants that share its line boxes included). Block descendants are a
/// later slice. Called only after a text-clip layer was opened.
void emit_background_text_mask(DisplayList& out, const layout::Box& box, float dx, float dy) {
    float scroll_dx = 0.f;
    float scroll_dy = 0.f;
    if (box.style_ptr != nullptr) {
        const layout::OverflowMode ox = box.style().overflow_x;
        const layout::OverflowMode oy = box.style().overflow_y;
        const bool scrolls =
            ox == layout::OverflowMode::Auto || ox == layout::OverflowMode::Scroll ||
            ox == layout::OverflowMode::Hidden || oy == layout::OverflowMode::Auto ||
            oy == layout::OverflowMode::Scroll || oy == layout::OverflowMode::Hidden;
        if (scrolls) {
            scroll_dx = box.geo.scroll_x;
            scroll_dy = box.geo.scroll_y;
            if (box.style().direction == layout::Direction::Rtl) {
                const float view = std::max(0.f, box.geo.width - box.geo.border.horizontal());
                const float extent = std::max(0.f, box.geo.scroll_width - view);
                scroll_dx = box.geo.scroll_x + extent;
            }
        } else if (box.node != nullptr && html::is_text_field(box.node) &&
                   (ox == layout::OverflowMode::Clip || oy == layout::OverflowMode::Clip)) {
            scroll_dx = box.geo.scroll_x;
            scroll_dy = box.geo.scroll_y;
        }
    }
    for (const layout::TextRun& run : box.geo.runs) {
        if (run.text.empty()) {
            continue;
        }
        const layout::Box* face = &box;
        if (run.style_box != nullptr && run.style_box->style_ptr != nullptr) {
            face = run.style_box;
        }
        if (face->style_ptr == nullptr ||
            face->style().writing_mode != layout::WritingMode::HorizontalTb) {
            continue;
        }
        const float text_x = box.geo.world_x + dx + run.x - scroll_dx;
        const float text_y = box.geo.world_y + dy + run.y - scroll_dy + run.ascent;
        const std::vector<std::uint32_t>* ids = run.glyph_ids.empty() ? nullptr : &run.glyph_ids;
        out.draw_text_mask(text_x, text_y, run.text, face->style().font_size,
                           face->style().font_family, face->style().font_weight, box.node,
                           face->style().letter_spacing, face->style().font_italic,
                           face->style().font_kerning_none, &run.glyph_advances, ids);
    }
}

}  // namespace painter_detail
}  // namespace paint
}  // namespace zui
