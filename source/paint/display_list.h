#ifndef ZUI_PAINT_DISPLAY_LIST_H
#define ZUI_PAINT_DISPLAY_LIST_H

#include "layout/box.h"
#include "layout/caret/caret_types.h"
#include "paint/display_types.h"

#include <cstddef>
#include <optional>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace zui {

namespace html {

struct Node;
class DamageTracker;

}  // namespace html

namespace paint {

enum class DisplayOp {
    PushClip,
    PopClip,
    FillRect,
    StrokeRect,
    DrawText,
    DrawImage,
    FillGradient,
    /// `box-shadow` shape. Outer: `rect` is the spread-adjusted, offset border box. Inset:
    /// `rect` is the border box (clip) and `hole` is the offset+spread-shrunk punch-out. `blur`
    /// is the CSS blur radius (0 = hard edge). Soft raster applies blur; GPU may draw hard outer
    /// only and skip inset.
    ShadowRect,
    /// Effect group (`filter` / `mask-image` / `mix-blend-mode`): items until EndFilter paint
    /// into an offscreen layer (soft-raster), then effects are applied and the layer is composited.
    BeginFilter,
    EndFilter,
    /// CSS Filter Effects 2 `backdrop-filter`. Sample what has already been painted, apply
    /// `filter` (blur sigma is the CSS length in px), and composite the result into `rect`,
    /// clipped by `radii` when they are uniform. Emitted before the element's own decorations.
    BackdropFilter,
    /// Transform subtree: items until EndTransform paint in local space into an offscreen
    /// layer (soft-raster / GPU FBO), then the layer is Aff2-blitted to the parent. `rect` is
    /// the local bounds; `has_xform` + `xform_*` map local → world.
    BeginTransform,
    EndTransform,
    /// Glyph coverage for `background-clip: text`. Same fields as `DrawText`, but the colour is
    /// ignored: OpenGL uses the coverage to punch the current text-clip layer. Not a visible run.
    DrawTextMask,
};

/// Who painted the item. The offscreen raster writes this into a mask so a scrollbar
/// skin difference is not scored as content ink (TESTPLAN section 9).
enum class PaintSource : std::uint8_t { Content = 0, Scrollbar = 1 };

/// DrawText glyph orientation under CSS writing-mode / text-orientation sideways.
/// Upright advances on +X (horizontal-tb). SidewaysRl rotates each glyph 90° clockwise and
/// advances on +Y (vertical-*-rl / sideways-rl). SidewaysLr rotates 90° counter-clockwise and
/// advances on +Y (vertical-*-lr / sideways-lr). CJK upright under vertical-rl is deferred.
enum class TextGlyphOrient : std::uint8_t { Upright = 0, SidewaysRl = 1, SidewaysLr = 2 };

struct DisplayItem {
    DisplayOp op = DisplayOp::FillRect;
    Rect rect;
    Color color;
    float stroke_width = 1;
    /// Max of `radii` for quick "has rounding" checks; keep in sync when writing items.
    float radius = 0;
    CornerRadii radii;
    std::string text;
    std::string font_family;
    float font_px = 16;
    int font_weight = 400;
    bool font_italic = false;
    /// CSS letter-spacing added after each painted glyph, matching layout (Chrome includes the
    /// trailing spacing after the last character).
    float letter_spacing = 0;
    /// CSS `font-kerning: none`. When false, paint applies the same pair kerning layout already
    /// baked into shaped advances so glyph pens match measured run width.
    bool font_kerning_none = false;
    /// Per-codepoint advances straight from the layout (`layout::TextRun::glyph_advances`). They
    /// already carry letter-spacing, the font's pair kerning and `text-align: justify`'s stretch,
    /// which a renderer re-deriving them from the font cannot reproduce: a justified line then
    /// paints un-stretched (measured on `inline-text`'s `div#justify`). Empty means the renderer
    /// falls back to the font, which is what non-mappable runs get.
    std::vector<float> glyph_advances;
    /// Parallel to `glyph_advances` when the run has a ligature. Empty paints every codepoint's
    /// cmap glyph. `0xffffffff` (`text::kSkipPaintGlyph`) draws no ink. Any other value is a
    /// glyph id loaded from the face that codepoint maps to.
    std::vector<std::uint32_t> glyph_ids;
    TextGlyphOrient text_orient = TextGlyphOrient::Upright;
    PaintSource source = PaintSource::Content;
    /// Diagnostic only: the element that painted this item, filled when the reader's flow dump runs
    /// with ZUI_DEBUG_FLOW_ITEMS. A stray glyph run can then be traced back to the box that produced
    /// it instead of being guessed at from its colour and position. A borrowed pointer - the node
    /// outlives the frame - so an ordinary frame pays nothing beyond the field itself.
    const html::Node* debug_owner = nullptr;
    /// The box whose paint emitted this item (the innermost box being painted when it was appended).
    /// docs/PERF_REFACTOR.md capability E: a retained listing tiles its items so a damage can drop
    /// only the tiles it touches; the owner is what lets the narrow rebuild replace exactly the
    /// items of the boxes it visited, in the listing's own order. Null for items no box owns.
    const layout::Box* owner = nullptr;
    /// FillGradient only: the line and stops resolved by linear_gradient_for_box().
    Gradient gradient;
    /// ShadowRect / blurred DrawText: CSS shadow blur radius in device px.
    float blur = 0;
    /// ShadowRect inset: punch-out rectangle inside `rect` (ignored when `inset` is false).
    Rect hole;
    CornerRadii hole_radii;
    bool inset = false;
    /// Optional Aff2 (a b c / d e f) mapping `rect` local space to world for FillRect /
    /// StrokeRect / PushClip / DrawText / DrawImage when the box has a non-translation
    /// `transform`. DrawText `rect` is the local pen origin; DrawImage `rect` is the local dest.
    bool has_xform = false;
    float xform_a = 1;
    float xform_b = 0;
    float xform_c = 0;
    float xform_d = 0;
    float xform_e = 1;
    float xform_f = 0;
    /// BeginFilter only.
    FilterEffects filter;
    /// PushClip only: interleaved world-space `polygon()` vertices (x, y, …). Empty means the
    /// clip is `rect` (and its radius). Paint uploads the first 16 vertices; hit-testing uses
    /// the parsed shape. `poly_evenodd` selects the CSS fill-rule (initial value is nonzero).
    std::vector<float> poly;
    bool poly_evenodd = false;
    /// DrawImage source rectangle in normalized texture space. (0, 0)–(1, 1) samples the whole
    /// file. These are not positions: `append_shifted` must not translate them.
    float src_u0 = 0.f;
    float src_v0 = 0.f;
    float src_u1 = 1.f;
    float src_v1 = 1.f;
};

class DisplayList {
public:
    void clear() { items_.clear(); }
    /// Drops everything past @p n. The retained path (`LAYOUT.md` section 9 step D) paints a
    /// container's content into the frame list, moves that range into the listing, and re-appends
    /// only the part this frame's clip can see.
    void truncate(std::size_t n);
    /// Moves items `[from, to)` out of this list and onto the end of @p into.
    void move_range(std::size_t from, std::size_t to, DisplayList& into);
    /// Per-item adjustment for `append_shifted`. `dx` / `dy` start as the caller's shift. `skip`
    /// drops the item (a scrollbar whose thumb has to be painted again from current geometry).
    struct ItemShift {
        float dx = 0;
        float dy = 0;
        bool skip = false;
    };
    /// Appends @p src shifted by `(dx, dy)`. Clip operations move with the content they clip, and
    /// an item that cannot reach @p clip is dropped - the listing covers a margin wider than the
    /// viewport, so a replayed frame has to cut back to the rectangle it is painting through.
    /// Text items are never dropped: their rectangle carries the pen origin, not the run's extent,
    /// so the clip stack is what cuts them (the raster and the renderers clip as they blend).
    /// `adjust`, when set, may add a further shift per item (a nested scroller's own delta) or
    /// skip one. A null `adjust` is the uniform shift.
    void append_shifted(const DisplayList& src, float dx, float dy, Rect clip, bool has_clip,
                        void (*adjust)(const DisplayItem& item, ItemShift& shift, const void* ctx) =
                            nullptr,
                        const void* ctx = nullptr);
    /// Copies the already-painted items of @p root (and its descendants) onto the end of this
    /// list, shifted by `(dx, dy)`. Structural clips and opacity groups are left behind, so the
    /// copy is the element's own ink at full strength — the drag thumbnail.
    void append_node_ghost(const html::Node* root, float dx, float dy);
    void push_clip(Rect r, float radius = 0);
    void push_clip(Rect r, CornerRadii radii);
    /// `pts` is interleaved world-space vertices. `r` is their axis-aligned bound (the scissor).
    void push_clip_polygon(Rect r, std::vector<float> pts, bool evenodd);
    void push_clip_xform(Rect r, CornerRadii radii, float a, float b, float c0, float d, float e,
                         float f);
    void pop_clip();
    void fill_rect(Rect r, Color c, float radius = 0, PaintSource source = PaintSource::Content);
    void fill_rect(Rect r, Color c, CornerRadii radii, PaintSource source = PaintSource::Content);
    /// Like `fill_rect`, but `r` is in the Aff2's local space (box border-box coords → world).
    void fill_rect_xform(Rect r, Color c, CornerRadii radii, float a, float b, float c0, float d,
                         float e, float f, PaintSource source = PaintSource::Content);
    void fill_gradient(Rect r, const Gradient& gradient, float radius = 0,
                       PaintSource source = PaintSource::Content);
    void fill_gradient(Rect r, const Gradient& gradient, CornerRadii radii,
                       PaintSource source = PaintSource::Content);
    void stroke_rect(Rect r, Color c, float width, float radius = 0);
    void stroke_rect(Rect r, Color c, float width, CornerRadii radii);
    /// Border ring with an asymmetric inner edge (`hole` = padding box, `hole_radii` =
    /// CSS Backgrounds §5.3 inset radii). Soft-raster subtracts hole coverage; GPU may
    /// approximate with a uniform stroke from the mean inset.
    void stroke_ring(Rect outer, CornerRadii outer_radii, Rect hole, CornerRadii hole_radii,
                     Color c);
    void draw_text(float x, float y, Color c, std::string_view utf8, float font_px,
                   std::string_view family, int font_weight = 400,
                   const html::Node* debug_owner = nullptr, float letter_spacing = 0,
                   bool font_italic = false, bool font_kerning_none = false, float blur = 0,
                   TextGlyphOrient orient = TextGlyphOrient::Upright,
                   const std::vector<float>* glyph_advances = nullptr,
                   const std::vector<std::uint32_t>* glyph_ids = nullptr);
    /// Coverage mask for `background-clip: text`. `(x, y)` is the same pen `draw_text` uses.
    /// The item is not visible text: a transparent colour still emits, and the renderer reads
    /// only glyph coverage.
    void draw_text_mask(float x, float y, std::string_view utf8, float font_px,
                        std::string_view family, int font_weight = 400,
                        const html::Node* debug_owner = nullptr, float letter_spacing = 0,
                        bool font_italic = false, bool font_kerning_none = false,
                        const std::vector<float>* glyph_advances = nullptr,
                        const std::vector<std::uint32_t>* glyph_ids = nullptr);
    /// Like `draw_text`, but `(x, y)` is the Aff2 local pen (border-box coords → world).
    void draw_text_xform(float x, float y, Color c, std::string_view utf8, float font_px,
                         std::string_view family, float a, float b, float c0, float d, float e,
                         float f, int font_weight = 400, const html::Node* debug_owner = nullptr,
                         float letter_spacing = 0, bool font_italic = false,
                         bool font_kerning_none = false, float blur = 0,
                         TextGlyphOrient orient = TextGlyphOrient::Upright,
                         const std::vector<float>* glyph_advances = nullptr,
                         const std::vector<std::uint32_t>* glyph_ids = nullptr);
    void draw_image(Rect r, std::string_view path);
    /// `u0,v0`–`u1,v1` is the source rectangle in normalized texture space. `tint` modulates
    /// the sample (white is unchanged). Used by `border-image` slices.
    void draw_image(Rect r, std::string_view path, float u0, float v0, float u1, float v1,
                    Color tint);
    /// Like `draw_image`, but `r` is in Aff2 local space (border-box coords → world).
    void draw_image_xform(Rect r, std::string_view path, float a, float b, float c0, float d,
                          float e, float f);
    void shadow_rect(Rect r, Color c, CornerRadii radii, float blur, bool inset = false,
                     Rect hole = {}, CornerRadii hole_radii = {});
    /// `bounds` is the axis-aligned box of the filtered content (plus caller-added blur pad).
    void begin_filter(Rect bounds, FilterEffects effects);
    /// Filtered backdrop of `r` (the border box). `effects.blur` is the CSS `blur()` stddev in px.
    void backdrop_filter(Rect r, CornerRadii radii, FilterEffects effects);
    void end_filter();
    /// Local-space subtree; `a..f` maps `local_bounds` to world (Aff2 blit on EndTransform).
    void begin_transform(Rect local_bounds, float a, float b, float c0, float d, float e, float f);
    void end_transform();

