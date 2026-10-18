#include "paint/painter_internal.h"

#include "paint/display_list.h"
#include "html/document.h"

#include "perf/perf_budget.h"
#include "css/transition/transition.h"
#include "image/image.h"
#include "layout/clip_path.h"
#include "layout/overflow_clip.h"
#include "layout/caret/caret_service.h"
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
#include <vector>

namespace zui {
namespace paint {

using namespace painter_detail;

void Painter::paint_box_contents(const layout::Box& box, DisplayList& out, Rect child_clip,
                                 bool child_has_clip, float op, float dx, float dy,
                                 bool paint_self) const {
    const bool recording = !scopes_.empty();
        paint_replaced_content(box, out, dx, dy, paint_self);

        paint_native_widgets(box, out, dx, dy, op, paint_self);

        // A text box paints in the colour of the box it is a run of. A generated element - the
        // `::placeholder` run of a text control - carries a computed style of its own and is the one
        // exception; a `#text` node never has one, so it still follows its parent.
        const layout::Box* color_src = &box;
        if (box.node == nullptr || (box.role == layout::BoxRole::Text && !box.node->computed)) {
            color_src = box.parent;
            while (color_src != nullptr && color_src->node == nullptr) {
                color_src = color_src->parent;
            }
        }
        Color fg{0, 0, 0, 255};
        if (color_src != nullptr && color_src->node != nullptr &&
            (color_src->node->used.flags & html::VisualUsed::kColor)) {
            fg = overlay_rgba(color_src->node->used.color_r, color_src->node->used.color_g,
                              color_src->node->used.color_b, color_src->node->used.color_a);
        } else if (color_src != nullptr) {
            if (const std::string* v = computed(*color_src, "color")) {
                // `style()` answers nullptr for a missing or empty declaration, so a value here is a
                // real colour - including the explicitly transparent one, which must paint nothing.
                // Treating `transparent` as "no colour" painted black text.
                fg = paint_color(*color_src, v);
            }
        }
        fg = apply_opacity(fg, op);
        // The box's own text runs are not children, so the scroll offset map_box applies to
        // children never reaches them. A nowrap sample's horizontal bar moved the thumb and
        // left the sentence where it was.
        float scroll_dx = 0.f;
        float scroll_dy = 0.f;
        if (box.node != nullptr && box.style_ptr != nullptr) {
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
            } else if (html::is_text_field(box.node) &&
                       (ox == layout::OverflowMode::Clip || oy == layout::OverflowMode::Clip)) {
                // A text field's overflow stays clip, which is not a scroll container, but
                // reveal still shifts `scroll_x` so the caret stays in the client. The glyphs
                // use that same shift. The caret service subtracts `scroll_x` with no RTL extent.
                scroll_dx = box.geo.scroll_x;
                scroll_dy = box.geo.scroll_y;
            }
        }
        // CSS text-overflow:ellipsis on a nowrap block that clips overflow: paint as much text as
        // fits ahead of a U+2026 marker. Full text stays in layout for hit-testing / selection.
        const bool paint_ellipsis = paint_self && box.style_ptr != nullptr &&
                                    box.style().text_overflow == layout::TextOverflow::Ellipsis &&
                                    layout::clips_overflow(box) &&
                                    (box.style().white_space == layout::WhiteSpace::Nowrap ||
                                     box.style().white_space == layout::WhiteSpace::Pre);
        float ellipsis_remain = paint_ellipsis ? std::max(0.f, box.geo.content_width)
                                               : std::numeric_limits<float>::max();
        bool ellipsis_drawn = false;
        if (paint_ellipsis) {
            // The marker only exists when something does not fit. Reserving room for it up front cut a
            // block whose text was exactly as wide as its own content box (the reader's brand name, a
            // flex item sized to its max-content): Chrome paints such a block in full. Only let the
            // reserve start once the content really overflows.
            float total = 0.f;
            for (const auto& run : box.geo.runs) {
                const layout::Box* face = &box;
                if (run.style_box != nullptr && run.style_box->style_ptr != nullptr) {
                    face = run.style_box;
                }
                total += run_advance_px(run);
            }
            if (total <= box.geo.content_width + 0.5f) {
                ellipsis_remain = std::numeric_limits<float>::max();
            }
        }

