#include "paint/painter_internal.h"

#include "paint/display_list.h"

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

/// Emit one bitmap layer into `clip_area` (already the background painting area). `image_rect` is
/// the positioned/sized tile origin from `resolve_bg_image_rect`. `repeat` tiles inside the clip;
/// `space` / `round` fall back to `repeat` (same documented subset as the gradient path).
void emit_bg_bitmap(DisplayList& out, const std::string& path, Rect image_rect, Rect clip_area,
                    CornerRadii clip_radii, const std::string& repeat_l) {
    if (path.empty() || image_rect.w <= 0.f || image_rect.h <= 0.f || clip_area.w <= 0.f ||
        clip_area.h <= 0.f) {
        return;
    }
    const bool no_repeat = repeat_l == "no-repeat";
    const bool repeat_x_only = repeat_l == "repeat-x";
    const bool repeat_y_only = repeat_l == "repeat-y";
    out.push_clip(clip_area, clip_radii);
    auto draw_one = [&](float x, float y) {
        out.draw_image(Rect{x, y, image_rect.w, image_rect.h}, path);
    };
    if (no_repeat) {
        draw_one(image_rect.x, image_rect.y);
        out.pop_clip();
        return;
    }
    auto tile_start = [](float origin, float size, float clip0) {
        if (size <= 0.f) {
            return origin;
        }
        float x = origin;
        while (x > clip0) {
            x -= size;
        }
        while (x + size <= clip0) {
            x += size;
        }
        return x;
    };
    const float x0 =
        (repeat_y_only) ? image_rect.x : tile_start(image_rect.x, image_rect.w, clip_area.x);
    const float y0 =
        (repeat_x_only) ? image_rect.y : tile_start(image_rect.y, image_rect.h, clip_area.y);
    const float x1 = clip_area.x + clip_area.w;
    const float y1 = clip_area.y + clip_area.h;
    if (repeat_x_only) {
        for (float x = x0; x < x1; x += image_rect.w) {
            draw_one(x, image_rect.y);
        }
    } else if (repeat_y_only) {
        for (float y = y0; y < y1; y += image_rect.h) {
            draw_one(image_rect.x, y);
        }
    } else {
        for (float y = y0; y < y1; y += image_rect.h) {
            for (float x = x0; x < x1; x += image_rect.w) {
                draw_one(x, y);
            }
        }
    }
    out.pop_clip();
}

namespace painter_detail {

bool top_layer_member(const layout::Box& box) {
    // A `BoxRole::Text` box copies its element's node (layout/box_tree/builder.cpp) but paints as
    // part of that element, so only an element box can be a member.
    return box.role != layout::BoxRole::Text && box.node != nullptr && html::in_top_layer(box.node);
}

}  // namespace painter_detail