    /// The box every item appended from now on belongs to (`DisplayItem::owner`). The painter sets
    /// this per box so a retained listing can tell which of its items a narrow rebuild has to
    /// replace. `append_shifted` / `move_range` copy items as they are and ignore it.
    void set_owner(const layout::Box* owner) { owner_ = owner; }
    const layout::Box* owner() const { return owner_; }

    const std::vector<DisplayItem>& items() const { return items_; }

    /// Steal every item, leaving the list empty. The retained path uses this to rebuild a listing
    /// in place (capability E) without copying the items it is going to keep.
    std::vector<DisplayItem> take_items() { return std::move(items_); }
    void replace_items(std::vector<DisplayItem> items) { items_ = std::move(items); }

private:
    void push_item(DisplayItem&& item) {
        item.owner = owner_;
        items_.push_back(std::move(item));
    }

    std::vector<DisplayItem> items_;
    const layout::Box* owner_ = nullptr;
};

Color parse_color(std::string_view css);
bool color_is_transparent(Color c);

/// Resolve a `linear-gradient(...)` value against the painting area it fills: the gradient line
/// runs through the centre of `area`. Returns false for any other background image, including
/// `none`, so a caller can fall back to the background colour alone.
/// `color_box`, when set, resolves `currentcolor` and `light-dark()` in each stop. A null box
/// parses the stop text alone (unit tests, and mask images until that walk can pass a box).
bool linear_gradient_for_box(std::string_view css, Rect area, Gradient& out,
                             const layout::Box* color_box = nullptr);

/// `repeating-linear-gradient(...)` — same geometry as linear, with `repeating` set.
bool repeating_linear_gradient_for_box(std::string_view css, Rect area, Gradient& out,
                                       const layout::Box* color_box = nullptr);

/// `radial-gradient(...)` subset: circle|ellipse, size keywords / lengths, optional `at`
/// position, colour stops. Default is `ellipse farthest-corner at center`.
bool radial_gradient_for_box(std::string_view css, Rect area, Gradient& out,
                             const layout::Box* color_box = nullptr);

/// `repeating-radial-gradient(...)` — same ending shape as radial, with `repeating` set.
bool repeating_radial_gradient_for_box(std::string_view css, Rect area, Gradient& out,
                                       const layout::Box* color_box = nullptr);

/// `conic-gradient(...)`: optional `from <angle>` and `at <position>`, then colour stops.
/// A stop position is an angle or a percentage of one turn. Two positions on one colour are a
/// hard segment. `0deg` is up and angles grow clockwise.
bool conic_gradient_for_box(std::string_view css, Rect area, Gradient& out,
                            const layout::Box* color_box = nullptr);

/// `repeating-conic-gradient(...)` — same centre and `from` as conic, with `repeating` set.
bool repeating_conic_gradient_for_box(std::string_view css, Rect area, Gradient& out,
                                      const layout::Box* color_box = nullptr);

/// Any single-layer gradient image the painter can draw.
bool gradient_image_for_box(std::string_view css, Rect area, Gradient& out,
                            const layout::Box* color_box = nullptr);

/// The colour at a device-space point. Stops interpolate in premultiplied space, which is what
/// CSS Images 3 section 3.4.4 and every browser use for a gradient that carries alpha.
Color gradient_color_at(const Gradient& g, float x, float y);

/// GL/VK shape shaders hold at most four colour stops. Longer stop lists are split into
/// overlapping 4-stop windows; `grad_count < 0` asks the shader to clamp `t` to the chunk so
/// windows do not overwrite each other. `stroke_flag` is 0 (middle), -2 (first), or -1 (last)
/// and is only meaningful when `grad_count < 0` (fill gradients never stroke).
struct GradientGpuChunk {
    int grad_count = 0;
    float stroke_flag = 0;
    float off[4] = {};
    float col[16] = {};
};
void gradient_gpu_chunks(const Gradient& g, std::vector<GradientGpuChunk>& out);

class Painter {
public:
    /// LAYOUT section 9: the classic scrollbar thickness is a platform parameter, not a constant.
    /// The layout reserves it and the painter draws it with the same number.
    void set_scrollbar_thickness(float thickness) { scrollbar_thickness_ = thickness; }

