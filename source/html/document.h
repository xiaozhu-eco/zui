#ifndef ZUI_HTML_DOCUMENT_H
#define ZUI_HTML_DOCUMENT_H

#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "html/style_map.h"

namespace zui {

class ListModel;
class TableModel;

namespace html {

class DamageTracker;

/// Frame used overlay for CSS transitions. Specified values stay in computed.
struct VisualUsed {
    static constexpr std::uint32_t kOpacity = 1u << 0;
    static constexpr std::uint32_t kColor = 1u << 1;
    static constexpr std::uint32_t kBg = 1u << 2;
    static constexpr std::uint32_t kBorderTop = 1u << 3;
    static constexpr std::uint32_t kTransform = 1u << 4;
    static constexpr std::uint32_t kBorderRight = 1u << 5;
    static constexpr std::uint32_t kBorderBottom = 1u << 6;
    static constexpr std::uint32_t kBorderLeft = 1u << 7;
    static constexpr std::uint32_t kWidth = 1u << 8;
    static constexpr std::uint32_t kHeight = 1u << 9;
    static constexpr std::uint32_t kMarginTop = 1u << 10;
    static constexpr std::uint32_t kMarginRight = 1u << 11;
    static constexpr std::uint32_t kMarginBottom = 1u << 12;
    static constexpr std::uint32_t kMarginLeft = 1u << 13;
    static constexpr std::uint32_t kPaddingTop = 1u << 14;
    static constexpr std::uint32_t kPaddingRight = 1u << 15;
    static constexpr std::uint32_t kPaddingBottom = 1u << 16;
    static constexpr std::uint32_t kPaddingLeft = 1u << 17;
    /// Any border-*-color overlay is live.
    static constexpr std::uint32_t kBorder =
        kBorderTop | kBorderRight | kBorderBottom | kBorderLeft;
    static constexpr std::uint32_t kMargin =
        kMarginTop | kMarginRight | kMarginBottom | kMarginLeft;
    static constexpr std::uint32_t kPadding =
        kPaddingTop | kPaddingRight | kPaddingBottom | kPaddingLeft;
    /// Length overlays that change layout geometry (width/height/margin/padding).
    static constexpr std::uint32_t kLayoutLengths = kWidth | kHeight | kMargin | kPadding;
    std::uint32_t flags = 0;
    float opacity = 1;
    std::uint8_t color_r = 0, color_g = 0, color_b = 0, color_a = 255;
    std::uint8_t bg_r = 0, bg_g = 0, bg_b = 0, bg_a = 255;
    std::uint8_t border_top_r = 0, border_top_g = 0, border_top_b = 0, border_top_a = 255;
    std::uint8_t border_right_r = 0, border_right_g = 0, border_right_b = 0, border_right_a = 255;
    std::uint8_t border_bottom_r = 0, border_bottom_g = 0, border_bottom_b = 0,
                 border_bottom_a = 255;
    std::uint8_t border_left_r = 0, border_left_g = 0, border_left_b = 0, border_left_a = 255;
    float tx = 0;
    float ty = 0;
    float sx = 1;
    float sy = 1;
    float rot = 0;
    bool tx_pct = false;
    bool ty_pct = false;
    float width = 0;
    float height = 0;
    float margin_top = 0;
    float margin_right = 0;
    float margin_bottom = 0;
    float margin_left = 0;
    float padding_top = 0;
    float padding_right = 0;
    float padding_bottom = 0;
    float padding_left = 0;
};

/// Fingerprint of live length overlays for LayoutKey (0 when none).
inline std::uint64_t length_overlay_sig(const VisualUsed& u) {
    if ((u.flags & VisualUsed::kLayoutLengths) == 0) {
        return 0;
    }
    auto bits = [](float f) {
        std::uint32_t raw = 0;
        static_assert(sizeof(float) == sizeof(std::uint32_t), "float width");
        std::memcpy(&raw, &f, sizeof(raw));
        return static_cast<std::uint64_t>(raw);
    };
    std::uint64_t h = u.flags;
    h ^= bits(u.width) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    h ^= bits(u.height) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    h ^= bits(u.margin_top) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    h ^= bits(u.margin_right) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    h ^= bits(u.margin_bottom) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    h ^= bits(u.margin_left) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    h ^= bits(u.padding_top) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    h ^= bits(u.padding_right) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    h ^= bits(u.padding_bottom) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    h ^= bits(u.padding_left) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
}

/// Used `::selection` colours for this node after highlight inheritance. `flags == 0` (or a null
/// `Node::selection`) means the chain has no author colour or background, so paint uses the UA
/// Highlight / HighlightText pair. `author_*` tracks author origin through inheritance; a
/// currentColor fill sets `kHasFg` without `kAuthorFg` and must not propagate to children.
struct SelectionStyle {
    static constexpr std::uint8_t kHasFg = 1u << 0;
    static constexpr std::uint8_t kHasBg = 1u << 1;
    static constexpr std::uint8_t kAuthorFg = 1u << 2;
    static constexpr std::uint8_t kAuthorBg = 1u << 3;
    std::uint8_t fg_r = 0, fg_g = 0, fg_b = 0, fg_a = 255;
    std::uint8_t bg_r = 0, bg_g = 0, bg_b = 0, bg_a = 0;
    std::uint8_t flags = 0;
};

/// Declared here so `Node::style(const char*)` can call it. The definition is in document.cpp.
const Atom* literal_atom(const char* text);

struct Node {
    // --- Structure: HTML tree, parse, and Document::set data. Valid for the node's life. ---
    Node* parent = nullptr;
    /// Set on the document element. Descendants reach it by walking `parent`. The tracker
    /// lives on the heap in `Document::damage`, so moving a `Document` does not move it.
    DamageTracker* damage = nullptr;
    std::string tag;
    std::string id;
    std::string class_name;
    std::string text;
    std::string value;
    /// Stable identity of a `set_model` row group (the first node of the group). Empty means the
    /// row is matched by position.
    std::string model_key;
    std::unordered_map<std::string, std::string> attrs;
    bool visible = true;
    bool disabled = false;
    bool checked = false;
    /// Snapshot of the `checked` content attribute on a checkbox or radio, and of `selected` on an
    /// option, taken when the element is parsed. `:default` reads this. A later live toggle
    /// (`Node::checked`, `select_commit` rewriting the attribute) must not redefine it.
    bool default_selected = false;
    /// True when the DOM had a whitespace-only text child that convert dropped. Selectors 3
    /// `:empty` must still see that whitespace (Chrome); ShareKey fingerprints the same bit.
    bool whitespace_child = false;
    /// The dropped whitespace included a text node after the last kept child. Chrome's Range
    /// over the element's text nodes then covers the elements in between (album's select
    /// sits between "相册" and the newline before `</label>`).
    bool trailing_whitespace = false;
    std::vector<std::unique_ptr<Node>> children;

