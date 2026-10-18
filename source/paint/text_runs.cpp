#include "paint/painter_internal.h"

#include "paint/display_list.h"
#include "html/selection.h"

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
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace zui {
namespace paint {

using namespace painter_detail;


namespace {

/// True when an ancestor already carries shaped `geo.runs` for this text (the IFC that
/// collected the glyphs). Immediate `parent` can be an anonymous inline with empty runs.
bool text_ancestor_already_paints(const layout::Box& box) {
    for (const layout::Box* p = box.parent; p != nullptr; p = p->parent) {
        if (!p->geo.runs.empty()) {
            return true;
        }
    }
    return false;
}

}  // namespace

void Painter::paint_box_text_runs(const layout::Box& box, DisplayList& out, Color fg, float op,
                                  float dx, float dy, float scroll_dx, float scroll_dy,
                                  float& ellipsis_remain, bool paint_ellipsis, bool& ellipsis_drawn,
                                  bool paint_self) const {
    if (paint_self && !box.geo.runs.empty()) {
        for (const auto& run : box.geo.runs) {
            if (ellipsis_drawn) {
                break;
            }
            const layout::Box* face = &box;
            if (run.style_box != nullptr && run.style_box->style_ptr != nullptr) {
                face = run.style_box;
            }
            Color run_fg = fg;
            if (face != &box) {
                run_fg = Color{0, 0, 0, 255};
                if (face->node != nullptr && (face->node->used.flags & html::VisualUsed::kColor)) {
                    run_fg = overlay_rgba(face->node->used.color_r, face->node->used.color_g,
                                          face->node->used.color_b, face->node->used.color_a);
                } else if (const std::string* v = computed(*face, "color")) {
                    run_fg = paint_color(*face, v);
                }
                run_fg = apply_opacity(run_fg, op);
            }
            // Line origins are physical inside the border box. Aff2 boxes keep the pen in local
            // space; axis-aligned boxes still add world_x/y. Under vertical/sideways writing-mode
            // the alphabetic baseline is a vertical line and glyphs advance on Y.
            const TextGlyphOrient orient = glyph_orient_for(face->style().writing_mode);
            const bool text_xform = box.paint_transformed;
            float text_x = (text_xform ? 0.f : box.geo.world_x) + dx + run.x - scroll_dx;
            float text_y = (text_xform ? 0.f : box.geo.world_y) + dy + run.y - scroll_dy;
            if (orient == TextGlyphOrient::Upright) {
                text_y += run.ascent;
            } else if (orient == TextGlyphOrient::SidewaysRl) {
                text_x -= run.ascent;
            } else {
                text_x += run.ascent;
            }
            if (orient == TextGlyphOrient::Upright && !text_xform && !run.text.empty()) {
                // Read only inside this branch. An unset emphasis property returns immediately.
                emit_text_emphasis(out, *face, text_x, text_y, run, op);
            }
            const float letter_spacing = face->style().letter_spacing;
            const html::Node* owning = run_owning_node(box, run);
            // A mixed inline's bytes live on the `#text` child. Highlight colour stays on the
            // element the run is stamped with, which is the node that inherited `::selection`.
            const html::Node* slice_node = run.text_node != nullptr ? run.text_node : owning;
            std::vector<TextShadowLayer> shadows;
            if (const std::string* shadow_v = computed(*face, "text-shadow")) {
                shadows = parse_text_shadows(*shadow_v, *face);
            }
            TextDecoration decoration;
            const bool has_decoration =
                orient == TextGlyphOrient::Upright && parse_text_decoration(*face, decoration);
            const float run_w = run_advance_px(run);
            auto emit_run = [&](float x, float y, Color color, std::string_view chars,
                                float spacing, const std::vector<TextShadowLayer>& sh,
                                const std::vector<float>* advances = nullptr,
                                const std::vector<std::uint32_t>* glyph_ids = nullptr) {
                emit_text_with_shadows(
                    out, x, y, color, chars, face->style().font_size, face->style().font_family,
                    face->style().font_weight, owner_node(box), spacing, face->style().font_italic,
                    face->style().font_kerning_none, op, sh, orient, text_xform, box.paint_a,
                    box.paint_b, box.paint_c, box.paint_d, box.paint_e, box.paint_f, advances,
                    glyph_ids);
            };
            const std::vector<std::uint32_t>* run_ids =
                run.glyph_ids.empty() ? nullptr : &run.glyph_ids;
            std::string paint_text = run.text;
            float paint_w = run_w;
            if (paint_ellipsis && orient == TextGlyphOrient::Upright && run.mappable &&
                !run.text.empty()) {
                // Reserve ~1em for U+2026; good enough vs FreeType without a metrics handle here.
                const float ell_w = std::max(face->style().font_size * 0.75f, 1.f);
                if (run.x >= ellipsis_remain) {
                    emit_run(text_x, text_y, run_fg, "\xE2\x80\xA6", 0.f, shadows);
                    ellipsis_drawn = true;
                    break;
                }
                const float budget = ellipsis_remain - run.x;
                if (run_w + ell_w > budget) {
                    float used = 0.f;
                    std::size_t byte = 0;
                    std::size_t keep_bytes = 0;
                    std::size_t kept = 0;
                    for (float adv : run.glyph_advances) {
                        if (byte >= run.text.size()) {
                            break;
                        }
                        const std::size_t start = byte;
                        text::next_cp(run.text, byte);
                        if (used + adv + ell_w > budget) {
                            break;
                        }
                        used += adv;
                        keep_bytes = byte;
                        ++kept;
                        (void)start;
                    }
                    paint_text = run.text.substr(0, keep_bytes);
                    paint_w = used;
                    // The kept prefix keeps the layout's advances so the visible glyphs sit
                    // where the layout put them (justification, kerning, letter-spacing).
                    std::vector<float> kept_advances(
                        run.glyph_advances.begin(),
                        run.glyph_advances.begin() +
                            static_cast<std::ptrdiff_t>(std::min(kept, run.glyph_advances.size())));
                    std::vector<std::uint32_t> kept_ids;
                    const std::vector<std::uint32_t>* kept_ids_ptr = nullptr;
                    if (!run.glyph_ids.empty()) {
                        const std::size_t n = std::min(kept, run.glyph_ids.size());
                        kept_ids.assign(run.glyph_ids.begin(),
                                        run.glyph_ids.begin() + static_cast<std::ptrdiff_t>(n));
                        kept_ids_ptr = &kept_ids;
                    }
                    emit_run(text_x, text_y, run_fg, paint_text, letter_spacing, shadows,
                             &kept_advances, kept_ids_ptr);
                    emit_run(text_x + paint_w, text_y, run_fg, "\xE2\x80\xA6", 0.f, shadows);
                    ellipsis_drawn = true;
                    continue;
                }
                ellipsis_remain = budget - run_w;
            }
            std::size_t slice_a = 0;
            std::size_t slice_b = 0;
            const bool select_here =
                orient == TextGlyphOrient::Upright && run.mappable && slice_node != nullptr &&
                !is_placeholder_node(slice_node) && !run.text.empty() &&
                html::selection_slice(slice_node, run.byte_begin,
                                      run.byte_begin + run.text.size(), selection_, slice_a,
                                      slice_b);
            if (!select_here) {
                emit_run(text_x, text_y, run_fg, paint_text, letter_spacing, shadows,
                         &run.glyph_advances, run_ids);
                if (has_decoration) {
                    emit_text_decoration(out, text_x, text_y, paint_w, run.ascent,
                                         face->style().font_size,
                                         run.line_block > 0.f ? run.line_block - run.ascent
                                                              : run.font_descent,
                                         decoration, op, &run, face);
                }
                continue;
            }
            const std::size_t run_begin = run.byte_begin;
            const std::size_t run_end = run_begin + run.text.size();
            const std::size_t sel_a = slice_a;
            const std::size_t sel_b = slice_b;
            if (sel_a >= sel_b) {
                emit_run(text_x, text_y, run_fg, run.text, letter_spacing, shadows,
                         &run.glyph_advances, run_ids);
                if (has_decoration) {
                    emit_text_decoration(out, text_x, text_y, run_w, run.ascent,
                                         face->style().font_size,
                                         run.line_block > 0.f ? run.line_block - run.ascent
                                                              : run.font_descent,
                                         decoration, op, &run, face);
                }
                continue;
            }
            Color sel_fg;
            Color sel_bg;
            selection_colors(owning, sel_fg, sel_bg);
            sel_fg = apply_opacity(sel_fg, op);
            sel_bg = apply_opacity(sel_bg, op);
            const std::size_t local_a = sel_a - run_begin;
            const std::size_t local_b = sel_b - run_begin;
            const float x0 = run_byte_x(run, local_a);
            const float x1 = run_byte_x(run, local_b);
            if (!color_is_transparent(sel_bg) && x1 > x0) {
                // The highlight is the line box, not the em square. `text_y - ascent` is the
                // line-box start (`run.y`). A 24px line with a 16px font was leaving the bottom
                // of the line unpainted (selection, p#p).
                const float sel_h = run.line_block > 0.f ? run.line_block : face->style().font_size;
                const Rect sel_r{text_x + x0, text_y - run.ascent, x1 - x0, sel_h};
                if (text_xform) {
                    out.fill_rect_xform(sel_r, sel_bg, CornerRadii{}, box.paint_a, box.paint_b,
                                        box.paint_c, box.paint_d, box.paint_e, box.paint_f);
                } else {
                    out.fill_rect(sel_r, sel_bg);
                }
            }
            // A selection slice is the same shaped run. The pen stays on the layout advances.
            // A cut that lands inside a codepoint moves to that codepoint's lead byte, and the
            // character belongs to the following slice. Extending the left slice forward as well
            // drew the character twice and started the next slice a glyph early.
            auto snap_boundary = [](std::string_view s, std::size_t i) {
                if (i > s.size()) {
                    i = s.size();
                }
                while (i > 0 && i < s.size() &&
                       (static_cast<unsigned char>(s[i]) & 0xc0) == 0x80) {
                    --i;
                }
                return i;
            };
            // One advance per codepoint, the same index the raster consumes from the full
            // vector. A per-byte ASCII run has the same index; a CJK run stores one advance
            // per character, not per byte.
            auto codepoint_advances = [&](std::size_t from, std::size_t to, std::vector<float>& adv,
                                          std::vector<std::uint32_t>& ids) {
                adv.clear();
                ids.clear();
                if (run.glyph_advances.empty() || from >= to) {
                    return;
                }
                const bool have_ids = run.glyph_ids.size() == run.glyph_advances.size();
                std::size_t b = 0;
                std::size_t gi = 0;
                while (b < run.text.size() && gi < run.glyph_advances.size()) {
                    const std::size_t start = b;
                    text::next_cp(run.text, b);
                    if (start >= to) {
                        break;
                    }
                    if (b > from && start < to) {
                        adv.push_back(run.glyph_advances[gi]);
                        if (have_ids) {
                            ids.push_back(run.glyph_ids[gi]);
                        }
                    }
                    ++gi;
                }
            };
            auto draw_slice = [&](std::size_t from, std::size_t to, Color color, bool with_shadow) {
                from = snap_boundary(run.text, from);
                to = snap_boundary(run.text, to);
                if (from >= to || to > run.text.size()) {
                    return;
                }
                const float slice_x = text_x + run_byte_x(run, from);
                const std::string_view slice = std::string_view(run.text).substr(from, to - from);
                std::vector<float> adv;
                std::vector<std::uint32_t> ids;
                codepoint_advances(from, to, adv, ids);
                const std::vector<float>* adv_p = adv.empty() ? nullptr : &adv;
                const std::vector<std::uint32_t>* id_p = ids.empty() ? nullptr : &ids;
                if (with_shadow) {
                    emit_run(slice_x, text_y, color, slice, letter_spacing, shadows, adv_p, id_p);
                } else if (text_xform) {
                    out.draw_text_xform(slice_x, text_y, color, slice, face->style().font_size,
                                        face->style().font_family, box.paint_a, box.paint_b,
                                        box.paint_c, box.paint_d, box.paint_e, box.paint_f,
                                        face->style().font_weight, owner_node(box), letter_spacing,
                                        face->style().font_italic, face->style().font_kerning_none,
                                        0.f, orient, adv_p, id_p);
                } else {
                    out.draw_text(slice_x, text_y, color, slice, face->style().font_size,
                                  face->style().font_family, face->style().font_weight,
                                  owner_node(box), letter_spacing, face->style().font_italic,
                                  face->style().font_kerning_none, 0.f, orient, adv_p, id_p);
                }
            };
            // Shadows follow the unselected ink; the selected slice keeps highlight colours only.
            draw_slice(0, local_a, run_fg, true);
            draw_slice(local_a, local_b, sel_fg, false);
            draw_slice(local_b, run.text.size(), run_fg, true);
            if (has_decoration) {
                emit_text_decoration(out, text_x, text_y, run_w, run.ascent,
                                     face->style().font_size,
                                     run.line_block > 0.f ? run.line_block - run.ascent
                                                          : run.font_descent,
                                     decoration, op, &run, face);
            }
        }
    } else if (paint_self && box.role == layout::BoxRole::Text && !box.text.empty() &&
               !text_ancestor_already_paints(box)) {
        // Only when no ancestor IFC already emitted this text as shaped runs. An anonymous
        // inline between a `#text` and the span/block that owns `geo.runs` used to look like
        // `parent->geo.runs.empty()` and fall through here: the fallback painted every text
        // fragment at the `#text` box origin (line start) without layout advances, stacking
        // mid-line CJK on top of the first glyphs (zmd welcome body / caret split).
        // Guard against culled-but-reachable anonymous text still holding a previous
        // scroll's world_y (reader ghost titles).
        bool stale = false;
        if (box.parent != nullptr && !box.parent->geo.culled) {
            const float py = box.parent->geo.world_y;
            const float ph = std::max(box.parent->geo.height, 1.f);
            if (box.geo.world_y < py - 1.f || box.geo.world_y > py + ph + 1.f) {
                stale = true;
            }
        }
        if (!stale) {
            std::vector<TextShadowLayer> shadows;
            if (const std::string* shadow_v = computed(box, "text-shadow")) {
                shadows = parse_text_shadows(*shadow_v, box);
            }
            const TextGlyphOrient orient = glyph_orient_for(box.style().writing_mode);
            const bool text_xform = box.paint_transformed;
            float tx = (text_xform ? 0.f : box.geo.world_x) + dx;
            float ty = (text_xform ? 0.f : box.geo.world_y) + dy;
            if (orient == TextGlyphOrient::Upright) {
                ty += box.geo.baseline;
            } else if (orient == TextGlyphOrient::SidewaysRl) {
                tx -= box.geo.baseline;
            } else {
                tx += box.geo.baseline;
            }
            emit_text_with_shadows(out, tx, ty, fg, box.text, box.style().font_size,
                                   box.style().font_family, box.style().font_weight,
                                   owner_node(box), box.style().letter_spacing,
                                   box.style().font_italic, box.style().font_kerning_none, op,
                                   shadows, orient, text_xform, box.paint_a, box.paint_b,
                                   box.paint_c, box.paint_d, box.paint_e, box.paint_f);
            TextDecoration decoration;
            if (orient == TextGlyphOrient::Upright && parse_text_decoration(box, decoration)) {
                const float w = std::max(0.f, box.geo.content_width);
                const float under_edge = box.geo.height - box.geo.baseline;
                layout::TextRun synth;
                synth.text = box.text;
                emit_text_decoration(out, tx, ty, w, box.geo.baseline, box.style().font_size,
                                     under_edge > 0.f ? under_edge : box.style().font_size * 0.2f,
                                     decoration, op, &synth, &box);
            }
        }
    }
}

}  // namespace paint
}  // namespace zui