        // Appendix E: own in-flow text (step 5) paints after negative-z / blocks / floats (steps 2–4)
        // and before positioned / positive-z children. Non-context boxes still reorder float/block/
        // inline children; only a stacking context lifts z-index through `z-index: auto` mid boxes.
        const bool stack_context = box.parent == nullptr ||
                                   (box.style_ptr != nullptr && layout::stacking_info(box).context);
        const bool stack_children = layout::has_stacking_children(box);
        std::vector<layout::Box*> stack_order;
        std::size_t before_own_inlines = 0;
        if (stack_children) {
            layout::stacking_paint_order(box, stack_order, &before_own_inlines);
            if (!(child_has_clip && (child_clip.w <= 0 || child_clip.h <= 0))) {
                // A host that is not a stacking context does not paint its positioned /
                // stacking-context children: appendix E hands them to the nearest ancestor context, so
                // this walk never reaches them. A sticky or fixed box among them is anchored to this
                // scrollport rather than carried by it, and a listing recorded from this walk would not
                // contain it - refuse every open scope instead of recording content that cannot be
                // shifted.
                if (recording && !stack_context) {
                    for (const layout::Box* child : box.children) {
                        if (child == nullptr || child->style_ptr == nullptr) {
                            continue;
                        }
                        const layout::StackingInfo lifted = layout::stacking_info(*child);
                        if ((lifted.positioned || lifted.context) && subtree_has_anchored(*child)) {
                            for (Scope& scope : scopes_) {
                                scope.tainted = true;
                            }
                            break;
                        }
                    }
                }
                for (std::size_t i = 0; i < before_own_inlines && i < stack_order.size(); ++i) {
                    if (stack_order[i] == nullptr) {
                        continue;
                    }
                    if (top_layer_member(*stack_order[i])) {
                        // capability B: a member paints in the top-layer pass, after this walk.
                        continue;
                    }
                    Rect lifted = child_clip;
                    bool lifted_clip = child_has_clip;
                    apply_lifted_clip(*stack_order[i], box, dx, dy, scrollbar_thickness_, lifted,
                                      lifted_clip);
                    const int lifted_clips = push_lifted_overflow_clips(
                        out, *stack_order[i], box, dx, dy, scrollbar_thickness_);
                    paint_box_shifted(*stack_order[i], out, lifted, lifted_clip, op, dx, dy);
                    for (int n = 0; n < lifted_clips; ++n) {
                        out.pop_clip();
                    }
                }
            }
        }

        paint_box_text_runs(box, out, fg, op, dx, dy, scroll_dx, scroll_dy, ellipsis_remain,
                            paint_ellipsis, ellipsis_drawn, paint_self);
        // Caret is a paint-list item from the collapsed document Selection (P5.4).
        if (paint_self && caret_node_ != nullptr && box.node == caret_node_) {
            const html::BoundaryPoint at{caret_node_, caret_offset_};
            const layout::CaretRect bar = layout::caret_rect_at(box, at, caret_affinity_);
            if (bar.ok && bar.height > 0.f) {
                const float x = bar.x + dx;
                const float y = bar.y + dy;
                const float bar_w = bar.width > 0.f ? bar.width : 1.f;
                last_caret_rect_ = CaretBar{x, y, bar_w, bar.height, true};
                // Transparent caret-color or blink-off skips the fill; IME still reads last_caret_rect_.
                if (caret_paint_fill_ && caret_color_.has_value()) {
                    Color c = *caret_color_;
                    c.a = static_cast<std::uint8_t>(
                        static_cast<float>(c.a) * std::max(0.f, std::min(1.f, op)));
                    if (c.a != 0) {
                        out.fill_rect(Rect{x, y, bar_w, bar.height}, c);
                    }
                }
            }
        }