    // --- Input-frame: Interaction writes these each frame; cascade matches :hover / :focus. ---
    bool hovered = false;
    bool active = false;
    bool focused = false;
    /// Selectors 4 `:focus-visible`. Set with `focused` when the focus came from the keyboard
    /// (or the control always shows a focus indicator, e.g. a text field). Cleared on blur.
    bool focus_visible = false;
    /// True on every ancestor of the focused element. `Interaction::focus` maintains the chain.
    /// `:focus-within` matches `focused` or this flag, so it does not walk descendants.
    bool focus_within = false;
    /// HTML dialog top-layer / `:modal`. Set when a `<dialog>` is open as modal (declarative
    /// `open` is treated as modal until a show()/showModal() host split lands). Drives `:modal`
    /// matching and `::backdrop` paint.
    bool modal = false;

    // --- Layout-written: used sizes and scroll, read by cascade (@container) and paint. ---
    float scroll_x = 0;
    float scroll_y = 0;
    /// CSS Scroll Anchoring: last selected anchor inside this scroll container, and that
    /// anchor's content-block offset relative to this scroller. Cleared when unused.
    Node* scroll_anchor = nullptr;
    float scroll_anchor_y = 0;
    /// Query container content-box size written by layout (CSS Containment 3). Cascade reads
    /// these when evaluating `@container`; unknown (`query_size_known == false`) fails the query.
    float query_width = 0;
    float query_height = 0;
    bool query_size_known = false;

