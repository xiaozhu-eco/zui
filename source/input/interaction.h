#ifndef ZUI_INPUT_INTERACTION_H
#define ZUI_INPUT_INTERACTION_H

// Window-free input (TESTPLAN section 14). App and zui_difftest call this; neither the
// driver nor a test re-implements wheel, clamp, hit, or hover.
//
// Wheel deltas use the CDP sign: positive deltaY increases scrollTop. A window backend
// converts its own notch units before calling in. delta_mode 0 is pixels. delta_mode 1
// is lines (line height times the delta) and is an engine self-check, not a browser golden.

#include "html/document.h"
#include "input/edit_model.h"
#include "layout/engine.h"

#include <zui/event.h>
#include <zui/host_types.h>

#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace zui {
namespace input {

struct ScrollRange {
    float min_x = 0;
    float max_x = 0;
    float min_y = 0;
    float max_y = 0;
};

/// Client rectangle in the same space as the border box: origin inside the border,
/// size is clientWidth / clientHeight (padding included, scrollbar excluded).
struct ClientRect {
    float x = 0;
    float y = 0;
    float w = 0;
    float h = 0;
};

/// `view_w` / `view_h` override the border box when the scrollport is the viewport
/// (the root box's border box is the content, not the window). Negative keeps the box.
ScrollRange scroll_range(const layout::Box& box, float view_w = -1.f, float view_h = -1.f);
ClientRect client_rect(const layout::Box& box, float scrollbar_thickness);

/// `documentElement.clientWidth/Height` is the viewport, minus a scrollbar on the other
/// axis when that axis overflows. It is not the content height of the root box.
void document_client_size(float viewport_w, float viewport_h, float scroll_w, float scroll_h,
                          float scrollbar_thickness, float border_horizontal, float border_vertical,
                          float& out_w, float& out_h);

/// When @p box is the document root, fills @p view_w / @p view_h with the viewport client
/// size. The root border box is the content, so using it as the scrollport clamps every
/// document scroll to 0. Returns true when @p box is the root.
bool document_scroll_view(const layout::LayoutEngine& layout, const layout::Box& box,
                          float scrollbar_thickness, float& view_w, float& view_h);

/// Writes the clamped offset onto the box's node. One entry for the scroll step and the wheel.
/// @return true when either axis moved after clamping.
bool set_scroll(layout::Box& box, float x, float y, float view_w = -1.f, float view_h = -1.f);

/// After a scroll has been written and `sync_world` has published it: lay out again when the
/// document uses `content-visibility: auto`, so a box that entered the viewport takes its real
/// size before paint. A document that never used the property returns without laying out.
/// Clamps scrollports whose content shrank and lays out once more when that clamp moved.
void settle_content_visibility(layout::LayoutEngine& layout, html::Document& doc,
                               float scrollbar_thickness);

/// CSS Scroll Anchoring: after layout, keep each scrolled container's anchor visually stable
/// when content above it grows or shrinks. Uses `Node::scroll_anchor` / `scroll_anchor_y`.
/// @return true when any scroller's offset changed (caller should re-publish world geometry).
bool adjust_scroll_anchors(layout::Box& root);

/// Aligns @p target into the nearest ancestor with overflow scroll/auto and a non-zero range
/// (CSSOM View). The same alignment is applied on both axes. `scroll-margin` on the target
/// and `scroll-padding` on the scrollport inset that alignment (physical longhands, or the
/// shorthand when the longhand is unset). @p scrolled receives the node whose offset changed
/// when non-null.
/// @return true when a scrollable ancestor moved.
bool scroll_into_view(layout::LayoutEngine& layout, html::Node& target, ScrollAlign block,
                      float scrollbar_thickness = 15.f, html::Node** scrolled = nullptr);

/// delta_mode 1: pixels = lines * line_height. The line height is the caller's used line box.
float line_mode_pixels(float line_height_px, float delta_lines);

bool is_editable(const html::Node* node);

/// `contenteditable` other than the literal `false`. An empty attribute counts as true.
bool is_content_editable(const html::Node* node);

/// One frame of text-input edits. `frame.cpp` fills this from the window; tests call
/// `Interaction::apply_editing` with the same struct.
struct EditInput {
    std::string text;
    int backspace = 0;
    int del = 0;
    int left = 0;
    int right = 0;
    /// Vertical caret moves: one laid-out line up or down, which for a textarea is a soft wrap as
    /// often as a `\n`. Only `apply_editing` can answer these - they need the laid-out lines.
    int up = 0;
    int down = 0;
    /// Page moves: a page of laid-out lines when the control scrolls its own text, and one page of
    /// the surrounding scroller when it does not (a textarea that grows with its value shows
    /// everything, so the page is what moves - the same thing the key does outside a control).
    int page_up = 0;
    int page_down = 0;
    int home = 0;
    int end = 0;
    /// Shift+move: the caret moves and the selection's other end stays where it was, which is how
    /// every text control grows a selection from the keyboard.
    bool extend = false;
    /// Ctrl / Cmd with a move: a word for Left / Right, the whole value's ends for Home / End.
    /// Every browser reads the same modifier this way, and the frame hands it down with the counts.
    bool word = false;
    bool enter = false;
    bool enter_prevented = false;
    bool composing = false;
    /// Live preedit while @c composing. Replaces the open composition range.
    /// Empty @c text on a frame that is not composing drops that range. A commit is
    /// @c text with @c composing false, and it is inserted once.
    std::string composition;
};

/// True for the same controls Tab / pointer focus accept (a/button/input/textarea/select/tabindex).
bool is_focusable(const html::Node* node);

/// The caret's box inside an editable control, in world CSS px: the top-left of the bar and its
/// height. The caret sits on the laid-out line that begins at or before it - a soft wrap is a
/// line, so the bar of a wrapped value stays on the line it edits - and its x reads that line's
/// own advances plus the control's scroll, not one `measure` of the whole value.
struct CaretRect {
    float x = 0.f;
    float y = 0.f;
    float width = 1.f;
    float height = 0.f;
};

/// Forwards to layout::get_rect (caret service). @p metrics is unused (kept for call-site
/// compatibility). An empty value still maps onto the control's line strut. A non-empty value
/// that cannot be aligned fail-closes to a zero-height rect. @p affinity selects which side of
/// a soft-wrap offset the bar sits on; the default matches a caret that has not moved vertically.
CaretRect caret_rect(const layout::Box& box, layout::FreeTypeTextMetrics& metrics,
                     const std::string& value, std::size_t caret,
                     layout::CaretAffinity affinity = layout::CaretAffinity::Downstream);

/// The engine's default action for Enter on an editable control (EVENT.md section 7): a
/// `<textarea>` or `contenteditable` host inserts a line break at the caret. A single-line
/// `<input>` inside a `<form>` submits that form unless `keydown` called `prevent_default`,
/// or the handler rebuilt the tree. Enter while composing does not insert a newline.
/// @return true when the text changed; the caller re-shapes and restyles.
bool insert_line_break(html::Node& node);

/// DOM path used by the trace (`0` is the document element, then element-child indexes).
std::string dom_path(const html::Document& doc, const html::Node* node);

struct InteractionHost {
    html::Document* document = nullptr;
    layout::LayoutEngine* layout = nullptr;
    layout::FreeTypeTextMetrics* metrics = nullptr;
    float scrollbar_thickness = 15.f;
    /// The app owns these. forget_subtree clears them; the session writes the same pointers.
    html::Node** hovered = nullptr;
    html::Node** focused = nullptr;
    html::Node** scroll_drag = nullptr;
    std::function<void()> restyle;
    /// A restyle whose scope is known: the chain nodes whose `:hover` flag changed, or the two
    /// nodes a focus move touches. When set, the host cascades only these subtrees instead of the
    /// whole document (docs/PERF.md I3). Empty → `restyle` is called instead.
    std::function<void(const std::vector<html::Node*>&)> restyle_roots;
    /// True when a loaded stylesheet has a `:hover` rule that could match this node, i.e. when a
    /// hover change can alter computed style. Null means "assume it can".
    std::function<bool(const html::Node&)> hover_styles;
    /// True when a loaded stylesheet has an `:active` rule that could match this node. Null means
    /// "assume it can" (the difftest driver), the same contract as `hover_styles`.
    std::function<bool(const html::Node&)> active_styles;
    /// The behavior table of the host: one firing arrives as one Event, and the host routes it
    /// by `Event::name()` (`App::bind`). The difftest driver leaves it empty.
    std::function<void(Event&)> dispatch;
    std::function<void(bool enabled)> set_ime;
    /// Optional IME caret/candidate rect in window/CSS px (PR-10). Empty when unsupported.
    std::function<void(float x, float y, float w, float h)> set_ime_rect;
    /// Window-bound clipboard (PR-5a). Empty when the host has no clipboard.
    std::function<ClipboardText()> clipboard_text;
    std::function<bool(std::string_view)> set_clipboard_text;
    /// Marks App geometry dirty so a value/text edit re-layouts this frame.
    std::function<void()> mark_geometry;
    /// Open-file dialog for `<input type=file>` sugar (App wires to `App::pick_open`).
    /// Empty → file inputs do not open a picker (difftest / headless).
    std::function<PickPaths(const FileDialogOptions&)> pick_open;
    /// Default action of a click on `<a href>` (App wires to `App::open_uri`).
    /// Empty → the engine does not launch anything (difftest / headless).
    std::function<bool(std::string_view)> open_uri;
};

class Interaction;

/// Pointer press, capture, and the last hover probe. `Interaction` keeps the methods the frame
/// calls and forwards the gesture state through this object.
class PointerGesture {
public:
    friend class Interaction;
    /// A live press, keyed by pointerId. The click fires on release, so the press only records
    /// what the release needs (target, press point, click count, pan candidate).
    struct PressState {
        html::Node* target = nullptr;
        float x = 0;
        float y = 0;
        Pointer mods;
        int click_count = 1;
        /// Touch/pen pan candidate: the scrollable ancestor and its offsets when the press landed.
        html::Node* scroller = nullptr;
        bool allow_x = false;
        bool allow_y = false;
        float scroll_x0 = 0;
        float scroll_y0 = 0;
        bool panning = false;
        /// The node chain holding `:active` for this press.
        html::Node* active_node = nullptr;
        /// `<input type=range>` armed by this press. Null when pointerdown cancelled it.
        html::Node* range = nullptr;
        /// `value` before this press, so release can fire `change` only when it moved.
        std::string range_value0;
        /// `<input type=color>` armed the same way.
        html::Node* color = nullptr;
        std::string color_value0;
    };

private:
    bool drag_horizontal_ = false;
    float drag_origin_ = 0;
    float drag_scroll0_ = 0;
    /// Primary-button capture target per pointerId until release.
    std::unordered_map<std::int32_t, html::Node*> pointer_capture_;
    std::unordered_map<std::int32_t, PressState> press_state_;
    /// The `<select>` whose option list is currently open (attrs `open`), or null.
    html::Node* open_select_ = nullptr;
    float last_move_x_ = 0;
    float last_move_y_ = 0;
    bool last_move_valid_ = false;
    /// Last hover probe: the point, buttons and world generation that produced `hovered`.
    float probe_x_ = 0;
    float probe_y_ = 0;
    Pointer probe_mods_{};
    std::uint64_t probe_world_gen_ = 0;
    bool probe_valid_ = false;
};

/// Nodes that owe an `onscroll` until the frame flushes them.
class ScrollController {
public:
    friend class Interaction;

private:
    std::vector<html::Node*> scroll_pending_;
    std::unordered_set<html::Node*> scroll_pending_set_;
};

/// Edit buffer, selection, and caret blink for the focused control.
class TextEditor {
public:
    friend class Interaction;

private:
    /// Drag-select session (anchor/focus direction). Projected into Document::selection.
    bool selecting_text_ = false;
    /// True when the drag is a tree Range (non-editable / cross-element), not a form flatten.
    bool selecting_tree_ = false;
    html::Node* select_node_ = nullptr;
    std::size_t select_anchor_ = 0;
    std::size_t select_focus_ = 0;
    html::BoundaryPoint range_anchor_point_{};
    /// The control whose caret the next `reveal_caret()` brings back inside its scrollport.
    html::Node* reveal_caret_ = nullptr;
    /// The last point a text-selection drag reported, for `tick_drag_scroll`.
    float drag_x_ = 0.f;
    float drag_y_ = 0.f;
    bool caret_visual_dirty_ = false;
    bool caret_blink_on_ = true;
    /// frame_t epoch for the current blink cycle; NaN = set on next tick.
    double caret_blink_epoch_ = std::numeric_limits<double>::quiet_NaN();
    float caret_damage_x_ = 0.f;
    float caret_damage_y_ = 0.f;
    float caret_damage_w_ = 0.f;
    float caret_damage_h_ = 0.f;
    bool caret_damage_valid_ = false;
    /// Editable dirty-since-focus (EVENT change-on-blur). Cleared on focus switch / blur.
    bool edit_dirty_ = false;
    /// Single edit authority (spec 3.4). Composition lives in edit_.composing only.
    EditModel edit_;
    /// Sticky caret column for Up/Down (CSS px). NaN when unset; cleared on horizontal move / hit.
    float preferred_x_ = std::numeric_limits<float>::quiet_NaN();
};

/// Element-to-element HTML drag. OS file drops do not use this.
class HtmlDragSession {
public:
    friend class Interaction;
    enum class HtmlDragPhase : std::uint8_t { Idle, Pending, Active };
    struct HtmlDrag {
        HtmlDragPhase phase = HtmlDragPhase::Idle;
        std::int32_t pointer_id = 1;
        html::Node* source = nullptr;
        html::Node* zone = nullptr;
        html::Node* hit = nullptr;
        float start_x = 0;
        float start_y = 0;
        float x = 0;
        float y = 0;
        bool ctrl = false;
        bool shift = false;
        bool alt = false;
        bool meta = false;
        bool accepted = false;
        DropEffect effect = DropEffect::None;
        std::string source_id;
        std::string text;
        std::string uri;
    };

private:
    HtmlDrag html_drag_;
    bool html_drag_suppress_click_ = false;
    bool drag_feedback_dirty_ = false;
};

/// Focus traversal state the Tab / dialog-return path keeps between keys.
class FocusController {
public:
    friend class Interaction;

private:
    /// True while `submit_form` is on the stack, so a submit button's click does not re-enter.
    bool submitting_ = false;
    /// Id focused just before `command="show-modal"` moved focus inside. Restored on close.
    std::string modal_return_id_;
};

class Interaction {
public:
    explicit Interaction(InteractionHost host);

