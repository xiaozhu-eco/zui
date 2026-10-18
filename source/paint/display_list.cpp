#include "paint/display_list.h"

#include "html/document.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace zui {
namespace paint {

namespace {
struct ItemBuildDiag {
    double build_ms = 0;
    double copy_ms = 0;
    std::uint64_t build_n = 0;
    std::uint64_t copy_n = 0;
};
ItemBuildDiag g_item_build;
}  // namespace

void take_item_build_diag(double& build_ms, std::uint64_t& build_n, double& copy_ms,
                          std::uint64_t& copy_n) {
    build_ms = g_item_build.build_ms;
    build_n = g_item_build.build_n;
    copy_ms = g_item_build.copy_ms;
    copy_n = g_item_build.copy_n;
    g_item_build = {};
}

// Construct the item in the caller. A lambda here is a separate function under /Ob0.
#define ZUI_EMIT_ITEM(...)                                                                         \
    do {                                                                                           \
        DisplayItem it;                                                                            \
        __VA_ARGS__                                                                                \
        push_item(std::move(it));                                                                  \
        ++g_item_build.build_n;                                                                    \
    } while (0)

void DisplayList::push_clip(Rect r, float radius) { push_clip(r, CornerRadii::uniform(radius)); }

void DisplayList::push_clip_xform(Rect r, CornerRadii radii, float a, float b, float c0, float d,
                                  float e, float f) {
    ZUI_EMIT_ITEM(it.op = DisplayOp::PushClip; it.rect = r; it.radii = radii;
                 it.radius = radii.max_component(); it.has_xform = true; it.xform_a = a;
                 it.xform_b = b; it.xform_c = c0; it.xform_d = d; it.xform_e = e; it.xform_f = f;);
}

void DisplayList::push_clip(Rect r, CornerRadii radii) {
    ZUI_EMIT_ITEM(it.op = DisplayOp::PushClip; it.rect = r; it.radii = radii;
                 it.radius = radii.max_component(););
}

void DisplayList::push_clip_polygon(Rect r, std::vector<float> pts, bool evenodd) {
    ZUI_EMIT_ITEM(it.op = DisplayOp::PushClip; it.rect = r; it.poly = std::move(pts);
                 it.poly_evenodd = evenodd;);
}

void DisplayList::pop_clip() {
    ZUI_EMIT_ITEM(it.op = DisplayOp::PopClip;);
}

void DisplayList::fill_rect(Rect r, Color c, float radius, PaintSource source) {
    fill_rect(r, c, CornerRadii::uniform(radius), source);
}

void DisplayList::fill_rect(Rect r, Color c, CornerRadii radii, PaintSource source) {
    if (color_is_transparent(c) || r.w <= 0 || r.h <= 0) {
        return;
    }
    ZUI_EMIT_ITEM(it.op = DisplayOp::FillRect; it.rect = r; it.color = c; it.radii = radii;
                 it.radius = radii.max_component(); it.source = source;);
}

void DisplayList::fill_rect_xform(Rect r, Color c, CornerRadii radii, float a, float b, float c0,
                                  float d, float e, float f, PaintSource source) {
    if (color_is_transparent(c) || r.w <= 0 || r.h <= 0) {
        return;
    }
    ZUI_EMIT_ITEM(it.op = DisplayOp::FillRect; it.rect = r; it.color = c; it.radii = radii;
                 it.radius = radii.max_component(); it.source = source; it.has_xform = true;
                 it.xform_a = a; it.xform_b = b; it.xform_c = c0; it.xform_d = d; it.xform_e = e;
                 it.xform_f = f;);
}

void DisplayList::fill_gradient(Rect r, const Gradient& gradient, float radius,
                                PaintSource source) {
    fill_gradient(r, gradient, CornerRadii::uniform(radius), source);
}

void DisplayList::fill_gradient(Rect r, const Gradient& gradient, CornerRadii radii,
                                PaintSource source) {
    if (r.w <= 0 || r.h <= 0 || gradient.stops.empty()) {
        return;
    }
    ZUI_EMIT_ITEM(it.op = DisplayOp::FillGradient; it.rect = r; it.radii = radii;
                 it.radius = radii.max_component(); it.source = source; it.gradient = gradient;);
}

void DisplayList::stroke_rect(Rect r, Color c, float width, float radius) {
    stroke_rect(r, c, width, CornerRadii::uniform(radius));
}

void DisplayList::stroke_rect(Rect r, Color c, float width, CornerRadii radii) {
    if (color_is_transparent(c) || width <= 0 || r.w <= 0 || r.h <= 0) {
        return;
    }
    ZUI_EMIT_ITEM(it.op = DisplayOp::StrokeRect; it.rect = r; it.color = c; it.stroke_width = width;
                 it.radii = radii; it.radius = radii.max_component(););
}

void DisplayList::stroke_ring(Rect outer, CornerRadii outer_radii, Rect hole,
                              CornerRadii hole_radii, Color c) {
    if (color_is_transparent(c) || outer.w <= 0.f || outer.h <= 0.f) {
        return;
    }
    ZUI_EMIT_ITEM(it.op = DisplayOp::StrokeRect; it.rect = outer; it.color = c;
                 // stroke_width 0 + non-empty hole → asymmetric ring (see soft-raster StrokeRect).
                 it.stroke_width = 0.f; it.radii = outer_radii; it.radius = outer_radii.max_component();
                 it.hole = hole; it.hole_radii = hole_radii;);
}

void DisplayList::draw_text(float x, float y, Color c, std::string_view utf8, float font_px,
                            std::string_view family, int font_weight, const html::Node* debug_owner,
                            float letter_spacing, bool font_italic, bool font_kerning_none,
                            float blur, TextGlyphOrient orient,
                            const std::vector<float>* glyph_advances,
                            const std::vector<std::uint32_t>* glyph_ids) {
    if (utf8.empty() || color_is_transparent(c)) {
        return;
    }
    ZUI_EMIT_ITEM(it.op = DisplayOp::DrawText; it.rect = Rect{x, y, 0, 0}; it.color = c;
                 it.text.assign(utf8.begin(), utf8.end());
                 it.font_family.assign(family.begin(), family.end()); it.font_px = font_px;
                 it.font_weight = font_weight; it.font_italic = font_italic;
                 it.letter_spacing = letter_spacing; it.font_kerning_none = font_kerning_none;
                 it.text_orient = orient; it.blur = std::max(0.f, blur); it.debug_owner = debug_owner;
                 if (glyph_advances != nullptr) { it.glyph_advances = *glyph_advances; } if (
                     glyph_ids != nullptr) { it.glyph_ids = *glyph_ids; });
}

void DisplayList::draw_text_mask(float x, float y, std::string_view utf8, float font_px,
                                 std::string_view family, int font_weight,
                                 const html::Node* debug_owner, float letter_spacing,
                                 bool font_italic, bool font_kerning_none,
                                 const std::vector<float>* glyph_advances,
                                 const std::vector<std::uint32_t>* glyph_ids) {
    if (utf8.empty()) {
        return;
    }
    ZUI_EMIT_ITEM(it.op = DisplayOp::DrawTextMask; it.rect = Rect{x, y, 0, 0};
                 it.color = Color{255, 255, 255, 255}; it.text.assign(utf8.begin(), utf8.end());
                 it.font_family.assign(family.begin(), family.end()); it.font_px = font_px;
                 it.font_weight = font_weight; it.font_italic = font_italic;
                 it.letter_spacing = letter_spacing; it.font_kerning_none = font_kerning_none;
                 it.debug_owner = debug_owner; if (glyph_advances != nullptr) {
                     it.glyph_advances = *glyph_advances;
                 } if (glyph_ids != nullptr) { it.glyph_ids = *glyph_ids; });
}

void DisplayList::draw_text_xform(float x, float y, Color c, std::string_view utf8, float font_px,
                                  std::string_view family, float a, float b, float c0, float d,
                                  float e, float f, int font_weight, const html::Node* debug_owner,
                                  float letter_spacing, bool font_italic, bool font_kerning_none,
                                  float blur, TextGlyphOrient orient,
                                  const std::vector<float>* glyph_advances,
                                  const std::vector<std::uint32_t>* glyph_ids) {
    if (utf8.empty() || color_is_transparent(c)) {
        return;
    }
    ZUI_EMIT_ITEM(it.op = DisplayOp::DrawText; it.rect = Rect{x, y, 0, 0}; it.color = c;
                 it.text.assign(utf8.begin(), utf8.end());
                 it.font_family.assign(family.begin(), family.end()); it.font_px = font_px;
                 it.font_weight = font_weight; it.font_italic = font_italic;
                 it.letter_spacing = letter_spacing; it.font_kerning_none = font_kerning_none;
                 it.text_orient = orient; it.blur = std::max(0.f, blur); it.debug_owner = debug_owner;
                 it.has_xform = true; it.xform_a = a; it.xform_b = b; it.xform_c = c0; it.xform_d = d;
                 it.xform_e = e; it.xform_f = f; if (glyph_advances != nullptr) {
                     it.glyph_advances = *glyph_advances;
                 } if (glyph_ids != nullptr) { it.glyph_ids = *glyph_ids; });
}

void DisplayList::draw_image(Rect r, std::string_view path) {
    draw_image(r, path, 0.f, 0.f, 1.f, 1.f, Color{255, 255, 255, 255});
}

void DisplayList::draw_image(Rect r, std::string_view path, float u0, float v0, float u1, float v1,
                             Color tint) {
    if (path.empty() || r.w <= 0 || r.h <= 0) {
        return;
    }
    ZUI_EMIT_ITEM(it.op = DisplayOp::DrawImage; it.rect = r; it.color = tint; it.src_u0 = u0;
                 it.src_v0 = v0; it.src_u1 = u1; it.src_v1 = v1;
                 it.text.assign(path.begin(), path.end()););
}

void DisplayList::draw_image_xform(Rect r, std::string_view path, float a, float b, float c0,
                                   float d, float e, float f) {
    if (path.empty() || r.w <= 0 || r.h <= 0) {
        return;
    }
    ZUI_EMIT_ITEM(it.op = DisplayOp::DrawImage; it.rect = r; it.color = Color{255, 255, 255, 255};
                 it.text.assign(path.begin(), path.end()); it.has_xform = true; it.xform_a = a;
                 it.xform_b = b; it.xform_c = c0; it.xform_d = d; it.xform_e = e; it.xform_f = f;);
}

void DisplayList::shadow_rect(Rect r, Color c, CornerRadii radii, float blur, bool inset, Rect hole,
                              CornerRadii hole_radii) {
    if (r.w <= 0 || r.h <= 0 || color_is_transparent(c)) {
        return;
    }
    if (inset && (hole.w <= 0.f || hole.h <= 0.f)) {
        // Degenerate hole: the whole border box is in shadow.
        hole = Rect{r.x + r.w * 0.5f, r.y + r.h * 0.5f, 0.f, 0.f};
    }
    ZUI_EMIT_ITEM(it.op = DisplayOp::ShadowRect; it.rect = r; it.color = c; it.radii = radii;
                 it.radius = radii.max_component(); it.blur = std::max(0.f, blur); it.inset = inset;
                 it.hole = hole; it.hole_radii = hole_radii;);
}

void DisplayList::backdrop_filter(Rect r, CornerRadii radii, FilterEffects effects) {
    if (!effects.any() || r.w <= 0.f || r.h <= 0.f) {
        return;
    }
    ZUI_EMIT_ITEM(it.op = DisplayOp::BackdropFilter; it.rect = r; it.radii = radii;
                 it.radius = radii.max_component(); it.filter = std::move(effects);
                 it.blur = it.filter.blur;);
}

void DisplayList::begin_filter(Rect bounds, FilterEffects effects) {
    if (!effects.any() || bounds.w <= 0.f || bounds.h <= 0.f) {
        return;
    }
    ZUI_EMIT_ITEM(it.op = DisplayOp::BeginFilter; it.rect = bounds; it.filter = std::move(effects);
                 it.blur = it.filter.blur; if (it.filter.has_mask && it.filter.mask_is_gradient) {
                     it.gradient = it.filter.mask_gradient;
                 } if (it.filter.has_mask && !it.filter.mask_path.empty()) {
                     it.text = it.filter.mask_path;
                 });
}

void DisplayList::end_filter() {
    ZUI_EMIT_ITEM(it.op = DisplayOp::EndFilter;);
}

void DisplayList::begin_transform(Rect local_bounds, float a, float b, float c0, float d, float e,
                                  float f) {
    if (local_bounds.w <= 0.f || local_bounds.h <= 0.f) {
        return;
    }
    ZUI_EMIT_ITEM(it.op = DisplayOp::BeginTransform; it.rect = local_bounds; it.has_xform = true;
                 it.xform_a = a; it.xform_b = b; it.xform_c = c0; it.xform_d = d; it.xform_e = e;
                 it.xform_f = f;);
}

void DisplayList::end_transform() {
    ZUI_EMIT_ITEM(it.op = DisplayOp::EndTransform;);
}

void DisplayList::truncate(std::size_t n) {
    if (n < items_.size()) {
        items_.resize(n);
    }
}

void DisplayList::move_range(std::size_t from, std::size_t to, DisplayList& into) {
    if (from >= to || to > items_.size()) {
        return;
    }
    into.items_.reserve(into.items_.size() + (to - from));
    for (std::size_t i = from; i < to; ++i) {
        into.items_.push_back(std::move(items_[i]));
    }
    items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(from),
                 items_.begin() + static_cast<std::ptrdiff_t>(to));
}