    /// Parent directory of the document (or empty). Relative `background-image: url(...)` paths
    /// resolve against this base the same way `<img src>` resolves against `BoxTree::resource_base_`.
    void set_resource_base(std::string document_path);

    /// Document selection for the next `paint`. Call sites copy `html::Document::selection`.
    void set_selection(html::SelectionRange sel) {
        selection_ = sel;
        selection_node_ = sel.node;
        selection_start_ = sel.start;
        selection_end_ = sel.end;
    }
    void set_selection(html::Node* node, std::size_t start, std::size_t end) {
        html::SelectionRange sel;
        sel.node = node;
        sel.start = start;
        sel.end = end;
        sel.anchor = html::BoundaryPoint{node, start};
        sel.focus = html::BoundaryPoint{node, end};
        selection_ = sel;
        selection_node_ = node;
        selection_start_ = start;
        selection_end_ = end;
    }
    void clear_selection() { set_selection(html::SelectionRange{}); }

    /// Focused editable caret for the next `paint`. A null node paints none. `color` nullopt means
    /// `caret-color: transparent`: no fill, but `last_caret_rect` is still updated for IME.
    /// @p affinity is the wrap side the hit or vertical move recorded. Downstream is the start of
    /// the next line; Upstream is the end of the previous one.
    void set_caret(html::Node* node, std::size_t offset, std::optional<Color> color,
                   bool paint_fill = true,
                   layout::CaretAffinity affinity = layout::CaretAffinity::Downstream) {
        caret_node_ = node;
        caret_offset_ = offset;
        caret_color_ = color;
        caret_paint_fill_ = paint_fill;
        caret_affinity_ = affinity;
    }
    void clear_caret() { set_caret(nullptr, 0, std::nullopt, false); }

