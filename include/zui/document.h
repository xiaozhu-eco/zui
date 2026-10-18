#ifndef ZUI_DOCUMENT_H
#define ZUI_DOCUMENT_H

#include <zui/element_box.h>
#include <zui/event.h>
#include <zui/export.h>
#include <zui/host_types.h>
#include <zui/model.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace zui {

/// Opaque id for Document::set_timeout / Document::set_interval. Zero is never a live timer.
using TimerId = std::uint64_t;

/// Result of `Document::set_model`. `reason` is set when `ok` is false.
struct ModelBind {
    bool ok = false;
    std::string reason;
    explicit operator bool() const noexcept { return ok; }
};

/// The text caret painted for one element. CSS px, the same space as `ElementBox::border`.
/// The bar is the insertion projection; it is not a box in the line.
struct TextCaret {
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;
};

/// One loaded page: its tree, handlers, classes, and timers.
///
/// A child opened with Window::open_document is another Document. Its handlers are consulted
/// first; a missing name falls through to the root document. Timers queued here are cancelled
/// when that child closes. load, fragments, set_model, and set_theme on a child return false:
/// open_document loads it.
class ZUI_EXPORT Document {
    friend class App;
    friend class Window;

public:
    using Handler = std::function<void(Event&)>;

    /// Loads an HTML5 document (.html or .zui). Structure, ids, event
    /// attribute names, and stylesheet links.
    /// May be called before or after bind().
    /// @return false when the file is missing or the document is invalid.
    bool load(std::string_view path);

    /// Parses an HTML fragment and appends its body children under <body>.
    /// Relative @p path is resolved against the last load() directory.
    /// Duplicate ids replace the existing node. Linked stylesheets are added.
    /// Scope: Children(parent) - the target's child nodes; siblings are untouched.
    /// Cost: created boxes stay inside the fragment's own boxes (docs/PERF.md section 4).
    /// @return false when the file is missing or no document is loaded.
    bool load_fragment(std::string_view path);

    /// Like load_fragment(path), appended under the element @p into_id.
    bool load_fragment(std::string_view path, std::string_view into_id);

    /// Like load_fragment(path, into_id), but the target's current children leave first: the
    /// fragment becomes the whole content of @p into_id, the way `innerHTML = …` does. A view
    /// that re-renders into the same host does not keep the nodes the new render dropped.
    /// Duplicate ids elsewhere in the document still give way to the new nodes.
    /// Scope: Children(parent) - the target's child nodes; siblings are untouched.
    /// Cost: as load_fragment, plus one child-list resync of the target.
    bool replace_fragment(std::string_view path, std::string_view into_id);

    /// Removes the element @p id and its descendants. No-op if missing.
    /// Scope: Children(parent) - removal only.
    /// Cost: no boxes are created; only the parent's children are resynced (docs/PERF.md
    ///       section 4).
    bool unload_fragment(std::string_view id);

    /// Applies a theme pack named @p name: class "theme-{name}" on <html>,
    /// and if "{name}.css" exists next to the document it becomes the extra
    /// theme stylesheet (replacing any previous one).
    /// Scope: the whole document - the <html> class plus one stylesheet. This is the only method
    ///        allowed a document-wide cascade (docs/PERF.md section 4).
    /// Cost: relower stays 0 while the theme leaves display/position alone; most boxes are
    ///       re-interned, which is what a theme is. Re-applying the applied theme is a no-op
    ///       (PERF.md I2).
    bool set_theme(std::string_view name);

    /// Registers @p handler_name, i.e. the HTML value such as onclick="on_save".
    /// A repeated name replaces the previous handler.
    void bind(std::string_view handler_name, Handler handler);

    /// Removes a binding; a no-op when @p handler_name is not bound.
    void unbind(std::string_view handler_name);