namespace {

bool box_in_node(const layout::Box* box, const html::Node* root) {
    if (root == nullptr) {
        return false;
    }
    for (const layout::Box* cursor = box; cursor != nullptr; cursor = cursor->parent) {
        for (const html::Node* node = cursor->node; node != nullptr; node = node->parent) {
            if (node == root) {
                return true;
            }
        }
    }
    return false;
}

void shift_poly(DisplayItem& item, float dx, float dy) {
    if (dx == 0.f && dy == 0.f) {
        return;
    }
    for (std::size_t i = 0; i + 1 < item.poly.size(); i += 2) {
        item.poly[i] += dx;
        item.poly[i + 1] += dy;
    }
}

void shift_item(DisplayItem& item, float dx, float dy) {
    item.rect.x += dx;
    item.rect.y += dy;
    item.hole.x += dx;
    item.hole.y += dy;
    shift_poly(item, dx, dy);
    if (item.has_xform) {
        item.xform_e += dx;
        item.xform_f += dy;
    }
}

bool ghost_ink(DisplayOp op) {
    return op == DisplayOp::FillRect || op == DisplayOp::StrokeRect || op == DisplayOp::DrawText ||
           op == DisplayOp::DrawImage || op == DisplayOp::ShadowRect || op == DisplayOp::FillGradient;
}

}  // namespace