    void pointer_move(float x, float y, const Pointer& mods = {});
    /// Hit-tests again when world coordinates changed after the last hover (PERF.md I7).
    void sync_hover();
    /// @p count is the click count for mousePressed (1 = single, 2 = double-click word select).
    /// @p mods supplies buttons bitmask + keyboard modifiers for the Pointer payload (HOST PR-8).
    void pointer_button(const std::string& type, float x, float y, const std::string& button,
                        int count, Pointer mods = {});
    /// Routes one contact the single-pointer default-action path is not driving (a second finger
    /// while another contact is the chosen one, a pen alongside a finger, …). Fires that contact's
    /// `pointerdown` / `pointermove` / `pointerup` / `pointercancel` through its own capture and
    /// runs **no** default action: no focus, no click, no scroll (PR-INPUT-2).
    void pointer_contact(float x, float y, const Pointer& mods, bool pressed, bool released,
                         bool cancelled);
    /// Aborts the chosen contact's gesture: fires `pointercancel` on its capture target and drops
    /// the capture, the drag and any text selection it owned.
    void pointer_cancel(float x, float y, const Pointer& mods);
    /// Dispatches `onwheel` from the hit under (@p x,@p y) before the scroll default. @p mods
    /// supplies only keyboard modifiers; deltas on the fired Wheel are the pixel values after
    /// any `delta_mode` conversion (same sign as scroll).
    /// @return true when a handler called prevent_default() (default scroll is skipped).
    bool wheel(float x, float y, float delta_x, float delta_y, int delta_mode, Wheel mods = {});
    /// Dispatches `keydown` (`type` "keyDown") or `keyup` (`type` "keyUp") for @p name
    /// (EVENT.md section 4.3: focused element, or <body> when nothing is focused). keyDown then
    /// runs the engine's default action - Tab moves focus, arrows / PageUp / PageDown / Space
    /// scroll. While a modal dialog is open, Tab stays inside it and `prevent_default()` skips
    /// that move. keyUp has no engine default.
    /// @return true when a handler called prevent_default().
    bool key(const std::string& type, std::string_view name, const Key& modifiers = {});

