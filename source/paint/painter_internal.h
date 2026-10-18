#ifndef ZUI_SOURCE_PAINT_PAINTER_INTERNAL_H
#define ZUI_SOURCE_PAINT_PAINTER_INTERNAL_H

// Internals of the painter: box geometry, background painting, shadows, text decoration, color
// effects, and the box-tree walk all live in sibling files. The types and helpers they share are
// declared here; `painter.cpp` keeps the Painter entry points.

#include "html/document.h"
#include "layout/box.h"
#include "layout/style_literal.h"
#include "paint/display_list.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace zui {
namespace paint {
namespace painter_detail {
constexpr float kThumbInset = 3.0f;

/// docs/PERF_REFACTOR.md capability E: the side of the square grid a retained listing is tiled on.
/// A frame's damage drops the tiles it touches and keeps the rest, and the narrow rebuild walks
/// only the boxes that reach one of the dropped tiles.
constexpr float kTilePx = 256.0f;

const std::string* computed_slow(const layout::Box& box, const char* key);

// `key` is a string literal. /Ob0 does not inline a helper, so the slot index has to fold at
// the call. `paint_box_at` installs a `SlotGuard` before the first read; without one this is
// the same map probe `computed_slow` does.
#define computed(box, key) style_literal((box).node, key)

bool establishes_own_transform(const layout::Box& box);

float attr_float(const html::Node* node, const char* key, float fallback);

float node_float_value(const html::Node* node, float fallback);

bool appearance_native(const layout::Box& box, std::string_view widget_keyword);

bool flow_debug_on();

const html::Node* owner_node(const layout::Box& box);

Color overlay_rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a);

const html::Node* run_owning_node(const layout::Box& box, const layout::TextRun& run);

bool is_placeholder_node(const html::Node* node);

constexpr Color kUaHighlight{0x00, 0x78, 0xD7, 255};

constexpr Color kUaHighlightText{255, 255, 255, 255};

void selection_colors(const html::Node* node, Color& fg, Color& bg);

float run_byte_x(const layout::TextRun& run, std::size_t byte_in_run);

Color paint_color(const layout::Box& box, const std::string* v);

Color apply_opacity(Color c, float opacity);

bool parse_css_filter(std::string_view css, FilterEffects& out,
                      Color current = Color{0, 0, 0, 255});

Color used_background(const layout::Box& box);

Color canvas_background(const layout::Box& root);

void paint_modal_backdrops(const layout::Box& box, DisplayList& out, const Rect& vp);

bool display_none(const layout::Box& box);

bool visibility_hidden(const layout::Box& box);

CornerRadii resolve_border_radii(const layout::LayoutStyle& style, float width, float height);

CornerRadii inset_radii(CornerRadii radii, float inset);

CornerRadii inset_radii_sides(CornerRadii radii, float top, float right, float bottom, float left);

Rect border_rect(const layout::Box& box, float dx, float dy);

Rect padding_rect(const layout::Box& box, float dx, float dy);

Rect content_rect(const layout::Box& box, float dx, float dy);

bool has_top_level_comma(std::string_view s);

std::string lower_ascii(std::string_view s);

enum class BgBoxKind { Border, Padding, Content, Text };

BgBoxKind parse_bg_clip(const std::string* v);

BgBoxKind parse_bg_origin(const std::string* v);

Rect bg_box_rect(const layout::Box& box, float dx, float dy, BgBoxKind kind);

CornerRadii bg_box_radii(const layout::Box& box, const CornerRadii& border_radii, BgBoxKind kind);

std::string_view trim_sv_local(std::string_view s);

bool parse_bg_length(std::string_view tok, float basis, float& out_px, bool& unparseable);

bool parse_bg_url(std::string_view image, std::string& path_out);

bool is_paintable_bg_url(std::string_view image);

std::string resolve_paint_resource(const std::string& src, const std::string& resource_base);

bool resolve_bg_image_rect(const Rect& origin, const std::string* pos_v, const std::string* size_v,
                           Rect& image_rect, float intrinsic_w = 0.f, float intrinsic_h = 0.f);

bool resolve_object_fit_rect(const Rect& content, float nat_w, float nat_h,
                             const std::string* fit_v, const std::string* pos_v, Rect& out);

bool starts_with_ci(std::string_view image, std::string_view prefix);

bool is_paintable_gradient_image(std::string_view image);

bool is_paintable_linear_gradient(std::string_view image);

/// One background-image layer after the comma lists are matched (CSS Backgrounds 3 §2.1).
/// Shorter lists cycle; `background-image` decides how many layers exist.
struct BgPaintLayer {
    std::string image;
    std::string position;
    std::string size;
    std::string repeat;
    std::string attachment;
    BgBoxKind clip = BgBoxKind::Border;
    BgBoxKind origin = BgBoxKind::Padding;
    /// CSS Backgrounds 3 `background-blend-mode` for this image layer. The background colour
    /// is not a layer and always blends as normal.
    MixBlendMode blend = MixBlendMode::Normal;
};

std::vector<BgPaintLayer> background_layers(const std::string* image, const std::string* position,
                                            const std::string* size, const std::string* repeat,
                                            const std::string* attachment, const std::string* clip,
                                            const std::string* origin);

/// Zip `background-blend-mode` onto `layers` (shorter lists cycle). A null property leaves
/// every layer at `normal`. Call only when a paintable image exists, so documents that never
/// set the property do not look it up.
void apply_background_blend(std::vector<BgPaintLayer>& layers, const std::string* blend);

inline bool background_has_blend(const std::vector<BgPaintLayer>& layers) {
    for (const BgPaintLayer& layer : layers) {
        if (layer.blend != MixBlendMode::Normal) {
            return true;
        }
    }
    return false;
}

/// Open an isolated backdrop for the background stack. The caller paints the background
/// colour and the image layers, then `end_filter`. Returns false when `bounds` is empty.
inline bool begin_background_isolation(DisplayList& out, const Rect& bounds) {
    if (bounds.w <= 0.f || bounds.h <= 0.f) {
        return false;
    }
    FilterEffects fx;
    fx.isolate = true;
    out.begin_filter(bounds, fx);
    return true;
}

/// Paint one layer into `clip` (the background painting area). `origin` is the positioning area
/// after `background-attachment` has been applied. `no-repeat` places the image and intersects
/// it with the clip; any other repeat fills the clip once (the same subset the single-layer
/// path already shipped).
/// CSS Backgrounds 3 border-image. True when the image is painted and the border stroke
/// must be skipped. A missing or `none` source returns false without reading the other
/// longhands. A transformed box is left to the caller (the stroke already follows the affine).
bool paint_border_image(DisplayList& out, const layout::Box& box, const Rect& border_box,
                        float opacity, const std::string& resource_base);

void paint_one_background_image(DisplayList& out, std::string_view image, std::string_view position,
                                std::string_view size, std::string_view repeat, const Rect& origin,
                                const Rect& clip, const CornerRadii& clip_radii, float opacity,
                                const std::string& resource_base,
                                const layout::Box* color_box = nullptr);

struct BoxShadowLayer {
    float ox = 0;
    float oy = 0;
    float blur = 0;
    float spread = 0;
    Color color{0, 0, 0, 255};
    bool inset = false;
};

std::vector<BoxShadowLayer> parse_box_shadows(std::string_view css, const layout::Box& box);

/// How far an outer box-shadow paints past the border box, one side each. A transformed
/// box can sit outside the window while this fringe is still on screen (the reader's
/// hidden tray). The visit test has to include it or the shadow is never emitted.
void outer_shadow_outset(std::string_view css, const layout::Box& box, float& left, float& top,
                         float& right, float& bottom);

void emit_outer_box_shadow(DisplayList& out, const Rect& br, const CornerRadii& radii,
                           const BoxShadowLayer& shadow, float opacity);

void emit_inset_box_shadow(DisplayList& out, const Rect& br, const CornerRadii& radii,
                           const BoxShadowLayer& shadow, float opacity);

bool paint_rounded_centerline_stroke(DisplayList& out, Rect outer, CornerRadii outer_radii,
                                     float thickness, Color color, bool dotted);

Color border_inset_outset_tone(Color color, bool is_darken);

bool border_dark_outer_for(layout::BorderLineStyle style, bool is_top_or_left);

void paint_border_edge(DisplayList& out, const layout::Box& box, bool horizontal, Rect strip,
                       Color color, layout::BorderLineStyle style, bool dark_on_outer,
                       bool outer_at_low);

struct BoxDecorationPaint {
    Rect br;
    CornerRadii radii;
    float bt = 0;
    float brw = 0;
    float bb = 0;
    float bl = 0;
    Color ct{};
    Color cr{};
    Color cb{};
    Color cl{};
    Color bg{};
    bool has_paintable_bg = false;
    BgBoxKind clip_kind = BgBoxKind::Border;
    bool paint_self = true;
    bool empty_cell_hidden = false;
    float dx = 0;
    float dy = 0;
    float op = 1;
};

void paint_box_border(DisplayList& out, const layout::Box& box, const BoxDecorationPaint& d,
                      const std::string& resource_base);
void paint_box_outline(DisplayList& out, const layout::Box& box, const BoxDecorationPaint& d);
void paint_box_column_rule(DisplayList& out, const layout::Box& box, const BoxDecorationPaint& d);

struct TextShadowLayer {
    float ox = 0;
    float oy = 0;
    float blur = 0;
    Color color{0, 0, 0, 255};
};

std::vector<TextShadowLayer> parse_text_shadows(std::string_view css, const layout::Box& box);

void emit_text_with_shadows(DisplayList& out, float text_x, float text_y, Color fg,
                            std::string_view text, float font_px, std::string_view family,
                            int font_weight, const html::Node* debug_owner, float letter_spacing,
                            bool font_italic, bool font_kerning_none, float opacity,
                            const std::vector<TextShadowLayer>& shadows,
                            TextGlyphOrient orient = TextGlyphOrient::Upright,
                            bool has_xform = false, float xa = 1.f, float xb = 0.f, float xc = 0.f,
                            float xd = 0.f, float xe = 1.f, float xf = 0.f,
                            const std::vector<float>* glyph_advances = nullptr,
                            const std::vector<std::uint32_t>* glyph_ids = nullptr);

TextGlyphOrient glyph_orient_for(layout::WritingMode mode);

enum class TextDecorStyle { Solid, Double, Dotted, Dashed, Wavy };

/// CSS Text Decoration 4 `text-decoration-skip-ink`. `Auto` is the initial value: underlines
/// and overlines skip glyph ink, line-through does not. `All` skips every line. `None` draws
/// the line through the glyphs.
enum class SkipInk { Auto, None, All };

struct TextDecoration {
    bool underline = false;
    bool overline = false;
    bool line_through = false;
    TextDecorStyle style = TextDecorStyle::Solid;
    Color color{0, 0, 0, 255};
    float thickness = 0.f;  // 0 = auto
    /// Extra shift from the automatic underline, in px. Positive moves away from the text.
    /// Zero is `text-underline-offset: auto`.
    float underline_offset = 0.f;
    /// `text-underline-position: under`. The automatic position is the font's under edge.
    bool underline_under = false;
    SkipInk skip_ink = SkipInk::Auto;
};

bool parse_text_decoration(const layout::Box& box, TextDecoration& out);

void stroke_decor_segment(DisplayList& out, float x0, float x1, float y, float thickness,
                          Color color, TextDecorStyle style);

/// `run` and `font_box` supply the glyphs `skip_ink` punches out of the line. Either may be
/// null; the line is then drawn solid, which is also what `skip-ink: none` does.
void emit_text_decoration(DisplayList& out, float text_x, float text_y, float run_w, float ascent,
                          float font_px, float descent, const TextDecoration& dec, float opacity,
                          const layout::TextRun* run = nullptr,
                          const layout::Box* font_box = nullptr);

/// CSS Text Decoration 3 emphasis marks. No-op unless the box sets `text-emphasis` or
/// `text-emphasis-style`. `text_y` is the alphabetic baseline.
void emit_text_emphasis(DisplayList& out, const layout::Box& box, float text_x, float text_y,
                        const layout::TextRun& run, float opacity);

/// Glyph coverage for `background-clip: text`.
void emit_background_text_mask(DisplayList& out, const layout::Box& box, float dx, float dy);

float run_advance_px(const layout::TextRun& run);

bool cell_has_flow_content(const layout::Box& box);

Rect scrollport_rect(const layout::Box& box, float dx, float dy, float thickness);

bool apply_lifted_clip(const layout::Box& child, const layout::Box& walker, float dx, float dy,
                       float thickness, Rect& clip, bool& has_clip);

/// The overflow clip this box pushes around its own in-flow content: scrollport, with border
/// radii inset by the border width. A lifted descendant is painted by an ancestor after this
/// clip has been popped, so that ancestor pushes the same clip again.
void push_overflow_clip(DisplayList& out, const layout::Box& box, float dx, float dy,
                        float thickness);

/// Push one rounded overflow clip per clipping ancestor between `child` and `walker`, outermost
/// first. Returns how many clips were pushed; the caller pops that many after the child.
/// `apply_lifted_clip` still narrows the cull rectangle. It does not carry radii.
int push_lifted_overflow_clips(DisplayList& out, const layout::Box& child,
                               const layout::Box& walker, float dx, float dy, float thickness);

Color apply_opacity(Color c, float opacity);

float node_opacity(const layout::Box& box);

Color overlay_rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a);

Color paint_color(const layout::Box& box, const std::string* v);

bool parse_css_filter(std::string_view css, FilterEffects& out, Color current);

void parse_scrollbar_colors(const layout::Box& box, Color& thumb, Color& track);

// Defined in retained.cpp; the box walk asks whether a container keeps a listing at all.
bool scroll_container(const layout::Box& box);
bool subtree_has_anchored(const layout::Box& box);

/// docs/PERF_REFACTOR.md capability B: whether @p box is the *principal* box of a top-layer member
/// (`html::in_top_layer`). A member paints after the document walk instead of inside it, so the
/// base walk skips it and no retained listing holds one. A `BoxRole::Text` box carries its
/// element's node but paints as part of that element, so it is not a member of its own.
/// Defined in walk.cpp.
bool top_layer_member(const layout::Box& box);

}  // namespace painter_detail

// Defined in walk.cpp: the bitmap background layer emitter is shared by `Painter::paint`'s
// canvas path and the box walk.
void emit_bg_bitmap(DisplayList& out, const std::string& path, Rect image_rect, Rect clip_area,
                    CornerRadii clip_radii, const std::string& repeat_l);

}  // namespace paint
}  // namespace zui

#endif