    // --- Cascade: interned computed style and generated pseudo nodes. ---
    std::shared_ptr<const StyleMap> computed;
    /// Computed style of the generated `::before` / `::after` / `::placeholder` / `::marker`.
    /// A slot is null when no declaration block matched that pseudo-element on this node.
    std::array<std::shared_ptr<const StyleMap>, 4> pseudo;
    /// The generated element that carries `pseudo[slot]`. It is deliberately not in `children`:
    /// `:empty`, `:first-child` and every tree walk the application can see stay about real
    /// elements. `::before` / `::after` / `::marker` layout as boxes; `::placeholder` only
    /// supplies the style and the characters a text control paints when it holds no value.
    std::array<std::unique_ptr<Node>, 4> pseudo_node;
    VisualUsed used;
    /// Committed `::selection` used colours (highlight inheritance applied). Null / flags 0 →
    /// paint uses the UA pair. Never a layout box; not in `pseudo` / `pseudo_node`.
    std::unique_ptr<SelectionStyle> selection;
    /// Cascaded `::backdrop` style for a modal top-layer element. Null when no rule matched or
    /// the element is not modal. Painter fills the viewport from this map; no layout box.
    std::shared_ptr<const StyleMap> backdrop;

    // --- Edit / caret: form-control local caret and the document Selection. ---
    int caret = 0;
    /// Other end of a form-control / CE-host range. Collapsed when equal to `caret`.
    std::size_t select_anchor = 0;
    /// Legacy CE style-bit projection. Empty. Tree children are the CE authority.
    std::string format;