    /// Writes a data field on the element with @p id.
    /// Known fields: text, value, checked, visible, disabled, src, href, scroll_top, scroll_left,
    /// insert, select, and `data-*` / `aria-*` (the prefix plus a name). `style` and any other
    /// attribute are rejected. There is no generic set_attr.
    /// `insert` writes @p value at the caret of a text entry or `contenteditable` host, replacing
    /// a non-empty selection on that host, and leaves the caret after the inserted text. `insert`
    /// on any other element is rejected. `select` is `start,end`, a half-open UTF-8 byte range.
    /// On a text entry or `contenteditable` host the bytes are that host's value and the caret is
    /// `end`. On any other laid-out element the bytes are that element's innerText: the range is
    /// Document Selection (the highlight), and the element does not become an insert target.
    /// `value` and `text` on a text entry keep the caret on the same character (shared prefix,
    /// else distance from the end). They do not move it to the end of the new string.
    /// `text` updates an existing direct `#text` child when the element has one (not only the
    /// element-owned text slot). `disabled` syncs the Node flag and the HTML attribute for
    /// `:disabled`. `src` writes `attrs["src"]` and reloads a replaced image; `href` writes
    /// `attrs["href"]`. There is no generic set_attr.
    /// A scroll field is clamped to the element's scrollable range and lands in the world before
    /// the frame paints, exactly as a wheel scroll does - so setting it never scrolls a scroll
    /// container past its own content.
    /// Scope: Node(id) - this element's own computed style and its own box. A `visible` write is
    ///        still Node(id); its diff lands on Children(parent). `scroll_*` is Box(scroll box).
    /// Cost: paint-only changes create no boxes and lay out nothing new; a change to a property
    ///       the painter reads from the interned LayoutStyle (border-radius / border-*-style)
    ///       re-interns this element's own box and the text / anonymous / generated boxes it
    ///       owns. Writing the same value is a no-op (PERF.md I2). See docs/PERF.md section 4.
    /// @return false when @p id is not in the loaded document.
    bool set(std::string_view id, std::string_view field, std::string_view value);

    /// Reconciles the <template> under @p id against the model. Placeholders {{name}} in the
    /// template text and attributes are filled from ListModel::value; TableModel binds the same
    /// way via column keys, and each row's own cells come from TableModel::cell_value.
    /// ListModel::with_children may hand out a nested model for a row; the engine stamps it into
    /// the parent of the row's inner <template> and reconciles it the same way, so a kept row
    /// still tracks its child list. Rows match by ListModel::row_key when it returns a non-empty
    /// key and by position otherwise; equal rendered rows are kept, so an inserted or moved row
    /// does not re-stamp the rest.
    /// Scope: Children(id) only - the host's child nodes, never the host's parent or siblings.
    /// Cost: the rebuild root is @p id itself, and created boxes stay inside the rows whose
    ///       rendered values changed; re-applying an equal model is a no-op (PERF.md I2, I4, I5).
    /// @return ok == false when @p id or its template is missing, or when a column template has
    ///         neither one cell nor exactly column_count() of them. `reason` names that failure.
    ModelBind set_model(std::string_view id, const ListModel& model);

    /// Reads a data field. Empty when @p id is missing.
    /// Known fields: text, value, checked, visible, disabled, src, href, class,
    /// scroll_top/scroll_y, scroll_left/scroll_x (positions), caret (UTF-8 byte offset on a text
    /// entry), selection (the selected substring, empty when collapsed or on another node),
    /// `data-*` / `aria-*`, and `validity` (`invalid` when `aria-invalid` is true, otherwise
    /// `valid`).
    /// Reads never flush (PERF.md I8). Use measure() for layout geometry.
    std::string get(std::string_view id, std::string_view field);

    /// Opaque id for `watch` / `unwatch`. Zero is never a live subscription.
    using WatchId = std::uint64_t;

    /// Runs @p fn when the element @p id, or the list row with that id, changes.
    /// A sibling change does not run it. `unwatch` stops it. An equal `set_model` does not run it.
    /// @return 0 when @p id or @p fn is empty.
    WatchId watch(std::string_view id, std::function<void(std::string_view id)> fn);

    /// Drops a subscription. No-op for an unknown id.
    void unwatch(WatchId id);

    /// CSSOM View-style measurement for @p id. Flushes pending style/layout
    /// first, so the result is current even inside a handler, the way
    /// `getBoundingClientRect()` forces layout. Never dispatches input events.
    /// An empty result means @p id is missing or has no principal box; there is
    /// no box to read in that case.
    std::optional<ElementBox> measure(std::string_view id);