    /// IME preedit underline for the next `paint`. Half-open UTF-8 bytes on the focused editable.
    /// A null node or start == end paints no underline.
    void set_composition(html::Node* node, std::size_t start, std::size_t end) {
        composition_node_ = node;
        composition_start_ = start;
        composition_end_ = end;
    }
    void clear_composition() { set_composition(nullptr, 0, 0); }

    /// World-space caret bar from the last `paint` walk (empty when no caret was mapped).
    struct CaretBar {
        float x = 0;
        float y = 0;
        float w = 1;
        float h = 0;
        bool ok = false;
    };
    CaretBar last_caret_rect() const { return this->last_caret_rect_; }

    void paint(const layout::Box& root, DisplayList& out, float viewport_w = 0,
               float viewport_h = 0) const;

    /// `LAYOUT.md` section 9 step D, observation channel (section 16): what the last `paint` did
    /// with the retained listings. `reused` is the number of scroll containers whose content came
    /// back out of a listing instead of a walk, `walked` the number that had to be recorded, and
    /// `items` how many items the listings hold.
    struct RetainedStats {
        std::size_t reused = 0;
        std::size_t walked = 0;
        /// Containers whose content turned out to be anchored to something other than its scroll
        /// (`sticky` / `fixed`), so no listing can be kept for them.
        std::size_t refused = 0;
        std::size_t records = 0;
        std::size_t items = 0;
        /// Listings this frame's damage erased instead of keeping (docs/PERF.md I6). A frame that
        /// changes nothing visible leaves it at 0.
        std::size_t damaged = 0;
        /// Drawing items those listings held: the spec's `damage.items`.
        std::size_t damaged_items = 0;
    };
    RetainedStats retained_stats() const { return retained_; }