void Painter::paint_box_at(const layout::Box& box, DisplayList& out, Rect clip, bool has_clip,
                           float opacity, float dx, float dy) const {
    // docs/PERF_REFACTOR.md capability E: every item this box appends carries it as its owner, so
    // a narrow rebuild can replace exactly the items of the boxes it visited. Children set their
    // own and restore this one on the way out, which makes the innermost box win.
    struct OwnerGuard {
        DisplayList* list = nullptr;
        const layout::Box* previous = nullptr;
        ~OwnerGuard() {
            if (list != nullptr) {
                list->set_owner(previous);
            }
        }
    } owner_guard{&out, out.owner()};
    out.set_owner(&box);
    // Section 9 step D: while a listing is being written the walk covers the retained margin, so a
    // box that only the margin can see is still visited. It keeps its geometry for exactly this
    // reason; the flag still says it is not painted by an ordinary frame.
    const bool recording = !scopes_.empty();
    if (display_none(box)) {
        return;
    }
    // LAYOUT.md section 9 step B: a culled subtree is off the visible rectangle. Stop even while a
    // retained listing is open: descendants of a parked scroll may still hold the previous origin,
    // and the lag that corrects them is not a paint coordinate.
    if (box.geo.culled) {
        return;
    }
    const bool paint_self = !visibility_hidden(box);
    if (recording) {
        if (box.style_ptr != nullptr &&
            (box.style().position_sticky || box.style().position_fixed)) {
            scopes_.back().tainted = true;
        }
        if (scroll_container(box)) {
            // Every open region contains this container's content, so every one of them stops being
            // replayable when its offsets move - including the regions above this container's own
            // listing, which hold the items that listing was materialised into.
            const Record::Mark mark{box.node, &box, box.node->scroll_x, box.node->scroll_y};
            for (Scope& scope : scopes_) {
                scope.inside.push_back(mark);
            }
        }
    }
    Rect visit = clip;
    if (recording) {
        visit =
            Rect{clip.x - layout::kRetainedMargin, clip.y - layout::kRetainedMargin,
                 clip.w + 2.f * layout::kRetainedMargin, clip.h + 2.f * layout::kRetainedMargin};
    }
    if (has_clip) {
        float shadow_left = 0.f;
        float shadow_top = 0.f;
        float shadow_right = 0.f;
        float shadow_bottom = 0.f;
        if (const std::string* shadow_css = computed(box, "box-shadow")) {
            outer_shadow_outset(*shadow_css, box, shadow_left, shadow_top, shadow_right,
                                shadow_bottom);
        }
        const float x = box.geo.world_x - shadow_left;
        const float y = box.geo.world_y - shadow_top;
        const float w = box.geo.width + shadow_left + shadow_right;
        const float h = box.geo.height + shadow_top + shadow_bottom;
        if (x + w < visit.x || y + h < visit.y || x > visit.x + visit.w || y > visit.y + visit.h) {
            return;
        }
    }

    // One walk of this node's computed map. Later `computed(box, "…")` reads a slot. A child
    // box installs its own guard and restores this one.
    const ::zui::layout::style_literal_detail::SlotGuard literal_slots(box.node);

    const bool clip_overflow = layout::clips_overflow(box);
    const Rect br = border_rect(box, dx, dy);
    const CornerRadii radii = resolve_border_radii(box.style(), br.w, br.h);

    // CSS Masking 1: `clip-path` clips the entire element (border box reference by default).
    struct ClipPathGuard {
        DisplayList* list = nullptr;
        bool active = false;
        ~ClipPathGuard() {
            if (active && list != nullptr) {
                list->pop_clip();
            }
        }
    } clip_path_guard;
    // `ClipPathShape` and the polygon buffer live in the lambda. `/RTC1` poisons every local of
    // this function on entry, and almost no box has a clip-path.
    if (const std::string* cp = computed(box, "clip-path")) {
        const bool reject = [&]() -> bool {
            layout::ClipPathShape clip_path;
            if (!layout::parse_clip_path(*cp, br.w, br.h, clip_path)) {
                return false;
            }
            if (clip_path.kind == layout::ClipPathShape::Kind::Inset) {
                const float x0 = br.x + clip_path.inset_l;
                const float y0 = br.y + clip_path.inset_t;
                const float x1 = br.x + br.w - clip_path.inset_r;
                const float y1 = br.y + br.h - clip_path.inset_b;
                const float iw = std::max(0.f, x1 - x0);
                const float ih = std::max(0.f, y1 - y0);
                if (iw <= 0.f || ih <= 0.f) {
                    return true;
                }
                out.push_clip(Rect{x0, y0, iw, ih},
                              clip_path.inset_round > 0.5f
                                  ? CornerRadii::uniform(clip_path.inset_round)
                                  : CornerRadii{});
                clip_path_guard.list = &out;
                clip_path_guard.active = true;
            } else if (clip_path.kind == layout::ClipPathShape::Kind::Circle ||
                       clip_path.kind == layout::ClipPathShape::Kind::Ellipse) {
                const float x0 = br.x + clip_path.cx - clip_path.rx;
                const float y0 = br.y + clip_path.cy - clip_path.ry;
                const CornerRadii er{clip_path.rx, clip_path.rx, clip_path.rx, clip_path.rx,
                                     clip_path.ry, clip_path.ry, clip_path.ry, clip_path.ry};
                out.push_clip(Rect{x0, y0, clip_path.rx * 2.f, clip_path.ry * 2.f}, er);
                clip_path_guard.list = &out;
                clip_path_guard.active = true;
            } else if (clip_path.kind == layout::ClipPathShape::Kind::Polygon &&
                       clip_path.points.size() >= 3) {
                // One polygon clip. Vertices are border-box local; the scissor is their AABB.
                // Paint uploads at most 16 of them. A zero-area bound clips the element away.
                float min_x = clip_path.points[0].first;
                float min_y = clip_path.points[0].second;
                float max_x = min_x;
                float max_y = min_y;
                std::vector<float> pts;
                pts.reserve(clip_path.points.size() * 2);
                for (const auto& p : clip_path.points) {
                    min_x = std::min(min_x, p.first);
                    min_y = std::min(min_y, p.second);
                    max_x = std::max(max_x, p.first);
                    max_y = std::max(max_y, p.second);
                    pts.push_back(br.x + p.first);
                    pts.push_back(br.y + p.second);
                }
                const float pw = max_x - min_x;
                const float ph = max_y - min_y;
                if (pw <= 0.f || ph <= 0.f) {
                    return true;
                }
                out.push_clip_polygon(Rect{br.x + min_x, br.y + min_y, pw, ph}, std::move(pts),
                                      clip_path.evenodd);
                clip_path_guard.list = &out;
                clip_path_guard.active = true;
            }
            return false;
        }();
        if (reject) {
            return;
        }
    }
    // CSS Filter Effects 2: the backdrop is whatever has already been painted behind this border
    // box. Emit the sample before the element's own filter group, which redirects later items
    // into a cleared layer and would hide that backdrop. A transformed box's border box is not
    // the axis-aligned `br` the sampler clips to, so those keep an unfiltered backdrop.
    Color current{0, 0, 0, 255};
    if (const std::string* cv = computed(box, "color")) {
        current = paint_color(box, cv);
    }
    if (!box.paint_transformed) {
        if (const std::string* bv = computed(box, "backdrop-filter")) {
            // `FilterEffects` stays in the lambda so a box without a backdrop does not poison it.
            [&]() {
                FilterEffects backdrop;
                if (parse_css_filter(*bv, backdrop, current)) {
                    out.backdrop_filter(br, radii, std::move(backdrop));
                }
            }();
        }
    }
    // CSS filter / mask-image / mix-blend-mode / opacity: isolate the element's painted output.
    // `opacity < 1` must composite the element (and descendants) as one group, then apply the
    // factor — multiplying each primitive's alpha is wrong when descendants overlap.
    struct FilterGuard {
        DisplayList* list = nullptr;
        bool active = false;
        ~FilterGuard() {
            if (active && list != nullptr) {
                list->end_filter();
            }
        }
    } filter_guard;
    const float self_opacity = node_opacity(box);
    const std::string* filter_css = computed(box, "filter");
    const std::string* mask_css = computed(box, "mask-image");
    const std::string* blend_css = computed(box, "mix-blend-mode");
    if (self_opacity < 0.999f || filter_css != nullptr || mask_css != nullptr ||
        blend_css != nullptr) {
        const bool began = [&]() -> bool {
            FilterEffects fx;
            if (filter_css != nullptr) {
                FilterEffects parsed;
                if (parse_css_filter(*filter_css, parsed, current)) {
                    fx.blur = parsed.blur;
                    fx.brightness = parsed.brightness;
                    fx.opacity = parsed.opacity;
                    fx.shadows = std::move(parsed.shadows);
                    if (parsed.has_color_matrix) {
                        fx.has_color_matrix = true;
                        fx.color_matrix_count = parsed.color_matrix_count;
                        fx.color_matrix = std::move(parsed.color_matrix);
                    }
                }
            }
            if (mask_css != nullptr) {
                const std::string& m = *mask_css;
                if (!m.empty() && lower_ascii(m) != "none") {
                    Gradient mg;
                    if (gradient_image_for_box(m, br, mg)) {
                        fx.has_mask = true;
                        fx.mask_is_gradient = true;
                        fx.mask_gradient = std::move(mg);
                    } else {
                        std::string url_path;
                        if (parse_bg_url(m, url_path)) {
                            fx.has_mask = true;
                            fx.mask_is_gradient = false;
                            fx.mask_path = resolve_paint_resource(url_path, resource_base_);
                        }
                    }
                }
            }
            if (blend_css != nullptr) {
                const std::string b = lower_ascii(*blend_css);
                if (b == "multiply") {
                    fx.blend = MixBlendMode::Multiply;
                } else if (b == "screen") {
                    fx.blend = MixBlendMode::Screen;
                } else if (b == "overlay") {
                    fx.blend = MixBlendMode::Overlay;
                } else if (b == "darken") {
                    fx.blend = MixBlendMode::Darken;
                } else if (b == "lighten") {
                    fx.blend = MixBlendMode::Lighten;
                } else if (b == "color-dodge") {
                    fx.blend = MixBlendMode::ColorDodge;
                } else if (b == "color-burn") {
                    fx.blend = MixBlendMode::ColorBurn;
                } else if (b == "hard-light") {
                    fx.blend = MixBlendMode::HardLight;
                } else if (b == "soft-light") {
                    fx.blend = MixBlendMode::SoftLight;
                } else if (b == "difference") {
                    fx.blend = MixBlendMode::Difference;
                } else if (b == "exclusion") {
                    fx.blend = MixBlendMode::Exclusion;
                }
            }
            if (self_opacity < 0.999f) {
                fx.opacity *= std::max(0.f, std::min(1.f, self_opacity));
            }
            if (!fx.any()) {
                return false;
            }
            // Three standard deviations covers the Gaussian that `blur()` and a drop-shadow's
            // radius paint outside the border box. The offscreen target is the framebuffer, so
            // this pad only has to keep a CPU consumer from culling the fringe.
            float pad = fx.blur * 3.f + 2.f;
            for (const DropShadow& shadow : fx.shadows) {
                const float reach =
                    std::max(std::abs(shadow.dx), std::abs(shadow.dy)) + shadow.blur * 3.f;
                pad = std::max(pad, reach + 2.f);
            }
            out.begin_filter(Rect{br.x - pad, br.y - pad, br.w + 2.f * pad, br.h + 2.f * pad},
                             std::move(fx));
            return true;
        }();
        if (began) {
            filter_guard.list = &out;
            filter_guard.active = true;
        }
    }
    const float radius = radii.max_component();
    // Ancestor opacity rides prior BeginFilter groups; this box's opacity is in the group above.
    const float op = opacity;
    paint_box_decorations(box, out, br, radii, radius, op, dx, dy, paint_self);

    if (clip_overflow) {
        push_overflow_clip(out, box, dx, dy, scrollbar_thickness_);
    }

    // What the content (the box's own runs and every child) is painted through. Section 9 step D's
    // listing is replayed inside exactly this rectangle.
    Rect child_clip = clip;
    bool child_has_clip = has_clip;
    if (clip_overflow) {
        const Rect pad = scrollport_rect(box, dx, dy, scrollbar_thickness_);
        child_clip = has_clip ? intersect_rect(clip, pad) : pad;
        child_has_clip = true;
    }
    // The rectangle a listing is emitted through. At the top of a frame that is this container's own
    // clip - the frame paints what it can see and nothing else. Inside another listing it is the
    // *widened* clip instead: the listing above this one has to hold the whole retained margin, and
    // the clip operations the items sit inside already keep them off the screen.
    Rect listing_clip = child_clip;
    bool listing_has_clip = child_has_clip;
    if (recording) {
        listing_clip =
            Rect{child_clip.x - layout::kRetainedMargin, child_clip.y - layout::kRetainedMargin,
                 child_clip.w + 2.f * layout::kRetainedMargin,
                 child_clip.h + 2.f * layout::kRetainedMargin};
    }

    // ---- the retained region (LAYOUT.md section 9 step D) -------------------------------------
    // A scroll container's content is the thing a scroll frame pays for. Keeping it as a listing
    // turns that frame into a shift of what the last one drew, and only a frame where something
    // actually moved (`html::paint_stamp`) or a scroll past the retained margin pays for the walk.
    // Only a frame that paints a real window can keep a listing: the walk below reaches the
    // retained margin, and the geometry inside that window is exactly what the last mapping wrote.
    // A paint with no viewport - the painter's trace path - has no such window and stays a plain
    // walk.
    const layout::Rect mapped = layout::paint_viewport();
    const auto known = records_.find(&box);
    const bool refused = known != records_.end() && !known->second.usable;
    if (refused) {
        // A listing was tried here and the content turned out to be anchored to something other
        // than the scroll. Recording again would pay for the wider window on every frame.
        ++retained_.refused;
    }
    // The box a narrow rebuild re-walks is replaying, not recording: the caller is about to put
    // the rebuilt listing in its place.
    const bool rebuilding = rebuilding_box_ == &box;
    // A listing is replayable whatever this frame did about the world: it is what the last recorded
    // walk painted, and `replay_record` re-checks the window and the offsets it was recorded under.
    // Only *recording* needs a mapping - that walk reads `geo.world_*`, which is only current for
    // the window the last mapping covered.
    const bool recordable_here =
        !rebuilding && retained_on_ && has_clip && frame_w_ > 0.f && !refused && recordable(box);
    const bool record_here =
        recordable_here && mapped.w == frame_w_ && mapped.h == frame_h_;
    bool replayed = false;
    if (recordable_here) {
        replayed = replay_record(box, out, listing_clip, listing_has_clip);
        if (!replayed && record_here) {
            if (narrow_rects_ != nullptr) {
                // A nested listing that cannot replay would have to be *recorded* from the narrow
                // walk, and that partial capture would replace the whole listing. Give up: the
                // caller drops this listing and the ordinary walk records it in full.
                narrow_failed_ = true;
                return;
            }
            scopes_.push_back(Scope{});
        }
    }
    const std::size_t region_start = out.items().size();
    if (!replayed) {
        paint_box_contents(box, out, child_clip, child_has_clip, op, dx, dy, paint_self);
    }  // ---- end of the retained region ---------------------------------------------------
    if (record_here && !replayed) {
        // The scope opened above encloses exactly this container's content, so it is what the
        // listing records: the offsets inside it that cannot move under the listing, and whether
        // something in it is anchored to something other than the scroll.
        const Scope scope = std::move(scopes_.back());
        scopes_.pop_back();
        capture_record(box, out, region_start, out.items().size(), listing_clip, listing_has_clip,
                       clip, has_clip, scope);
    }

    // Pop before the bars. The content clip stops at the client edge, and the bar sits in
    // the gutter just outside that edge.
    if (clip_overflow) {
        out.pop_clip();
    }

    // The bars of the box a narrow rebuild is re-walking belong to the frame, not to the listing
    // (`capture_record` runs before them), so the rebuild must not emit them: they would have no
    // counterpart in the listing to replace. A nested container's bars *are* part of the listing
    // above it, and the walk below emits them with the same owners the recording walk did.
    if (rebuilding_box_ == &box) {
        return;
    }
    paint_scrollbars(box, out);
}