void DisplayList::append_node_ghost(const html::Node* root, float dx, float dy) {
    if (root == nullptr) {
        return;
    }
    const std::size_t count = items_.size();
    for (std::size_t i = 0; i < count; ++i) {
        const DisplayItem& item = items_[i];
        if (!ghost_ink(item.op) || !box_in_node(item.owner, root)) {
            continue;
        }
        DisplayItem copy = item;
        ++g_item_build.copy_n;
        shift_item(copy, dx, dy);
        items_.push_back(std::move(copy));
    }
}

void DisplayList::append_shifted(const DisplayList& src, float dx, float dy, Rect clip,
                                 bool has_clip,
                                 void (*adjust)(const DisplayItem& item, ItemShift& shift,
                                                const void* ctx),
                                 const void* ctx) {
    Rect running = clip;
    bool clipping = has_clip;
    for (const DisplayItem& item : src.items()) {
        if (item.op == DisplayOp::PopClip) {
            // A recorded sequence is balanced, and the outermost clip of a listing is the one the
            // caller is painting through, so a pop restores exactly that.
            running = clip;
            clipping = has_clip;
            items_.push_back(item);
            ++g_item_build.copy_n;
            continue;
        }
        ItemShift shift;
        shift.dx = dx;
        shift.dy = dy;
        if (adjust != nullptr) {
            adjust(item, shift, ctx);
        }
        if (shift.skip) {
            continue;
        }
        DisplayItem copy = item;
        ++g_item_build.copy_n;
        copy.rect.x += shift.dx;
        copy.rect.y += shift.dy;
        shift_poly(copy, shift.dx, shift.dy);
        if (item.op == DisplayOp::PushClip) {
            // The clip operation keeps its own rectangle: the painter pushes a scrollport as it is,
            // and the frame it is replayed into intersects it at draw time. Narrowing it here would
            // record *this* frame's outer rectangle into the listing, which is what makes a listing
            // recorded at one scroll drop content the next one can see.
            if (clipping) {
                running = intersect_rect(running, copy.rect);
            } else {
                running = copy.rect;
            }
            clipping = true;
            items_.push_back(std::move(copy));
            continue;
        }
        // A text item's rectangle is the pen origin, not the run's extent, so the clip stack is
        // what cuts it; every other operation carries the rectangle it paints inside.
        // Group boundaries (`Begin/EndFilter`, `Begin/EndTransform`) carry no rectangle of their
        // own: culling an `End` (rect is 0x0) left its `Begin` in the list, and the GPU renderers
        // multiply every following primitive by that group's opacity - the calendar's agenda
        // `opacity: 0.8` made the later fixed overlay (scrim + edit dialog) translucent.
        const bool group_boundary =
            item.op == DisplayOp::BeginFilter || item.op == DisplayOp::EndFilter ||
            item.op == DisplayOp::BeginTransform || item.op == DisplayOp::EndTransform;
        if (clipping && item.op != DisplayOp::DrawText && item.op != DisplayOp::DrawTextMask &&
            !group_boundary) {
            Rect test = copy.rect;
            if (item.op == DisplayOp::StrokeRect) {
                // A stroke straddles the rectangle it is given (CSS 2.1 section 8.5.3): half of it
                // lies outside, so the test has to allow for the width it was drawn with.
                const float slack = std::max(1.0f, item.stroke_width);
                test = Rect{test.x - slack, test.y - slack, test.w + 2.f * slack,
                            test.h + 2.f * slack};
            } else if (item.op == DisplayOp::ShadowRect && item.blur > 0.f) {
                // A blurred shadow spills past the shape by roughly the blur radius.
                const float slack = item.blur * 1.5f;
                test = Rect{test.x - slack, test.y - slack, test.w + 2.f * slack,
                            test.h + 2.f * slack};
            }
            const Rect hit = intersect_rect(test, running);
            if (hit.w <= 0.f || hit.h <= 0.f) {
                continue;
            }
        }
        items_.push_back(std::move(copy));
    }
}

}  // namespace paint
}  // namespace zui