    /// Turns the retained listings off. A caller comparing a replayed frame against the frame the
    /// same walk produces needs the two paths, and nothing else about `paint` changes.
    void set_retained(bool on) {
        retained_on_ = on;
        records_.clear();
        retained_ = RetainedStats{};
    }
    bool retained() const { return retained_on_; }

private:
    /// One scroll container's content, as the frame that recorded it painted it. Coordinates are
    /// that frame's device space, in the order the walk produced them, clip operations included.
    struct Record {
        DisplayList list;
        /// The offsets the container itself was recorded at; a replay shifts by the difference.
        float scroll_x = 0;
        float scroll_y = 0;
        float viewport_w = 0;
        float viewport_h = 0;
        /// The node the offsets belong to, so a recycled `Box` address cannot pick up a listing.
        const html::Node* node = nullptr;
        /// Offsets that shift the whole listing: the scroll containers above this one.
        /// `box` is the container the offset belongs to, so a replay can move the items that
        /// container owns without walking them again. Null on a mark recorded before that was kept.
        struct Mark {
            const html::Node* node = nullptr;
            const layout::Box* box = nullptr;
            float x = 0;
            float y = 0;
        };
        std::vector<Mark> outside;
        /// Scroll containers whose content the listing holds. Their offsets cannot move under it.
        std::vector<Mark> inside;
        /// False when something in the listing is anchored to something other than the scroll - a
        /// `sticky` / `fixed` box - because a plain shift would move it with everything else.
        bool usable = true;
        /// The rectangle this listing covers, widened to the retained margin it was recorded with.
        /// A frame's damage only has to re-record the listings it touches (docs/PERF.md I6).
        Rect damage_clip;
        bool has_damage_clip = false;
        /// The clip the walk that recorded this listing painted the container through. The narrow
        /// rebuild (capability E) re-walks with the same clip so the items it produces are the ones
        /// this listing holds, not the ones another clip would have culled.
        Rect walk_clip;
        bool walk_has_clip = false;
    };