void Painter::paint_scrollbars(const layout::Box& box, DisplayList& out) const {
    Color thumb;
    Color track;
    parse_scrollbar_colors(box, thumb, track);
    // Pixels whose centers lie in the layout rect. A fractional track (a line box that is not
    // a whole number of pixels tall) otherwise blends the first and last rows, so content keeps
    // the content bit inside the gutter and a halo falls just outside it.
    auto on_pixels = [](Rect r) {
        if (r.w <= 0.f || r.h <= 0.f) {
            return r;
        }
        const float x1 = r.x + r.w;
        const float y1 = r.y + r.h;
        const float left = std::ceil(r.x - 0.5f);
        const float top = std::ceil(r.y - 0.5f);
        const float right = std::ceil(x1 - 0.5f);
        const float bottom = std::ceil(y1 - 0.5f);
        return Rect{left, top, std::max(0.f, right - left), std::max(0.f, bottom - top)};
    };
    auto fill_bar = [&](Rect r, Color color) {
        r = on_pixels(r);
        if (r.w >= 1.f && r.h >= 1.f) {
            out.fill_rect(r, color, 0, PaintSource::Scrollbar);
        }
    };
    {
        layout::ScrollbarV sb = layout::vertical_scrollbar(box, scrollbar_thickness_);
        if (sb.visible) {
            fill_bar(Rect{sb.track_x, sb.track_y, sb.track_w, sb.track_h}, track);
            fill_bar(Rect{sb.track_x + kThumbInset, sb.thumb_y, sb.track_w - 2.f * kThumbInset,
                          sb.thumb_h},
                     thumb);
        }
    }
    {
        layout::ScrollbarH sb = layout::horizontal_scrollbar(box, scrollbar_thickness_);
        if (sb.visible) {
            fill_bar(Rect{sb.track_x, sb.track_y, sb.track_w, sb.track_h}, track);
            fill_bar(Rect{sb.thumb_x, sb.track_y + kThumbInset, sb.thumb_w,
                          sb.track_h - 2.f * kThumbInset},
                     thumb);
        }
    }
}