    const std::string* style(const Atom& key) const {
        if (!computed) {
            return nullptr;
        }
        auto it = computed->find(key);
        if (it == computed->end() || it->second.empty()) {
            return nullptr;
        }
        return it->second.ptr();
    }
    /// `key` is a string literal. The first call interns it; later calls from the same literal
    /// reuse that atom so the map probe does not hash the characters again.
    const std::string* style(const char* key) const {
        if (!computed || key == nullptr) {
            return nullptr;
        }
        if (const Atom* cached = literal_atom(key)) {
            return style(*cached);
        }
        return style(Atom::intern(key));
    }
};

/// The handler name written in a standard HTML event attribute: the value of `on<kind>`
/// (`onclick` -> kind "click"). Handler names are the only link between HTML and `App::bind`.
///
/// Parsing keeps every `on*` attribute - they are ordinary entries of `Node::attrs` - while
/// dispatching only walks the kinds the engine actually wires (`click` / `dblclick` / `input` /
/// `change` / `keydown` / `keyup` / `beforeunload` / `scroll` / `wheel` / `drop` / `focus` /
/// `blur` / `contextmenu` / `auxclick` / `pointermove` / `mousemove` / `pointerdown` /
/// `pointerup` / `pointercancel` / `mouseenter` / `mouseleave` / `mouseover` / `mouseout` /
/// `pointerenter` / `pointerleave` / `dragstart` / `drag` / `dragenter` / `dragover` /
/// `dragleave` / `dragend` / `visibilitychange` / `resize` / `orientationchange` / `copy` /
/// `cut` / `paste` / `select` / `gotpointercapture` / `lostpointercapture` / `submit` /
/// `invalid` / `beforeinput` / `compositionstart` / `compositionupdate` / `compositionend`).
/// `drop` is both an OS file drop and an element drop; `is_drop()` / `is_drag()` tell them apart.
/// An `onfoo` nobody implements does nothing, as in HTML.
///
/// @return nullptr when the element does not carry `on<kind>`, or carries it empty.
inline const std::string* handler_attribute(const Node* node, std::string_view kind) {
    if (node == nullptr || kind.empty()) {
        return nullptr;
    }
    std::string name("on");
    name.append(kind);
    const auto it = node->attrs.find(name);
    if (it == node->attrs.end() || it->second.empty()) {
        return nullptr;
    }
    return &it->second;
}

/// CSS Pseudo-Elements. The order is the order of `Node::pseudo` (not DOM paint order:
/// the box tree places `Marker` before `Before`).
enum class PseudoElement : std::uint8_t {
    Before = 0,
    After = 1,
    Placeholder = 2,
    Marker = 3,
    Count = 4
};

/// One endpoint of the document Selection: a node and a UTF-8 byte offset into that node's
/// text (a `#text`, a form control's value, or a host flatten). Offset on an element is a
/// child index when the node is not a text host.
struct BoundaryPoint {
    Node* node = nullptr;
    std::size_t offset = 0;
};

/// Document Selection: two boundary points. `node` / `start` / `end` are the same-node
/// projection (form control, CE host, or a single `#text`): `start <= end` into that node's
/// text. A cross-element range leaves `node` null; paint then uses `anchor` / `focus`.
struct SelectionRange {
    BoundaryPoint anchor;
    BoundaryPoint focus;
    Node* node = nullptr;
    std::size_t start = 0;
    std::size_t end = 0;
};

inline bool selection_empty(const SelectionRange& r) {
    if (r.node != nullptr) {
        return r.start == r.end;
    }
    return r.anchor.node == nullptr ||
           (r.anchor.node == r.focus.node && r.anchor.offset == r.focus.offset);
}

/// One large flat list whose stamp keeps `slots` rows and two block spacers for the rest.
/// The model is not retained: `values` is the field snapshot taken at `set_model`.
struct ListWindow {
    std::string host_id;
    std::size_t row_count = 0;
    std::size_t first = 0;
    std::size_t slots = 32;
    float row_px = 0.f;
    std::vector<std::string> keys;
    std::vector<std::string> row_key;
    /// Row-major, `keys.size()` entries per row.
    std::vector<std::string> values;
};

struct Document {
    /// One author stylesheet in document order: a `<link rel="stylesheet">` names a file, a
    /// `<style>` element carries its text. They share one list because cascade source order is
    /// document order (CSS Cascade 5 section 6.4), so a `<style>` before a `<link>` has to win
    /// the tie against it.
    struct StylesheetSource {
        std::string href;  ///< Relative to Document::source_path; empty for an inline sheet.
        std::string text;  ///< `<style>` contents; used when `href` is empty.
    };
    std::string source_path;
    std::string title;
    std::vector<StylesheetSource> stylesheets;
    std::unique_ptr<Node> root;
    std::unordered_map<std::string, Node*> ids;
    /// C-tier / test inject; not an A-tier App API. Paint reads this before each frame.
    SelectionRange selection;
    /// Large flat lists keep a scroll window here. Empty for every list the stamp built in full.
    std::vector<ListWindow> list_windows;
    /// Paint and DOM clocks for this document. Heap-stable across moves of `Document`.
    std::unique_ptr<DamageTracker> damage;
};

/// The frame-thread paint clock (`LAYOUT.md` section 15: styles, layout and painting all run on
/// one thread and none of them locks). Anything that can change what a frame paints stamps it.
/// The clock and the damage rectangles live on the `Document` that owns the tree, so a second
/// document alive at the same time does not observe them. Layout tests that compile
/// `paint_map.cpp` without `document.cpp` still see this header-only type.

/// A rectangle (device / world px) the painter must re-record.
struct PaintRect {
    float x = 0;
    float y = 0;
    float w = 0;
    float h = 0;
};

inline constexpr std::size_t kMaxDamageRects = 8;

/// Add one rectangle to a damage set. Touching (not only overlapping) rectangles merge, so two
/// edits to the same box a frame apart do not accumulate into entries that grow without bound.
inline void merge_paint_rect(std::vector<PaintRect>& rects, const PaintRect& rect) {
    // A box the placement walk culled is parked at `world_y = -1.0e10f` (`layout/paint_map.cpp`); it
    // is not on screen, and taking its rectangle literally produced one rect that covered the page
    // and swallowed every localized one. The choke point refuses those coordinates.
    if (rect.w <= 0.f || rect.h <= 0.f || rect.y <= -1.0e9f || rect.h >= 1.0e9f) {
        return;
    }
    for (PaintRect& d : rects) {
        if (d.x <= rect.x + rect.w && rect.x <= d.x + d.w && d.y <= rect.y + rect.h &&
            rect.y <= d.y + d.h) {
            const float x0 = d.x < rect.x ? d.x : rect.x;
            const float y0 = d.y < rect.y ? d.y : rect.y;
            const float x1 = (d.x + d.w) > (rect.x + rect.w) ? (d.x + d.w) : (rect.x + rect.w);
            const float y1 = (d.y + d.h) > (rect.y + rect.h) ? (d.y + d.h) : (rect.y + rect.h);
            d = PaintRect{x0, y0, x1 - x0, y1 - y0};
            return;
        }
    }
    rects.push_back(rect);
}

/// Paint stamp, DOM stamp, and this frame's damage rectangles for one document.
/// Header-only so `paint_map.cpp` can record damage without linking `document.cpp`.
class DamageTracker {
public:
    std::uint64_t paint_stamp() const { return paint_stamp_; }
    std::uint64_t dom_stamp() const { return dom_stamp_; }
    bool full() const { return full_; }
    const std::vector<PaintRect>& rects() const { return rects_; }
    const std::vector<PaintRect>& overlay() const { return overlay_; }
    const std::vector<const Node*>& nodes() const { return nodes_; }