    /// Whether this box keeps a listing: it has content that its own scroll offset moves, it is
    /// painted once, and no ancestor anchors it. `out` is where a listing is replayed into.
    bool recordable(const layout::Box& box) const;
    /// One open recording region. The walk writes a container's content while this is on the stack,
    /// and the marks and the taint belong to the region rather than to the painter - a scroll
    /// container inside another one keeps a listing of its own, inside its parent's.
    struct Scope {
        bool tainted = false;
        std::vector<Record::Mark> inside;
    };
    bool replay_record(const layout::Box& box, DisplayList& out, Rect clip, bool has_clip) const;
    void capture_record(const layout::Box& box, DisplayList& out, std::size_t from, std::size_t to,
                        Rect clip, bool has_clip, Rect walk_clip, bool walk_has_clip,
                        const Scope& scope) const;
    void collect_outside_marks(const layout::Box& box, Record& record) const;
    /// Scrollbars sit outside a container's listing.
    void paint_scrollbars(const layout::Box& box, DisplayList& out) const;

    /// docs/PERF_REFACTOR.md capability E. Fills @p out with the `kTilePx` tiles of @p record's
    /// `damage_clip` that @p damage touches, and answers whether this listing can be rebuilt tile
    /// by tile. False means the caller keeps today's behaviour and drops the listing whole: any
    /// record but the outermost one, a moved offset, an unresolvable window, or a damage that
    /// already covers every tile (or too many of them to be worth a narrow walk).
    bool damage_tiles(const layout::Box& root, const layout::Box& box, const Record& record,
                      const std::vector<html::PaintRect>& damage, std::vector<Rect>& out) const;
    /// Re-walks @p box with the tiles as the only reachable region, then replaces in @p record only
    /// the items whose owning box that walk visited. Item order is the listing's own, so a replay
    /// is byte-for-byte what the full walk would have recorded.
    bool rebuild_record_tiles(const layout::Box& box, Record& record,
                              const std::vector<Rect>& tiles) const;