    /// Inserts, deletes, and moves the caret of the focused editable. Fires `beforeinput`
    /// before a text change; `prevent_default()` skips that change. Enter inserts a line
    /// break only when @p input says the key was not cancelled and no composition is active.
    /// @return true when the control's text changed.
    bool apply_editing(const EditInput& input);

    /// `compositionstart` (1), `compositionupdate` (2), or `compositionend` (3) on the focus.
    void composition_event(int phase);

    /// Moves document focus to @p node (or clears it when null). Fires `blur` then `focus`.
    /// @p keyboard_modality drives `:focus-visible` (Tab / App::focus pass true; pointer false).
    /// @return false when @p node is non-null and not focusable, or the host has no focused slot.
    bool focus(html::Node* node, bool keyboard_modality = true);

    /// Clears the focused node. Fires `blur`. @return false when nothing was focused.
    bool blur();

    bool style_dirty() const { return style_dirty_; }
    void clear_style_dirty() { style_dirty_ = false; }

    html::Node* hovered() const { return host_.hovered != nullptr ? *host_.hovered : nullptr; }
    html::Node* focused() const { return host_.focused != nullptr ? *host_.focused : nullptr; }

    /// Delivers one firing of the standard event @p kind on @p target: the handler name is the
    /// element's own `on<kind>` attribute value and the only link to `App::bind`. @p kind must
    /// outlive the call (callers pass a literal). A missing attribute, or no host table, is a
    /// no-op. @p key / @p drop / @p wheel / @p pointer supply typed payloads.
    /// @return true when a handler called prevent_default().
    bool fire(html::Node* target, std::string_view kind, const Key* key = nullptr,
              const Drop* drop = nullptr, const Wheel* wheel = nullptr,
              const Pointer* pointer = nullptr, const Drag* drag = nullptr,
              const void* drag_origin = nullptr);