    /// UTF-8 byte offset into the text laid out for @p id nearest to the CSS-px point
    /// (@p x, @p y). The same projection the caret uses: the line under the point, then the
    /// nearest glyph edge on that line. A point past the last glyph of a short line is the
    /// offset after that line. Empty when @p id is missing or has no laid-out text.
    /// Flushes pending style and layout first, the same way `measure` does.
    std::optional<std::size_t> text_offset_at(std::string_view id, float x, float y);

    /// Paint the text caret at UTF-8 byte @p offset into the text laid out for @p id.
    /// The bar is drawn from the glyph edge; the element's text does not change, so the line
    /// does not move. An empty @p id or an empty @p offset clears it. One caret at a time.
    void set_text_caret(std::string_view id, std::optional<std::size_t> offset);

    /// The caret `set_text_caret` is painting, after a layout flush. Empty when none is set
    /// or the element has no laid-out text.
    std::optional<TextCaret> text_caret_rect();

    /// Adds a CSS class token on @p id. Appearance is defined in CSS.
    /// Scope: Node(id) when the class only affects painted properties, Subtree(id) when it affects
    ///        geometry or inheritance (docs/PERF.md section 2.4).
    /// Cost: a token the element already carries is a no-op (PERF.md I2). A paint-only class
    ///       creates no boxes and lays out nothing; a class the painter reads from the interned
    ///       LayoutStyle re-interns the affected boxes (docs/PERF.md section 4).
    /// @return false when @p id is not in the loaded document.
    bool add_class(std::string_view id, std::string_view class_name);

    /// Drops a CSS class token on @p id. A no-op when the class is absent.
    /// Scope: Node(id) when the class only affects painted properties, Subtree(id) when it affects
    ///        geometry or inheritance (docs/PERF.md section 2.4).
    /// Cost: a token the element does not carry is a no-op (PERF.md I2); otherwise the same as
    ///       add_class (docs/PERF.md section 4).
    /// @return false when @p id is not in the loaded document.
    bool remove_class(std::string_view id, std::string_view class_name);

    /// Scrolls the nearest scrollable ancestor so @p id's box is in view (CSSOM View block
    /// alignment). Uses current layout boxes; scroll-margin/padding are ignored in v1.
    /// @return false when @p id is missing or no scrollable ancestor has range.
    bool scroll_into_view(std::string_view id, ScrollAlign block = ScrollAlign::Nearest);

    /// Programmatically focuses the element @p id (fires `onblur`/`onfocus`, updates IME).
    /// @return false when @p id is missing or the element is not focusable.
    bool focus(std::string_view id);

    /// Clears the focused element (fires `onblur`, disables IME). @return false when nothing
    /// was focused.
    bool blur();

    /// Queues @p fn to run once at the start of the next frame, before style and
    /// layout. Use it to paint a loading state in the click handler and do the heavy
    /// work on the following frame.
    void defer(std::function<void()> fn);

    /// Runs @p fn once on the main loop after @p delay_ms (same phase as defer,
    /// before style). Skipped while a host modal pick is nested. @return 0 if @p fn
    /// is empty.
    TimerId set_timeout(std::function<void()> fn, int delay_ms);

    /// Runs @p fn repeatedly every @p interval_ms on the main loop (same phase as
    /// defer). Skipped while a host modal pick is nested. @return 0 if @p fn is empty.
    TimerId set_interval(std::function<void()> fn, int interval_ms);

    /// Cancels a timeout or interval. No-op for unknown or already-fired ids.
    void clear_timer(TimerId id);

    ~Document();

    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
    Document(Document&&) = delete;
    Document& operator=(Document&&) = delete;

private:
    class Impl;
    explicit Document(std::unique_ptr<Impl> impl);
    /// The shared body of `load_fragment` / `replace_fragment`; only `replace_children` differs.
    bool fragment_into(std::string_view path, std::string_view into_id, bool replace_children);
    std::unique_ptr<Impl> impl_;
};

}  // namespace zui

#endif  // ZUI_DOCUMENT_H