    void paint_box(const layout::Box& box, DisplayList& out, Rect clip, bool has_clip,
                   float opacity) const;
    /// docs/PERF_REFACTOR.md capability B: paint the members of the top layer, in document order,
    /// after the document walk. A member is anchored to the viewport, so it is painted through the
    /// frame's own clip instead of the clip of whichever box it happened to descend from.
    void paint_top_layer(const layout::Box& root, DisplayList& out, Rect clip,
                         bool has_clip) const;
    /// Paint a box that the layout may have split across fragmentainers (LAYOUT.md section 12).
    /// `dx` / `dy` carry the slice shift of every fragment of an ancestor that is currently being
    /// painted, so a nested fragmented box lands in the same place its ancestor's fragment does.
    void paint_box_shifted(const layout::Box& box, DisplayList& out, Rect clip, bool has_clip,
                           float opacity, float dx, float dy) const;
    void paint_box_at(const layout::Box& box, DisplayList& out, Rect clip, bool has_clip,
                      float opacity, float dx, float dy) const;
    void paint_box_contents(const layout::Box& box, DisplayList& out, Rect child_clip,
                            bool child_has_clip, float op, float dx, float dy,
                            bool paint_self) const;
    /// Native form-control chrome (checkbox/radio marks, select chevron, range, progress, file).
    /// Runs inside the box's paint group, so it receives the already-resolved opacity and slice
    /// offset instead of recomputing them.
    void paint_native_widgets(const layout::Box& box, DisplayList& out, float dx, float dy,
                              float op, bool paint_self) const;
    /// `<img>` content: object-fit/object-position against the content box, with a clip when the
    /// fitted image overflows it.
    void paint_replaced_content(const layout::Box& box, DisplayList& out, float dx, float dy,
                                bool paint_self) const;
    /// The box's own text runs: glyphs, `text-overflow: ellipsis`, `::selection` highlight, and
    /// text decoration. `ellipsis_remain` / `ellipsis_drawn` thread through the stacking-ordered
    /// child paint that sits between the ellipsis setup and this call.
    void paint_box_text_runs(const layout::Box& box, DisplayList& out, Color fg, float op, float dx,
                             float dy, float scroll_dx, float scroll_dy, float& ellipsis_remain,
                             bool paint_ellipsis, bool& ellipsis_drawn, bool paint_self) const;
    /// Everything painted behind the content: background colour / image, empty-cell suppression,
    /// outer and inset shadows, borders, outline, and `column-rule`.
    void paint_box_decorations(const layout::Box& box, DisplayList& out, const Rect& br,
                               const CornerRadii& radii, float radius, float op, float dx,
                               float dy, bool paint_self) const;
    void paint_viewport_scrollbars(const layout::Box& root, DisplayList& out, float viewport_w,
                                   float viewport_h) const;