    void note_paint_change() {
        ++paint_stamp_;
        full_ = true;
    }

    void note_paint_change_at(const Node* node) {
        ++paint_stamp_;
        if (full_ || node == nullptr) {
            return;
        }
        nodes_.push_back(node);
    }

    void add_paint_damage(const PaintRect& rect) {
        if (full_) {
            return;
        }
        merge_paint_rect(rects_, rect);
        if (rects_.size() > kMaxDamageRects) {
            full_ = true;
            rects_.clear();
        }
    }

    void add_paint_overlay_damage(const PaintRect& rect) {
        if (full_) {
            return;
        }
        merge_paint_rect(overlay_, rect);
        if (overlay_.size() > kMaxDamageRects) {
            full_ = true;
            rects_.clear();
            overlay_.clear();
        }
    }

    void note_paint_damage_rect(const PaintRect& rect) {
        ++paint_stamp_;
        add_paint_damage(rect);
    }

    void note_dom_change() { ++dom_stamp_; }

    void clear_nodes() { nodes_.clear(); }

    void clear() {
        full_ = false;
        rects_.clear();
        overlay_.clear();
        nodes_.clear();
    }

    PaintRect bounds() const {
        if (rects_.empty()) {
            return PaintRect{};
        }
        PaintRect d = rects_.front();
        for (std::size_t i = 1; i < rects_.size(); ++i) {
            const PaintRect& r = rects_[i];
            const float x0 = d.x < r.x ? d.x : r.x;
            const float y0 = d.y < r.y ? d.y : r.y;
            const float x1 = (d.x + d.w) > (r.x + r.w) ? (d.x + d.w) : (r.x + r.w);
            const float y1 = (d.y + d.h) > (r.y + r.h) ? (d.y + d.h) : (r.y + r.h);
            d = PaintRect{x0, y0, x1 - x0, y1 - y0};
        }
        return d;
    }

private:
    std::uint64_t paint_stamp_ = 1;
    std::uint64_t dom_stamp_ = 1;
    bool full_ = true;
    std::vector<PaintRect> rects_;
    std::vector<PaintRect> overlay_;
    std::vector<const Node*> nodes_;
};

inline DamageTracker& damage_of(Document& doc) {
    if (doc.damage == nullptr) {
        doc.damage = std::make_unique<DamageTracker>();
    }
    return *doc.damage;
}

inline const DamageTracker& damage_of(const Document& doc) {
    return damage_of(const_cast<Document&>(doc));
}

/// Point the document element at this document's tracker. Call after replacing `root`.
inline void bind_document_damage(Document& doc) {
    if (doc.root != nullptr) {
        doc.root->damage = &damage_of(doc);
    }
}

inline DamageTracker* tracker_of(const Node* node) {
    for (const Node* n = node; n != nullptr; n = n->parent) {
        if (n->damage != nullptr) {
            return n->damage;
        }
    }
    return nullptr;
}

inline std::uint64_t paint_stamp(const Document& doc) { return damage_of(doc).paint_stamp(); }
inline std::uint64_t paint_stamp(const Node* node) {
    const DamageTracker* tracker = tracker_of(node);
    return tracker != nullptr ? tracker->paint_stamp() : 0;
}
inline void note_paint_change(Document& doc) { damage_of(doc).note_paint_change(); }
inline void note_paint_change(const Node* node) {
    if (DamageTracker* tracker = tracker_of(node)) {
        tracker->note_paint_change();
    }
}
inline void note_paint_change_at(const Node* node) {
    if (DamageTracker* tracker = tracker_of(node)) {
        tracker->note_paint_change_at(node);
    }
}
inline void note_paint_damage_rect(const Node* node, const PaintRect& rect) {
    if (DamageTracker* tracker = tracker_of(node)) {
        tracker->note_paint_damage_rect(rect);
    }
}
inline void note_paint_damage_rect(Document& doc, const PaintRect& rect) {
    damage_of(doc).note_paint_damage_rect(rect);
}
inline void add_paint_damage(const Node* node, const PaintRect& rect) {
    if (DamageTracker* tracker = tracker_of(node)) {
        tracker->add_paint_damage(rect);
    }
}
inline void add_paint_damage(Document& doc, const PaintRect& rect) {
    damage_of(doc).add_paint_damage(rect);
}
inline void add_paint_overlay_damage(const Node* node, const PaintRect& rect) {
    if (DamageTracker* tracker = tracker_of(node)) {
        tracker->add_paint_overlay_damage(rect);
    }
}
inline bool paint_damage_full(const DamageTracker& tracker) { return tracker.full(); }
inline const std::vector<PaintRect>& paint_damage_rects(const DamageTracker& tracker) {
    return tracker.rects();
}
inline const std::vector<PaintRect>& paint_overlay_damage_rects(const DamageTracker& tracker) {
    return tracker.overlay();
}
inline PaintRect paint_damage(const DamageTracker& tracker) { return tracker.bounds(); }
inline const std::vector<const Node*>& paint_damage_nodes(const DamageTracker& tracker) {
    return tracker.nodes();
}
inline void clear_paint_damage_nodes(DamageTracker& tracker) { tracker.clear_nodes(); }
inline void clear_paint_damage(DamageTracker& tracker) { tracker.clear(); }

inline std::uint64_t dom_stamp(const Document* doc) {
    return doc != nullptr ? damage_of(*doc).dom_stamp() : 0;
}
inline std::uint64_t dom_stamp(const Document& doc) { return damage_of(doc).dom_stamp(); }
inline std::uint64_t dom_stamp(const Node* node) {
    const DamageTracker* tracker = tracker_of(node);
    return tracker != nullptr ? tracker->dom_stamp() : 0;
}
inline void note_dom_change(Document& doc) { damage_of(doc).note_dom_change(); }
inline void note_dom_change(const Node* node) {
    if (DamageTracker* tracker = tracker_of(node)) {
        tracker->note_dom_change();
    }
}
/// Same-node projection of document Selection: two boundary points on @p node, clamped to
/// `text.size()`, with `start <= end`. Callers writing an editable range must sync
/// `text = value` first. A null node clears. Dirties paint only.
void set_selection(Document& doc, Node* node, std::size_t start, std::size_t end);

/// Clears `doc.selection` and dirties paint when it was non-empty.
void clear_selection(Document& doc);

/// Parses an HTML5 file with lexbor into a zui tree. Fills id table.
/// @return false on I/O or parse failure; @p error is set.
bool load_file(std::string_view path, Document& out, std::string& error);

/// Label text of an `<option>` (own text, else first `#text` child).
std::string option_label(const Node* option);

/// Option value attribute, or the label when the attribute is absent (HTML).
std::string option_value(const Node* option);

/// Writes `selected` / `checked` on options, and mirrors the chosen label into `select->text`
/// and value into `select->value`. Null @p option clears the control.
void select_commit(Node* select, Node* option);

/// Picks the `selected` option, else the first `<option>`, and commits it.
void select_init(Node* select);

/// Selects the first option whose value equals @p value (else whose label equals it).
/// @return false when @p select is not a `<select>` or no option matched.
bool select_set_value(Node* select, std::string_view value);

Node* find(Document& doc, std::string_view id);

/// Process-wide cache from a string-literal address to its interned atom. Null when the table
/// is full; the caller interns that one key itself.
const Atom* literal_atom(const char* text);

Node* find_tag(Node* node, std::string_view tag);

/// Parses @p path with the HTML fragment parsing algorithm, context element
/// `body`, and appends those nodes under @p into. A full-document load stays a
/// document parse; the context is `body` even when @p into is another tag.
/// Duplicate ids under @p into (including orphans missing from the id map) are
/// replaced. @p before_remove runs on each node about to leave the tree so the
/// app can detach layout boxes first. Stylesheet hrefs from the fragment are
/// appended to host.stylesheets.
/// Parses @p path and appends its body children under @p into. Duplicate ids replace the
/// existing node. With @p replace_children, every child @p into already has leaves first, so the
/// fragment is the target's whole content (`innerHTML = …`).
bool attach_fragment(Document& host, Node* into, std::string_view path, std::string& error,
                     const std::function<void(Node*)>& before_remove = {},
                     bool replace_children = false);

/// Unregisters ids and erases @p node from its parent. Identity is the node
/// pointer, not the id map.
bool remove_node(Document& doc, Node* node);

bool remove_tree(Document& doc, std::string_view id);

/// Walk @p root and collect every node whose node->id equals @p id (map-independent).
void collect_nodes_with_id(Node* root, std::string_view id, std::vector<Node*>& out);

bool set_field(Document& doc, std::string_view id, std::string_view field, std::string_view value);

bool add_class(Document& doc, std::string_view id, std::string_view class_name);

bool remove_class(Document& doc, std::string_view id, std::string_view class_name);

/// Reconciles the <template> under @p id. Equal rendered rows are kept. @p row_key, when it
/// returns a non-empty string, matches rows by that key instead of by position. @p retiring is
/// called on a node just before it leaves the tree. @p unchanged is set when the child list and
/// every rendered value are the same as before.
bool apply_model(Document& doc, std::string_view id, size_t row_count,
                 const std::function<std::string(size_t, std::string_view)>& field,
                 std::string& error, const std::function<std::string(size_t)>& row_key = {},
                 const std::function<void(Node*)>& retiring = {}, bool* unchanged = nullptr);

bool apply_model_on_node(Document& doc, Node* host, size_t row_count,
                         const std::function<std::string(size_t, std::string_view)>& field,
                         std::string& error, const std::function<std::string(size_t)>& row_key = {},
                         const std::function<void(Node*)>& retiring = {},
                         bool* unchanged = nullptr);

/// The whole model, not only its rows: the row stamp, each row's nested `with_children` host
/// (RFC_MODEL.md Q1), and - when @p model is a `TableModel` - the per-row column stamp. Kept rows
/// are reconciled, so a nested child that did not change creates no boxes. A null @p host is an
/// error. Failures leave @p error set: a missing host, a missing `<template>`, a column template
/// whose cell count is neither 1 nor `column_count()`, or the nesting depth limit.
bool apply_list_model(Document& doc, std::string_view id, const ListModel& model,
                      std::string& error, const std::function<void(Node*)>& retiring = {},
                      bool* unchanged = nullptr, std::vector<Node*>* sync_hosts = nullptr);
bool apply_list_model_on_node(Document& doc, Node* host, const ListModel& model,
                              std::string& error,
                              const std::function<void(Node*)>& retiring = {},
                              bool* unchanged = nullptr,
                              std::vector<Node*>* sync_hosts = nullptr);

/// Seeks a windowed list so the row under @p scroll_y is inside the stamped window.
/// @p row_px is one row's border-box block size; 0 leaves the window where it is and only
/// refreshes spacer heights when a previous measure exists. Returns true when the DOM changed.
bool sync_list_window(Document& doc, std::string_view host_id, float scroll_y, float row_px,
                      const std::function<void(Node*)>& retiring = {});

Node* find_template(Node* host);

}  // namespace html
}  // namespace zui

#include "html/node_query.h"

#endif  // ZUI_HTML_DOCUMENT_H