    /// The element being dragged, so the frame can replay its painted boxes next to the pointer.
    /// `source` is null when no drag is in progress. It stays valid until the drag ends.
    struct DragFeedback {
        bool active = false;
        /// Current pointer, in the same CSS pixels as the source box's world origin.
        float x = 0;
        float y = 0;
        /// Pointer at mouse-down. The frame pins the thumbnail so this point on the
        /// source stays under the cursor (the HTML drag-image hotspot).
        float start_x = 0;
        float start_y = 0;
        const html::Node* source = nullptr;
    };
    DragFeedback drag_feedback() const;
    /// True once since the last take when the drag image appeared, moved, or went away. The frame
    /// uses this to paint; a drag that only moves the image otherwise dirties no style.
    bool take_drag_feedback_dirty();

    /// True once since the last take when the caret moved (Clip(caret) damage already queued).
    /// The frame sets `frame_visual` without marking layout dirty (P5.4 / PERF.md 2.5).
    bool take_caret_visual_dirty();

    /// Caret blink phase (CSS UI-style ~530ms half-period). True = paint the caret bar.
    /// Resets to on after focus / caret move / typing; frame clock advances the phase.
    bool caret_blink_on() const { return text_.caret_blink_on_; }

    const EditModel& edit_model() const { return text_.edit_; }