        // IME preedit underline (spec 5.3): mark [composition_start_, composition_end_).
        if (paint_self && composition_node_ != nullptr && box.node == composition_node_ &&
            composition_start_ < composition_end_) {
            const std::string& value = composition_node_->value;
            const std::size_t c0 = std::min(composition_start_, value.size());
            const std::size_t c1 = std::min(composition_end_, value.size());
            if (c0 < c1) {
                const layout::EditMapping mapping = layout::make_edit_mapping(box, value);
                Color underline = caret_color_.value_or(Color{0, 0, 0, 255});
                underline.a = static_cast<std::uint8_t>(
                    static_cast<float>(underline.a) * std::max(0.f, std::min(1.f, op)));
                for (const layout::MappedSpan& span : mapping.spans) {
                    const std::size_t a = std::max(c0, span.byte_begin);
                    const std::size_t b = std::min(c1, span.byte_end);
                    if (a >= b) {
                        continue;
                    }
                    const layout::CaretRect ra =
                        layout::get_rect(box, mapping, a, layout::CaretAffinity::Downstream);
                    const layout::CaretRect rb =
                        layout::get_rect(box, mapping, b, layout::CaretAffinity::Downstream);
                    if (!ra.ok || !rb.ok || ra.height <= 0.f) {
                        continue;
                    }
                    const float x0 = std::min(ra.x, rb.x) + dx;
                    const float x1 = std::max(ra.x, rb.x) + dx;
                    const float uy = ra.y + ra.height - 1.f + dy;
                    if (x1 > x0 && underline.a != 0) {
                        out.fill_rect(Rect{x0, uy, x1 - x0, 1.f}, underline);
                    }
                }
            }
        }

        if (!(child_has_clip && (child_clip.w <= 0 || child_clip.h <= 0))) {
            if (!stack_children) {
                for (const auto& ch : box.children) {
                    if (ch == nullptr) {
                        continue;
                    }
                    if (top_layer_member(*ch)) {
                        // capability B: a member paints in the top-layer pass. It never reaches a
                        // listing, so it must not taint the scope the way an ordinary lifted box
                        // does - the exclusion is by design, not a box this walk cannot record.
                        continue;
                    }
                    if (layout::stacking_skip_lifted_child(box, *ch)) {
                        // Lifted to an ancestor context, so this walk never visits it: the same
                        // refusal the ordered path makes above applies here.
                        if (recording && subtree_has_anchored(*ch)) {
                            for (Scope& scope : scopes_) {
                                scope.tainted = true;
                            }
                        }
                        continue;
                    }
                    paint_box_shifted(*ch, out, child_clip, child_has_clip, op, dx, dy);
                }
            } else {
                for (std::size_t i = before_own_inlines; i < stack_order.size(); ++i) {
                    if (stack_order[i] == nullptr) {
                        continue;
                    }
                    if (top_layer_member(*stack_order[i])) {
                        // capability B: a member paints in the top-layer pass, after this walk.
                        continue;
                    }
                    Rect lifted = child_clip;
                    bool lifted_clip = child_has_clip;
                    apply_lifted_clip(*stack_order[i], box, dx, dy, scrollbar_thickness_, lifted,
                                      lifted_clip);
                    const int lifted_clips = push_lifted_overflow_clips(
                        out, *stack_order[i], box, dx, dy, scrollbar_thickness_);
                    paint_box_shifted(*stack_order[i], out, lifted, lifted_clip, op, dx, dy);
                    for (int n = 0; n < lifted_clips; ++n) {
                        out.pop_clip();
                    }
                }
            }
        }
}

}  // namespace paint
}  // namespace zui