    float scrollbar_thickness_ = 15.f;
    std::string resource_base_;
    /// Node whose background was painted onto the canvas this frame (CSS Backgrounds §2.11);
    /// that box skips its own background colour / image so the canvas is not double-painted.
    mutable const html::Node* canvas_bg_node_ = nullptr;
    html::SelectionRange selection_;
    html::Node* selection_node_ = nullptr;
    std::size_t selection_start_ = 0;
    std::size_t selection_end_ = 0;
    html::Node* caret_node_ = nullptr;
    std::size_t caret_offset_ = 0;
    std::optional<Color> caret_color_;
    bool caret_paint_fill_ = true;
    layout::CaretAffinity caret_affinity_ = layout::CaretAffinity::Downstream;
    mutable CaretBar last_caret_rect_{};
    html::Node* composition_node_ = nullptr;
    std::size_t composition_start_ = 0;
    std::size_t composition_end_ = 0;
    /// Section 9 step D. The frame thread owns one painter and mutates this cache in place; the
    /// listing is keyed by box address, and one paint stamp change (`html::paint_stamp`) drops it,
    /// which is also what keeps a freed box from answering for a new one.
    mutable std::unordered_map<const layout::Box*, Record> records_;
    /// The tiles a damaged listing has to rebuild, keyed by the listing's box. Filled while the
    /// frame's damage is handed out and consumed before the walk that replays the listing.
    mutable std::unordered_map<const layout::Box*, std::vector<Rect>> tile_rebuild_;
    /// Non-null only inside a narrow rebuild: the rectangles it is allowed to reach. A box that
    /// reaches none of them is skipped before it is counted as walked or descended into.
    mutable const std::vector<Rect>* narrow_rects_ = nullptr;
    /// The listing's box while a narrow rebuild walks. Its own record handling is skipped (it is
    /// replaying, not recording), and a nested listing that would have to be *recorded* rather than
    /// replayed fails the rebuild: a partial capture would replace a whole listing.
    mutable const layout::Box* rebuilding_box_ = nullptr;
    mutable bool narrow_failed_ = false;
    mutable RetainedStats retained_{};
    mutable bool retained_on_ = true;
    /// The recording regions currently open, innermost last. A non-empty stack is what makes the
    /// walk reach the retained margin the geometry was mapped with.
    mutable std::vector<Scope> scopes_;
    /// BeginTransform nesting: when >0, Aff2 emissions are relative to `xform_root_aff2_`.
    mutable int xform_layer_depth_ = 0;
    mutable float xform_root_a_ = 1, xform_root_b_ = 0, xform_root_c_ = 0;
    mutable float xform_root_d_ = 0, xform_root_e_ = 1, xform_root_f_ = 0;
    /// The frame's viewport, so a listing recorded at another size is not replayed into this one.
    mutable float frame_w_ = 0;
    mutable float frame_h_ = 0;
    mutable std::uint64_t stamp_ = 0;
    /// Retained listings belong to one document's damage tracker. A switch drops them.
    mutable const html::DamageTracker* stamp_tracker_ = nullptr;
};

}  // namespace paint
}  // namespace zui

#endif