    /// Sample blink against @p frame_t (seconds since app clock0). When the phase flips, queues
    /// Clip(caret) damage and caret visual dirty (no layout). @return true when phase flipped.
    bool tick_caret_blink(double frame_t);

    /// Fires `on<kind>` on the first ancestor of @p origin (inclusive) that carries it: the
    /// EVENT.md section 4.3 walk, for the kinds whose start point is not a hit box (`beforeunload`
    /// starts at <body>, keydown at the focused element).
    /// @return true when a handler called prevent_default().
    bool fire_from(html::Node* origin, std::string_view kind);

    /// Dispatches `ondrop` for a window file drop (HOST PR-12). Starts at the hit under
    /// (@p x,@p y), else <body>; walks for the first `ondrop` (so `<body ondrop>` works as a
    /// catch-all). @p drop must outlive the call.
    /// @return true when a handler called prevent_default().
    bool file_drop(float x, float y, const Drop& drop);

    /// `HTMLElement.click()`: fire `click` on @p node and run the same defaults a pointer
    /// click would, even when the node has no box.
    void click_target(html::Node* node);

    /// Clears pointer-capture / text-select state that points into @p dead (or under it).
    void forget_subtree(html::Node* dead);

    /// Queues an `onscroll` firing for @p node (at most once per node until flush_scroll_events).
    void note_scroll(html::Node* node);

    /// True when at least one onscroll is queued (wheel / drag / key / set / into_view).
    /// `App::flush_scroll` reads this before `flush_scroll_events` so a wheel that already
    /// applied the offset still marks the frame visual (docs/PERF.md I6).
    bool has_pending_scroll() const { return !scroll_.scroll_pending_.empty(); }

    /// Dispatches queued scroll events: one `scroll` per node, then clears the queue.
    void flush_scroll_events();

