#include "paint/display_list.h"
#include "paint/painter_internal.h"

#include "perf/perf_budget.h"
#include "css/transition/transition.h"
#include "image/image.h"
#include "log/log.h"
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

namespace {

/// Pixels a nested listing can actually hold. The stored clip is the scrollport grown by
/// `kRetainedMargin` so a parent listing can scroll this one into view, and that rectangle also
/// covers siblings this listing never paints. A sibling tile then erased the listing (the stage-0
/// select under the tile). Clamp the margin to the scrollable overflow.
Rect nested_listing_hit(const layout::Box& box, float thickness) {
    if (box.style_ptr == nullptr) {
        return Rect{};
    }
    const layout::LayoutStyle& style = box.style();
    if (style.direction == layout::Direction::Rtl ||
        style.writing_mode != layout::WritingMode::HorizontalTb) {
        return Rect{};
    }
    const Rect port = scrollport_rect(box, 0.f, 0.f, thickness);
    if (port.w <= 0.f || port.h <= 0.f) {
        return Rect{};
    }
    const Rect grown{port.x - layout::kRetainedMargin, port.y - layout::kRetainedMargin,
                     port.w + 2.f * layout::kRetainedMargin,
                     port.h + 2.f * layout::kRetainedMargin};
    const Rect pad = padding_rect(box, 0.f, 0.f);
    const float content_w = std::max(box.geo.scroll_width, port.w);
    const float content_h = std::max(box.geo.scroll_height, port.h);
    const Rect content{pad.x - box.geo.scroll_x, pad.y - box.geo.scroll_y, content_w, content_h};
    return intersect_rect(grown, content);
}

}  // namespace

void Painter::set_resource_base(std::string document_path) {
    if (document_path.empty()) {
        resource_base_.clear();
        return;
    }
    resource_base_ = std::filesystem::path(document_path).parent_path().string();
}