void Painter::paint_box(const layout::Box& box, DisplayList& out, Rect clip, bool has_clip,
                        float opacity) const {
    paint_box_shifted(box, out, clip, has_clip, opacity, 0.f, 0.f);
}

namespace {

void collect_top_layer(const layout::Box& box, std::vector<const layout::Box*>& out) {
    if (top_layer_member(box)) {
        // The member paints under the frame's clip rather than wherever the document walk would
        // have clipped it, but the walk still descends: a member nested in another member is a
        // participant in its own right and the outer paint skips it (see `paint_box_at`).
        out.push_back(&box);
    }
    for (const layout::Box* child : box.children) {
        if (child != nullptr) {
            collect_top_layer(*child, out);
        }
    }
}

}  // namespace

void Painter::paint_top_layer(const layout::Box& root, DisplayList& out, Rect clip,
                              bool has_clip) const {
    std::vector<const layout::Box*> members;
    collect_top_layer(root, members);
    for (const layout::Box* member : members) {
        // The member is anchored to the viewport rather than to whichever box its DOM parent is,
        // so the frame's own clip is the one it paints through. It keeps the frame's opacity: a
        // top-layer member is not inside any ancestor's `opacity` group.
        paint_box_shifted(*member, out, clip, has_clip, 1.0f, 0.f, 0.f);
    }
}

}  // namespace paint
}  // namespace zui