    /// Brings the caret of the control that last moved it inside its scrollport: the least scroll
    /// (CSSOM "nearest") that puts the caret's font box back in view - what a browser does for a
    /// keystroke or a caret key. The control's own scrollport goes first (a fixed-height textarea
    /// scrolls its own text, a long `<input>` slides sideways), then the nearest scrollable
    /// ancestor, which is where a growing editor's page lives. The frame calls it after layout:
    /// the edit changed the value, so the boxes the input phase saw are the previous frame's.
    void reveal_caret();

    /// One frame of a selection drag whose pointer parked outside the control's scrollport: the
    /// port scrolls under the pointer and the selection follows the text that arrives, which is
    /// what a browser does while the button stays down. `pump_frame` calls it once a frame, next to
    /// `sync_hover`; a drag that never left the port moves nothing.
    void tick_drag_scroll();

private:
    /// The scrollport a text-selection drag works in (the control's own text, else the nearest
    /// scroller that has a range) with @p port filled in. Null when nothing scrolls.
    layout::Box* text_drag_port(ClientRect& port, float& view_w, float& view_h);
    /// Extends the live drag to the text under the pointer, held inside the part of the control the
    /// reader can see: past the port the pointer drives the scroll, and the text it names is the
    /// last visible line - a browser does not select the whole tail when the pointer leaves the
    /// fold.
    void update_text_drag_focus();

    bool key_event(html::Node* origin, std::string_view name, const Key& modifiers);
    /// Returns true when the wheel moved at least one scroll offset; a wheel at the end of a list
    /// answers false, and the caller then has no placement to publish
    /// (docs/PERF_REFACTOR.md stage P).
    bool apply_wheel(layout::Box* box, float delta_x, float delta_y);
    /// Clamps via set_scroll and queues onscroll when the offset moves.
    bool apply_scroll(layout::Box& box, float x, float y, float view_w = -1.f, float view_h = -1.f);
    void arm_html_drag(html::Node* hit, float x, float y, const Pointer& mods);
    void update_html_drag(float x, float y, const Pointer& mods);
    void end_html_drag(float x, float y, bool allow_drop);
    void cancel_html_drag();
    bool fire_drag(html::Node* target, std::string_view kind);
    void reset_html_drag();
    void note_drag_image();
    bool insert_dropped_text(html::Node* edit);

    void press(float x, float y, int click_count, const Pointer& mods);
    void press_non_left(float x, float y, const std::string& button, int click_count,
                        const Pointer& mods);
    void drag(float x, float y);
    void release(float x, float y, const std::string& button, const Pointer& mods);
    void fire_pointer_move(float x, float y, const Pointer& mods);
    layout::Box* scroller_from(layout::Box* hit) const;
    /// Updates :hover and fires mouse/pointer leave→enter plus mouseout/mouseover when the
    /// hit target changes. @p mods supplies Pointer coords/buttons/mods for those firings.
    void set_hover(html::Node* node, const Pointer& mods = {});
    void note_restyle();
    void note_restyle_roots(const std::vector<html::Node*>& roots);
    /// Syncs `text = value` on an editable, then writes ordered `[min,max)` into Document::selection.
    /// Sets `node->caret` to @p focus (the focus end). No-op when @p node is null.
    void commit_edit_selection(html::Node* node, std::size_t anchor, std::size_t focus);
    /// Document Selection from two tree points (non-editable / cross-element drag).
    void commit_boundary_selection(html::BoundaryPoint anchor, html::BoundaryPoint focus);
    /// Shift+arrow (and arrow collapse) on a non-editable document Selection.
    bool move_document_caret(std::string_view key, bool extend);
    /// Byte index in `node->value` nearest to pointer (@p x,@p y) inside the principal box.
    std::size_t hit_edit_index(html::Node* node, float x, float y);
    /// The byte the caret lands on one laid-out line up (@p step -1) or down (+1) from @p caret,
    /// keeping its approximate column; npos when the control has no line table to read. Past the
    /// first or last line it answers that line's near edge.
    std::size_t edit_line_target(html::Node* node, std::size_t caret, int step);
    /// Home / End: the start (@p to_end false) or end of @p caret's laid-out line, which a soft
    /// wrap counts as much as a `\n`. Falls back to the `\n` bounds without a line table.
    std::size_t edit_edge_target(html::Node* node, std::size_t caret, bool to_end,
                                 layout::CaretAffinity* landed = nullptr) const;
    /// Delete the focused range if any. Updates @p s / @p caret. @return true when text changed.
    bool edit_erase_selection(html::Node* focused, std::string& s, std::size_t& caret);
    /// Replace [start, end) with @p next. Updates @p s / @p caret. @return true when text changed.
    bool edit_splice(html::Node* focused, std::string& s, std::size_t& caret, std::size_t start,
                     std::size_t end, std::string_view next);
    /// IME preedit / commit / typed insert. @return true when text changed.
    bool apply_edit_ime(html::Node* focused, const EditInput& input, std::string& s,
                        std::size_t& caret);
    /// Backspace / Delete. @return true when text changed.
    bool apply_edit_delete(html::Node* focused, const EditInput& input, std::string& s,
                           std::size_t& caret);
    /// Arrow / Home / End / page caret movement. @return true when the caret moved.
    bool apply_edit_move(html::Node* focused, const EditInput& input, const std::string& s,
                         std::size_t& caret);
    /// PR-INPUT-2: capture is keyed by pointerId, so one contact cannot redirect another's moves.
    html::Node* capture_for(std::int32_t pointer_id) const;
    void set_capture(std::int32_t pointer_id, html::Node* node);
    void clear_capture(std::int32_t pointer_id);
    /// PR-INPUT-3: fires click / dblclick / toggle / change / file-input for a completed press
    /// (browser order: the click happens on release, on the press and release targets' nearest
    /// common ancestor).
    void finish_click(float x, float y, const Pointer& mods);
    /// Starts or continues a touch/pen pan; fires `pointercancel` once when the gesture becomes a
    /// scroll, and never fires a click for it.
    void pan_move(float x, float y, const Pointer& mods);
    /// Sets `:active` on @p node and its ancestors (CSS Selectors 4: an activated element's
    /// ancestors are active too), restyling whatever changed.
    void set_active(html::Node* node, bool on);
    /// Pan axes `touch-action` leaves to us, intersected from @p node up to @p scroller.
    static void touch_action_axes(const html::Node* node, const html::Node* scroller, bool& allow_x,
                                  bool& allow_y);