void Painter::paint(const layout::Box& root, DisplayList& out, float viewport_w,
                    float viewport_h) const {
    out.clear();
    last_caret_rect_ = CaretBar{};
    // Section 9 step D: nothing a listing recorded survives a change to a paint input. The stamp
    // is bumped by whoever made the change (the cascade, the transition runner, the layout engine),
    // so the painter does not have to guess and a caller cannot forget.
    static const std::vector<html::PaintRect> kNoDamage;
    html::DamageTracker* tracker =
        root.node != nullptr ? html::tracker_of(root.node) : nullptr;
    if (tracker == nullptr && root.parent != nullptr) {
        tracker = html::tracker_of(root.parent->node);
    }
    const std::uint64_t stamp = tracker != nullptr ? tracker->paint_stamp() : stamp_ + 1;
    const bool switched = tracker != stamp_tracker_;
    std::size_t damaged = 0;
    std::size_t damaged_items = 0;
    if (tracker == nullptr || switched || stamp != stamp_) {
        stamp_tracker_ = tracker;
        stamp_ = tracker != nullptr ? stamp : stamp_;
        // docs/PERF.md I6: a frame whose damage extent is known only has to re-record the listings
        // that extent touches. A damage with no extent (a source that could not say what it
        // changed, or a node list nobody resolved) still drops every listing - that is what keeps
        // an unmodelled change from leaving a stale one behind.
        // Keep the selective path off by default until it is verified against a real window: the
        // calendar's edit dialog was replayed from a retained listing at partial coverage while it
        // was on (see PERF-NOTES F2). `ZUI_PERF_DAMAGE=1` turns it on for the next round of work.
        // The damage this frame could localize. `paint_damage_full` is the frame that could not;
        // an unresolved node list means nobody turned it into rectangles, which is the same thing.
        const bool damage_full = tracker == nullptr || switched || tracker->full() ||
                                 !tracker->nodes().empty();
        // docs/PERF_REFACTOR.md stage 0 / capability C: the rectangles resolved for this frame. A
        // whole-frame frame reports the one rectangle that also carries `paint_damage_full`.
        const std::vector<html::PaintRect>& damage_rects =
            tracker != nullptr ? tracker->rects() : kNoDamage;
        // capability B: the damage a top-layer member owns. No listing holds a member, so this set
        // is only read to tell an overlay-only frame apart from an unlocalized one.
        const std::vector<html::PaintRect>& overlay_rects =
            tracker != nullptr ? tracker->overlay() : kNoDamage;
        perf::note_paint_damage_rects(
            damage_full ? 1u : damage_rects.size() + overlay_rects.size());
        // The escape hatch: `ZUI_PERF_DAMAGE=0` keeps the old wholesale drop for a caller comparing
        // the two paths. Anything else (including unset) uses the localized damage.
        const char* damage_env = std::getenv("ZUI_PERF_DAMAGE");
        const bool selective = damage_env == nullptr || std::string_view(damage_env) != "0";
        const bool full = damage_full || !selective;
        const bool overlay_only = !overlay_rects.empty() && damage_rects.empty();
        tile_rebuild_.clear();
        if (full) {
            records_.clear();
        } else {
            for (auto it = records_.begin(); it != records_.end();) {
                const Record& record = it->second;
                const layout::Box* listed = it->first;
                // An empty set with a moved stamp is a change nobody could localize; dropping every
                // listing keeps that from leaving a stale one behind (the old `d.w <= 0` case).
                // A frame whose only damage is the overlay changes nothing a listing holds, so it
                // is the one case where an empty set is *not* "nobody could localize".
                Rect hit = record.damage_clip;
                if (listed != &root) {
                    const Rect content = nested_listing_hit(*listed, scrollbar_thickness_);
                    if (content.w > 0.f && content.h > 0.f) {
                        hit = content;
                    }
                }
                bool touches =
                    overlay_only ? false : (!record.has_damage_clip || damage_rects.empty());
                if (!touches) {
                    for (const html::PaintRect& d : damage_rects) {
                        if (d.x < hit.x + hit.w && hit.x < d.x + d.w && d.y < hit.y + hit.h &&
                            hit.y < d.y + d.h) {
                            touches = true;
                            break;
                        }
                    }
                }
                if (!touches) {
                    ++it;
                    continue;
                }
                // docs/PERF_REFACTOR.md capability E: when the damage only reaches part of the
                // listing's tiles, the listing is kept and the tiles it lost are rebuilt by a
                // narrow walk before the replay. Anything else keeps the wholesale drop.
                std::vector<Rect> tiles;
                if (damage_tiles(root, *it->first, record, damage_rects, tiles)) {
                    tile_rebuild_[it->first] = std::move(tiles);
                    ++it;
                    continue;
                }
                damaged_items += record.list.items().size();
                it = records_.erase(it);
                ++damaged;
            }
        }
        if (tracker != nullptr) {
            tracker->clear();
        }
    }
    retained_ = RetainedStats{};
    retained_.damaged = damaged;
    retained_.damaged_items = damaged_items;
    frame_w_ = viewport_w;
    frame_h_ = viewport_h;
    scopes_.clear();
    canvas_bg_node_ = nullptr;
    if (viewport_w > 0 && viewport_h > 0) {
        const Rect vp{0, 0, viewport_w, viewport_h};
        out.push_clip(vp);
        // CSS Backgrounds 3 §2.11: root background (else body) paints the canvas. Colour covers
        // the viewport; images are positioned against the donor's own background positioning area
        // but the painting area is the whole canvas. The donor then skips its own background.
        const layout::Box* donor = &root;
        {
            auto has_image = [](const layout::Box& box) {
                const std::string* image = computed(box, "background-image");
                if (image == nullptr || image->empty() || lower_ascii(*image) == "none") {
                    return false;
                }
                if (has_top_level_comma(*image)) {
                    return true;
                }
                return is_paintable_gradient_image(*image) || is_paintable_bg_url(*image);
            };
            if (color_is_transparent(used_background(root)) && !has_image(root)) {
                for (const layout::Box* child : root.children) {
                    if (child != nullptr && child->node != nullptr && child->node->tag == "body") {
                        donor = child;
                        break;
                    }
                }
            }
        }
        if (donor != nullptr && donor->node != nullptr) {
            canvas_bg_node_ = donor->node;
            const Color canvas = used_background(*donor);
            // Propagated values are treated as if specified on the root; positioning area is the
            // root border box, painting area is the whole canvas (Backgrounds §2.11). Each image
            // layer is painted, bottom first. `fixed` positions against the viewport.
            std::vector<BgPaintLayer> canvas_layers = background_layers(
                computed(*donor, "background-image"), computed(*donor, "background-position"),
                computed(*donor, "background-size"), computed(*donor, "background-repeat"),
                computed(*donor, "background-attachment"), computed(*donor, "background-clip"),
                computed(*donor, "background-origin"));
            bool canvas_paintable = false;
            for (const BgPaintLayer& layer : canvas_layers) {
                if (is_paintable_gradient_image(layer.image) || is_paintable_bg_url(layer.image)) {
                    canvas_paintable = true;
                    break;
                }
            }
            if (canvas_paintable) {
                apply_background_blend(canvas_layers, computed(*donor, "background-blend-mode"));
            }
            const bool blend_open =
                canvas_paintable && background_has_blend(canvas_layers) &&
                begin_background_isolation(out, vp);
            if (!color_is_transparent(canvas)) {
                out.fill_rect(vp, canvas, 0.f);
            }
            const Rect root_origin = border_rect(root, 0.f, 0.f);
            for (size_t i = canvas_layers.size(); i > 0; --i) {
                const BgPaintLayer& layer = canvas_layers[i - 1];
                const bool fixed = lower_ascii(layer.attachment) == "fixed";
                const Rect origin = fixed ? vp : root_origin;
                const bool layer_blend = blend_open && layer.blend != MixBlendMode::Normal;
                if (layer_blend) {
                    FilterEffects fx;
                    fx.blend = layer.blend;
                    out.begin_filter(vp, fx);
                }
                paint_one_background_image(out, layer.image, layer.position, layer.size,
                                           layer.repeat, origin, vp, CornerRadii{}, 1.f,
                                           resource_base_, donor);
                if (layer_blend) {
                    out.end_filter();
                }
            }
            if (blend_open) {
                out.end_filter();
            }
        }
        // docs/PERF_REFACTOR.md capability E: the listings that lost only part of their tiles are
        // rebuilt here, before the walk that replays them. The canvas neighbour is already settled
        // (`canvas_bg_node_`), so the narrow walk hands the donor the same skip the recording walk
        // did. A rebuild that cannot line up drops the listing, and the walk records it plainly.
        for (const auto& entry : tile_rebuild_) {
            const auto record = records_.find(entry.first);
            if (record == records_.end()) {
                continue;
            }
            if (!rebuild_record_tiles(*entry.first, record->second, entry.second)) {
                records_.erase(record);
            }
        }
        tile_rebuild_.clear();
        // Modal ::backdrop fills sit under the dialog (and over ordinary content) as a
        // top-layer stand-in until a real top layer exists.
        paint_modal_backdrops(root, out, vp);
        paint_box(root, out, vp, true, 1.0f);
        // capability B: the members of the top layer paint after the document walk, not inside it.
        paint_top_layer(root, out, vp, true);
        paint_viewport_scrollbars(root, out, viewport_w, viewport_h);
        out.pop_clip();
    } else {
        paint_box(root, out, Rect{}, false, 1.0f);
        paint_top_layer(root, out, Rect{}, false);
    }
    canvas_bg_node_ = nullptr;
    retained_.records = records_.size();
    for (const auto& entry : records_) {
        retained_.items += entry.second.list.items().size();
    }
    // docs/PERF_REFACTOR.md stage 0: the retained-listing units the gates read, on the same window
    // as the rest of the frame.
    perf::note_paint_retained(retained_.records, retained_.reused, retained_.damaged,
                              retained_.damaged_items, retained_.items);
}

// The viewport is a scroll container the page never declares: its bars sit in the gutter the layout
// reserved outside the initial containing block (the root box is already narrowed by it), so they
// are drawn from the viewport rectangle rather than from the root box's own edges.
void Painter::paint_viewport_scrollbars(const layout::Box& root, DisplayList& out, float viewport_w,
                                        float viewport_h) const {
    const layout::ScrollbarV vertical = layout::viewport_vertical_scrollbar(
        root, viewport_w, viewport_h, scrollbar_thickness_);
    const layout::ScrollbarH horizontal = layout::viewport_horizontal_scrollbar(
        root, viewport_w, viewport_h, scrollbar_thickness_);
    if (!vertical.visible && !horizontal.visible) {
        return;
    }
    Color thumb;
    Color track;
    parse_scrollbar_colors(root, thumb, track);
    if (vertical.visible) {
        out.fill_rect(Rect{vertical.track_x, vertical.track_y, vertical.track_w, vertical.track_h},
                      track, 0, PaintSource::Scrollbar);
        out.fill_rect(Rect{vertical.track_x + kThumbInset, vertical.thumb_y,
                           vertical.track_w - 2.f * kThumbInset, vertical.thumb_h},
                      thumb, 0, PaintSource::Scrollbar);
    }
    if (horizontal.visible) {
        out.fill_rect(
            Rect{horizontal.track_x, horizontal.track_y, horizontal.track_w, horizontal.track_h},
            track, 0, PaintSource::Scrollbar);
        out.fill_rect(Rect{horizontal.thumb_x, horizontal.track_y + kThumbInset, horizontal.thumb_w,
                           horizontal.track_h - 2.f * kThumbInset},
                      thumb, 0, PaintSource::Scrollbar);
    }
}

}  // namespace paint
}  // namespace zui