    using PressState = PointerGesture::PressState;
    using HtmlDragPhase = HtmlDragSession::HtmlDragPhase;
    using HtmlDrag = HtmlDragSession::HtmlDrag;

    /// Scrollbar thumb / track press. @return true when the press was consumed.
    bool press_scrollbar(layout::Box* hit, float x, float y);
    /// Select popup choose / toggle / dismiss. @return the select that committed, or null.
    html::Node* press_select_popup(html::Node*& click_node, html::Node*& hit_select);
    /// Arm range/color follow from this press unless pointerdown was cancelled.
    void press_range_or_color(PressState& state, html::Node* click_node, float x,
                              bool down_prevented);

    InteractionHost host_;
    bool style_dirty_ = false;
    PointerGesture pointer_;
    ScrollController scroll_;
    TextEditor text_;
    HtmlDragSession drag_;
    FocusController focus_;

    void reset_caret_blink();
    void note_caret_visual(html::Node* node);

    void close_select_popup();
    void open_select_popup(html::Node* select);
    void note_select_geometry(html::Node* select);
    /// Fires `change` for a dirty editable about to lose focus (HTML dirty-value flag).
    void commit_editable_change(html::Node* previous);
    /// Opens the host pick_open dialog for `@p node` (`input[type=file]`).
    void activate_file_input(html::Node* node);
    /// Click default of a focused button, submit/reset input, or link: fire `click`, then
    /// the same defaults a pointer click runs (link, dialog command, form submit/reset).
    void activate_control(html::Node* node);
    /// Constraint validation, then `submit`. An invalid control fires `invalid` and blocks
    /// the submit. `prevent_default()` on `submit` stops the rest.
    void submit_form(html::Node* form, html::Node* submitter);
    void reset_form(html::Node* form);
    /// Toggles bold (bit 1) or italic (bit 2) on the contenteditable selection.
    void toggle_inline_mark(html::Node* node, int bit);
    /// `command` / `commandfor` on a button: `show-modal` or `close` (EVENT.md section 7).
    void apply_dialog_command(html::Node* target);
    /// Writes `<input type=range>` `value` from a pointer x. @return true when the text changed.
    bool apply_range_value(html::Node* range, float x);
    /// Writes `<input type=color>` `value` as `#rrggbb` from a pointer x.
    bool apply_color_value(html::Node* color, float x);
};

}  // namespace input
}  // namespace zui

#endif
