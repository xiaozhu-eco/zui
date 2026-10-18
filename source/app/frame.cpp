#include <zui/app.h>

#include "perf/perf_budget.h"
#include "backends/host_media.h"
#include "backends/platform.h"
#include "backends/renderer.h"
#include "css/engine/engine.h"
#include "css/transition/transition.h"
#include "html/document.h"
#include "html/selection.h"
#include "input/interaction.h"
#include "layout/caret/caret_service.h"
#include "layout/engine.h"
#include "layout/place_dirty.h"
#include "paint/display_list.h"
#include "trace/trace.h"

#include "app/impl.h"
#include "app/pointer_fold.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>


namespace zui {

namespace {

/// A-tier `PointerType` for a B-tier contact kind.
PointerType pointer_type_of(backends::PointerKind kind) {
    switch (kind) {
        case backends::PointerKind::Touch:
            return PointerType::Touch;
        case backends::PointerKind::Pen:
            return PointerType::Pen;
        case backends::PointerKind::Mouse:
        default:
            return PointerType::Mouse;
    }
}

}  // namespace

bool App::pump_frame() {
    backends::IWindow* window = m_impl->borrowed_window != nullptr ? m_impl->borrowed_window
                                                                   : m_impl->loop_window.get();
    backends::IRenderer* renderer = m_impl->borrowed_renderer != nullptr
                                        ? m_impl->borrowed_renderer
                                        : m_impl->loop_renderer.get();
    if (!m_impl->running || window == nullptr || renderer == nullptr ||
        m_impl->loop_interaction == nullptr) {
        return false;
    }
    paint::Painter& painter = m_impl->loop_painter;
    paint::DisplayList& list = m_impl->loop_list;
    // A modal child is its own document. Pointer, keys, and paint target that
    // document for as long as it is open. The parent window still owns the OS
    // poll and the beforeunload close path.
    Impl::OwnedWindow* const modal = m_impl->modal_child();
    input::Interaction& parent_interaction = *m_impl->loop_interaction;
    input::Interaction& interaction = (modal != nullptr && modal->interaction != nullptr)
                                          ? *modal->interaction
                                          : parent_interaction;
    html::Node*& hovered = modal != nullptr ? modal->hovered : m_impl->hovered;
    html::Node*& focused = modal != nullptr ? modal->focused : m_impl->focused;
    layout::LayoutEngine& shown_layout =
        (modal != nullptr && modal->layouter != nullptr) ? *modal->layouter : m_impl->layouter;
    html::Document& shown_document = modal != nullptr ? modal->document : m_impl->document;
    layout::FreeTypeTextMetrics& shown_metrics = modal != nullptr ? modal->metrics : m_impl->metrics;

    // EVENT.md section 8: close request is a dispatch, not the end of the loop.
    auto answer_close_request = [&]() {
        html::Node* body = html::find_tag(m_impl->document.root.get(), "body");
        return body != nullptr && parent_interaction.fire_from(body, "beforeunload");
    };

    if (!window->poll()) {
        return false;
    }
    // PR-INPUT-4: gamepads are a polling channel; snapshot the window's frame for App::gamepad().
    m_impl->gamepads = window->gamepads();
    if (window->take_close_request() && !answer_close_request()) {
        return false;
    }
    if (window->take_visibility_change()) {
        html::Node* doc_el = html::find_tag(m_impl->document.root.get(), "html");
        if (doc_el == nullptr) {
            doc_el = html::find_tag(m_impl->document.root.get(), "body");
        }
        if (doc_el != nullptr) {
            (void)interaction.fire_from(doc_el, "visibilitychange");
        }
    }
    if (window->take_orientation_change()) {
        html::Node* origin = html::find_tag(m_impl->document.root.get(), "body");
        if (origin == nullptr) {
            origin = html::find_tag(m_impl->document.root.get(), "html");
        }
        if (origin != nullptr) {
            (void)interaction.fire_from(origin, "orientationchange");
        }
    }
    int fw = 0, fh = 0;
    window->framebuffer_size(fw, fh);
    if (fw <= 0 || fh <= 0) {
        // Do not busy-spin: a closed SDL window can briefly report 0x0 before poll() sees
        // DESTROYED. Sleeping keeps the process interruptible and cheap.
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        return true;
    }
    float scale = window->content_scale();
    if (!(scale > 0.f)) {
        scale = 1.f;
    }
    const float lw = static_cast<float>(fw) / scale;
    const float lh = static_cast<float>(fh) / scale;
    if (!m_impl->clock_on) {
        m_impl->clock0 = std::chrono::steady_clock::now();
        m_impl->clock_on = true;
    }
    // Task progress is a scalar `value`. Completions that were not cancelled run here, on the
    // main thread, before the frame's own deferred handlers.
    m_impl->apply_task_results(*this);
    // Handlers may queue defer / timers so a loading state can paint first.
    m_impl->drain_deferred();
    const auto frame_started = std::chrono::steady_clock::now();
    zui::trace::Zone frame_zone("frame", zui::trace::Category::App);
    perf::begin_frame();
    struct PublishFrame {
        ~PublishFrame() { perf::end_frame(); }
    } publish_frame;
    m_impl->frame_visual = false;
    m_impl->stage = Impl::StageMs{};
    m_impl->perf_resized = false;
    m_impl->frame_t =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - m_impl->clock0).count();
    // Layout / hit-test use logical CSS px; GPU still targets the full framebuffer.
    layout::Viewport vp{lw, lh};
    const bool had_viewport = m_impl->viewport.has_value();
    if (!m_impl->viewport || m_impl->viewport->width != vp.width ||
        m_impl->viewport->height != vp.height) {
        m_impl->viewport = vp;
        m_impl->restyle(Impl::kReasonMedia);
        m_impl->geometry_dirty = true;
        m_impl->perf_resized = true;
        // Window resize → body onresize (skip the first size establishment).
        if (had_viewport && m_impl->live_interaction != nullptr) {
            html::Node* body = html::find_tag(m_impl->document.root.get(), "body");
            if (body != nullptr) {
                m_impl->live_interaction->fire_from(body, "resize");
            }
        }
    }
    {
        const HostMediaFacts next = backends::query_host_media();
        const bool mq_changed = !m_impl->host_media_known ||
                                next.hover != m_impl->host_media.hover ||
                                next.any_hover != m_impl->host_media.any_hover ||
                                next.pointer != m_impl->host_media.pointer ||
                                next.any_pointer != m_impl->host_media.any_pointer ||
                                next.reduced_motion != m_impl->host_media.reduced_motion ||
                                next.color_scheme != m_impl->host_media.color_scheme;
        if (mq_changed) {
            // Preserve window insets already applied; only refresh OS MQ facts.
            m_impl->host_media.hover = next.hover;
            m_impl->host_media.any_hover = next.any_hover;
            m_impl->host_media.pointer = next.pointer;
            m_impl->host_media.any_pointer = next.any_pointer;
            m_impl->host_media.reduced_motion = next.reduced_motion;
            m_impl->host_media.color_scheme = next.color_scheme;
            m_impl->host_media_known = true;
            m_impl->restyle(Impl::kReasonMedia);
        }
        m_impl->apply_window_insets_to_host_media(window);
    }
    const bool style_was_dirty = m_impl->restyle_q_.dirty();
    m_impl->flush_style();
    if (!m_impl->debug_flow_path.empty()) {
        html::Node* wrap = html::find(m_impl->document, "flow_wrap");
        float scroll = wrap ? wrap->scroll_y : 0.f;
        bool near_bottom = false;
        if (wrap != nullptr) {
            if (layout::Box* wb = m_impl->layouter.principal_box(wrap)) {
                const float max_scroll = std::max(0.f, wb->geo.scroll_height - wb->geo.height);
                near_bottom = max_scroll > 100.f && scroll > max_scroll * 0.70f;
            }
        }
        const bool scroll_moved = m_impl->debug_last_scroll < 0.f ||
                                  std::fabs(scroll - m_impl->debug_last_scroll) > 200.f;
        if (style_was_dirty) {
            m_impl->dump_flow("after_restyle");
            m_impl->debug_last_scroll = scroll;
        } else if (near_bottom && scroll_moved) {
            m_impl->dump_flow("near_bottom");
            m_impl->debug_last_scroll = scroll;
        }
    }

    // Pointer is already window/logical CSS px (SDL/GLFW); do not divide by content_scale.
    // The window reports every contact (mouse / fingers / pens); PR-INPUT-1 still folds them to
    // the one-pointer view this path was written for. PR-INPUT-2 routes each contact on its own.
    const backends::PointerFrame& pointers = window->pointers();
    const app::FoldedPointer ptr = app::fold_pointer_frame(pointers);
    Pointer ptr_mods;
    ptr_mods.buttons = ptr.buttons;
    ptr_mods.ctrl = ptr.ctrl;
    ptr_mods.shift = ptr.shift;
    ptr_mods.alt = ptr.alt;
    ptr_mods.meta = ptr.meta;
    // PR-INPUT-2: every pointer firing carries the contact's identity, so an app can tell a mouse
    // from a finger or a pen (the old payload had one implicit pointer).
    ptr_mods.pointer_id = ptr.id;
    ptr_mods.pointer_type = pointer_type_of(ptr.kind);
    ptr_mods.is_primary = ptr.primary;
    ptr_mods.pressure = ptr.pressure;
    // HOST PR-12: drain OS file drops into one ondrop fire (Drop payload via bind).
    if (std::vector<std::string> dropped = window->take_dropped_paths(); !dropped.empty()) {
        std::vector<std::string_view> views;
        views.reserve(dropped.size());
        for (const std::string& path : dropped) {
            views.push_back(path);
        }
        Drop payload;
        bind_drop_paths(payload, views.data(), views.size());
        interaction.file_drop(ptr.x, ptr.y, payload);
    }
    // PR-INPUT-2: every other contact still gets its own pointer stream through its own capture;
    // only the chosen contact drives focus / click / scroll.
    const std::size_t chosen = app::chosen_contact_index(pointers);
    for (std::size_t i = 0; i < pointers.contacts.size(); ++i) {
        if (i == chosen) {
            continue;
        }
        const backends::PointerContact& contact = pointers.contacts[i];
        Pointer mods = ptr_mods;
        mods.pointer_id = contact.id;
        mods.pointer_type = pointer_type_of(contact.kind);
        mods.is_primary = contact.primary;
        mods.pressure = contact.pressure;
        mods.buttons = contact.buttons;
        interaction.pointer_contact(contact.x, contact.y, mods, contact.pressed, contact.released,
                                    contact.cancelled);
    }
    if (ptr.cancelled) {
        interaction.pointer_cancel(ptr.x, ptr.y, ptr_mods);
    } else {
        interaction.pointer_move(ptr.x, ptr.y, ptr_mods);
        // Pointer Events order: pointerdown, then moves, then pointerup. A down+up that lands in
        // one poll must still be press-then-release, or the click (which fires on release) is lost.
        if (ptr.left_pressed) {
            interaction.pointer_button("mousePressed", ptr.x, ptr.y, "left",
                                       ptr.click_count > 0 ? ptr.click_count : 1, ptr_mods);
        }
        if (ptr.right_pressed) {
            interaction.pointer_button("mousePressed", ptr.x, ptr.y, "right",
                                       ptr.click_count > 0 ? ptr.click_count : 1, ptr_mods);
        }
        if (ptr.middle_pressed) {
            interaction.pointer_button("mousePressed", ptr.x, ptr.y, "middle",
                                       ptr.click_count > 0 ? ptr.click_count : 1, ptr_mods);
        }
        if (ptr.left_down || ptr.right_down || ptr.middle_down) {
            interaction.pointer_button("mouseMoved", ptr.x, ptr.y, "none", 1, ptr_mods);
        }
        if (ptr.left_released) {
            interaction.pointer_button("mouseReleased", ptr.x, ptr.y, "left", 1, ptr_mods);
        }
        if (ptr.right_released) {
            interaction.pointer_button("mouseReleased", ptr.x, ptr.y, "right", 1, ptr_mods);
        }
        if (ptr.middle_released) {
            interaction.pointer_button("mouseReleased", ptr.x, ptr.y, "middle", 1, ptr_mods);
        }
    }
    const input::Interaction::DragFeedback drag_ghost = interaction.drag_feedback();
    if (!drag_ghost.active) {
        m_impl->drag_pin_source = nullptr;
    }
    if (ptr.wheel_x != 0 || ptr.wheel_y != 0) {
        // GLFW/SDL notches are positive for wheel-up. CDP deltaY has the opposite sign,
        // and this window path turns one notch into 48px before the shared clamp.
        // HOST PR-17: onwheel runs first; prevent_default skips the scroll default.
        Wheel mods;
        mods.ctrl = ptr.ctrl;
        mods.shift = ptr.shift;
        mods.alt = ptr.alt;
        mods.meta = ptr.meta;
        interaction.wheel(ptr.x, ptr.y, -ptr.wheel_x * 48.0f, -ptr.wheel_y * 48.0f, 0, mods);
        if (std::getenv("ZUI_DEBUG_INPUT") != nullptr) {
            html::Node* wrap = html::find(m_impl->document, "flow_wrap");
            ZUI_DEBUG("[input] wheel at ({}, {}) notches={} flow_wrap_scroll={}", ptr.x, ptr.y,
                      ptr.wheel_y, wrap != nullptr ? wrap->scroll_y : -1.f);
        }
    }

    const backends::TextInputFrame& keys = window->text();
    // EVENT.md section 7: a handler runs before the engine's default action for the key, so
    // every key this frame reported is dispatched as `keydown` first, in arrival order. Tab,
    // the arrows, PageUp/PageDown and Space then run their focus or scroll default inside
    // interaction.key(). Text insertion is the same `apply_editing` the tests call.
    const std::uint64_t dom_before_keys = html::dom_stamp(&shown_document);
    bool enter_prevented = false;
    bool up_prevented = false;
    bool down_prevented = false;
    bool shift_held = false;
    bool word_held = false;
    for (const backends::KeyPress& pressed : keys.key_events) {
        const Key payload{pressed.key, pressed.ctrl, pressed.shift,
                          pressed.alt, pressed.meta, pressed.composing};
        const bool prevented = interaction.key("keyDown", pressed.key, payload);
        shift_held = shift_held || pressed.shift;
        word_held = word_held || pressed.ctrl || pressed.meta;
        if (pressed.key == "Enter") {
            enter_prevented = enter_prevented || prevented;
        }
        // A writing view moves by the paper's lines. prevent_default on the arrow must skip the
        // textarea's own line step, or that step (a few bytes in the 24px proxy) wins.
        if (prevented && pressed.key == "ArrowUp") {
            up_prevented = true;
        }
        if (prevented && pressed.key == "ArrowDown") {
            down_prevented = true;
        }
    }
    for (const backends::KeyPress& released : keys.key_up_events) {
        const Key payload{released.key, released.ctrl, released.shift,
                          released.alt, released.meta, released.composing};
        (void)interaction.key("keyUp", released.key, payload);
    }
    // A keydown that rebuilds the tree (kanban add-card) frees the focused node. Skip the
    // edit default the same way Interaction::key does.
    if (html::dom_stamp(&shown_document) == dom_before_keys && focused != nullptr &&
        input::is_editable(focused)) {
        input::EditInput edit;
        edit.text = keys.text;
        edit.composition = keys.composition;
        edit.backspace = keys.backspace;
        edit.del = keys.del;
        edit.left = keys.left;
        edit.right = keys.right;
        edit.up = up_prevented ? 0 : keys.up;
        edit.down = down_prevented ? 0 : keys.down;
        edit.page_up = keys.page_up;
        edit.page_down = keys.page_down;
        edit.home = keys.home;
        edit.end = keys.end;
        // A key frame carries one key, so the shift of any press in it is the shift of the caret
        // move the backend counted: the arrows extend the selection while it is held.
        edit.extend = shift_held;
        // Ctrl / Cmd turns the same counts into word moves and the whole-value jumps, as it does
        // in a browser's text control.
        edit.word = word_held;
        edit.enter = keys.enter;
        edit.enter_prevented = enter_prevented;
        edit.composing = keys.composing;
        if (keys.composition_phase != 0) {
            interaction.composition_event(keys.composition_phase);
        }
        (void)interaction.apply_editing(edit);
        if (keys.escape) {
            interaction.blur();
        }
    }

    const bool style_was_dirty2 = m_impl->restyle_q_.dirty();
    m_impl->flush_style();
    // A windowed list's spacers are sized from the row this layout just measured. Seeking here,
    // before flush_scroll clamps, keeps a requested offset that names a row past the first window.
    if (m_impl->sync_list_windows()) {
        m_impl->flush_style();
    }
    // Clamp against the geometry this frame just wrote and remap in one place, the way a wheel
    // scroll does: `set_scroll` knows the box's range and `sync_world` publishes it.
    m_impl->flush_scroll(&interaction);
    // A keystroke or a caret key moved the caret: this frame's layout has run, so the caret has a
    // box to bring back inside its scrollport (the edit grew the value after the input phase read
    // the old boxes).
    interaction.reveal_caret();
    // A selection drag whose pointer parked outside the control keeps scrolling under it.
    interaction.tick_drag_scroll();
    // Scroll and layout may have moved boxes under a stationary pointer (PERF.md I7).
    interaction.sync_hover();
    // HOST PR-8: report CSS cursor on the hovered box. This runs after the input flush so
    // `:active { cursor: grabbing }` is already computed on the press frame, and a release
    // restores the open palm in the same frame. `#text` is not cascaded, so a hit on a glyph
    // walks to the element that actually set `cursor`.
    //
    // `cursor` inherits, but the initial value `auto` is not written into the computed map.
    // A node with no stored cursor is `auto`: an arrow, or an I-beam when the hit is inside a
    // text control (CSS UI 4). Skipping set_cursor in that case left the previous hand in
    // place, so the rest of the window kept looking clickable after the pointer left a button.
    // An active HTML drag keeps the closed palm when the pointer is over text or empty board,
    // which would otherwise stick the I-beam.
    if (drag_ghost.active) {
        window->set_cursor(Cursor::Grabbing);
    } else if (hovered != nullptr) {
        const std::string* cur = nullptr;
        for (html::Node* node = hovered; node != nullptr; node = node->parent) {
            cur = node->style("cursor");
            if (cur != nullptr) {
                break;
            }
        }
        const std::string_view value =
            cur != nullptr ? std::string_view(*cur) : std::string_view("auto");
        // A click that lands as down+up in one poll has already released. The closed palm
        // is only for a button that is still down.
        const bool held = (ptr.left_down || ptr.left_pressed) && !ptr.left_released;
        Cursor mapped = Cursor::Default;
        if (value == "pointer") {
            mapped = Cursor::Pointer;
        } else if (value == "text" || value == "vertical-text") {
            mapped = Cursor::Text;
        } else if (value == "wait" || value == "progress") {
            mapped = Cursor::Wait;
        } else if (value == "not-allowed" || value == "no-drop") {
            mapped = Cursor::NotAllowed;
        } else if (value == "ew-resize" || value == "col-resize" || value == "e-resize" ||
                   value == "w-resize") {
            mapped = Cursor::EwResize;
        } else if (value == "ns-resize" || value == "row-resize" || value == "n-resize" ||
                   value == "s-resize") {
            mapped = Cursor::NsResize;
        } else if (value == "nwse-resize" || value == "nw-resize" || value == "se-resize") {
            mapped = Cursor::NwseResize;
        } else if (value == "nesw-resize" || value == "ne-resize" || value == "sw-resize") {
            mapped = Cursor::NeswResize;
        } else if (value == "grab") {
            mapped = held ? Cursor::Grabbing : Cursor::Grab;
        } else if (value == "grabbing") {
            mapped = Cursor::Grabbing;
        } else if (value == "move" || value == "all-scroll") {
            mapped = Cursor::Move;
        } else if (value == "crosshair" || value == "cell") {
            mapped = Cursor::Crosshair;
        } else if (value == "auto") {
            for (html::Node* node = hovered; node != nullptr; node = node->parent) {
                if (input::is_editable(node)) {
                    mapped = Cursor::Text;
                    break;
                }
            }
        }
        window->set_cursor(mapped);
    } else {
        window->set_cursor(Cursor::Default);
    }
    if (!m_impl->debug_flow_path.empty()) {
        html::Node* wrap = html::find(m_impl->document, "flow_wrap");
        float scroll = wrap ? wrap->scroll_y : 0.f;
        bool near_bottom = false;
        if (wrap != nullptr) {
            if (layout::Box* wb = m_impl->layouter.principal_box(wrap)) {
                const float max_scroll = std::max(0.f, wb->geo.scroll_height - wb->geo.height);
                near_bottom = max_scroll > 100.f && scroll > max_scroll * 0.70f;
            }
        }
        const bool scroll_moved = m_impl->debug_last_scroll < 0.f ||
                                  std::fabs(scroll - m_impl->debug_last_scroll) > 200.f;
        if (style_was_dirty2) {
            m_impl->dump_flow("after_input_restyle");
            m_impl->debug_last_scroll = scroll;
        } else if (near_bottom && scroll_moved) {
            m_impl->dump_flow("near_bottom");
            m_impl->debug_last_scroll = scroll;
        }
    }
    m_impl->frame_t =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - m_impl->clock0).count();
    const bool visual = m_impl->motion.needs_visual();
    {
        zui::trace::Zone zone("transition.sample", zui::trace::Category::App);
        m_impl->motion.sample(m_impl->frame_t);
        m_impl->animation.sample(m_impl->frame_t);
    }
    if ((m_impl->motion.needs_layout() || m_impl->animation.needs_layout()) &&
        m_impl->viewport.has_value()) {
        zui::trace::Zone zone("transition.layout", zui::trace::Category::App);
        if (m_impl->motion.needs_layout()) {
            for (html::Node* n : m_impl->motion.layout_nodes()) {
                m_impl->layouter.mark_needs_layout(n);
            }
        }
        if (m_impl->animation.needs_layout()) {
            for (html::Node* n : m_impl->animation.layout_nodes()) {
                m_impl->layouter.mark_needs_layout(n);
            }
        }
        m_impl->layouter.layout(m_impl->document, *m_impl->viewport);
        // The layout tail already published the placement, so this only runs if something else
        // invalidated the world since (docs/PERF.md I3, `world.walk`).
        if (!m_impl->layouter.world_is_current()) {
            m_impl->layouter.sync_world();
        }
    } else if (visual || m_impl->motion.needs_visual()) {
        // docs/PERF_REFACTOR.md stage P: a transform transition moves a box's world origin without
        // introducing an overlay here; nothing else in this frame marks it, so the placement marks
        // come from the sampler's own list. A frame whose list is empty marks nothing and the
        // placement falls back to the whole tree.
        for (html::Node* node : m_impl->motion.visual_nodes()) {
            if (layout::Box* box = m_impl->layouter.principal_box(node)) {
                layout::mark_place_dirty(*box);
            }
        }
        m_impl->layouter.sync_world();
    }

    // docs/RFC_MODEL.md wave 1: a `set_model` that kept a row kept that row's boxes, and the
    // placement walk above cleared their `geo.runs`. Write the single line back now, after every
    // placement of this frame and before the painter reads the runs.
    m_impl->refill_kept_rows();

    // Present re-submits the last frame when nothing changed, so the list may only be cleared when
    // this frame actually paints. Clearing it unconditionally presented an empty list on every
    // idle frame - the window went black until the next hover painted one frame (docs/PERF.md I6:
    // "no change paints nothing" must not mean "no change draws nothing").
    // A running transition is a paint input of its own: `Transitions::sample` writes the used
    // overlay outside the interned style, so neither the cascade nor the layout reports it. Without
    // `repaint_pending` a colour transition painted only on the frame an input happened to dirty
    // something and then froze at that intermediate value (docs/PERF.md I6).
    // A drag image follows the pointer without dirtying style. Ask this frame to paint so the
    // previous list is not re-submitted with the ghost left at the old point.
    if (interaction.take_drag_feedback_dirty()) {
        m_impl->frame_visual = true;
    }
    // Caret blink phase from frame clock (Task 6): flip queues Clip(caret) + visual dirty.
    (void)interaction.tick_caret_blink(m_impl->frame_t);
    if (interaction.take_caret_visual_dirty()) {
        m_impl->frame_visual = true;
    }
    if (modal != nullptr && modal->needs_paint) {
        m_impl->frame_visual = true;
        modal->needs_paint = false;
    }
    const bool paint_modal = modal != nullptr && modal->layouter != nullptr &&
                             modal->layouter->root() != nullptr;
    if (paint_modal != m_impl->loop_painted_modal) {
        m_impl->frame_visual = true;
        m_impl->loop_painted_modal = paint_modal;
    }
    const bool paint_now = m_impl->frame_visual || m_impl->motion.needs_visual() ||
                           m_impl->motion.repaint_pending() || m_impl->animation.needs_visual() ||
                           m_impl->animation.repaint_pending();
    if (paint_now) {
        list.clear();
    }
    const auto paint_started = std::chrono::steady_clock::now();
    {
        zui::trace::Zone zone("paint", zui::trace::Category::App);
        if (paint_now && shown_layout.root() != nullptr) {
            // A paint-only frame never called `layout()`, so the cascade's damaged nodes are still
            // nodes: turn them into rectangles here, or the painter has to drop every listing.
            shown_layout.resolve_pending_paint_damage();
            const html::SelectionRange& sel = shown_document.selection;
            painter.set_resource_base(shown_document.source_path);
            painter.set_selection(sel);
            html::Node* caret_n = nullptr;
            std::size_t caret_off = 0;
            if (html::selection_collapsed(sel) && sel.focus.node != nullptr) {
                caret_n = sel.focus.node;
                caret_off = sel.focus.offset;
            } else if (sel.node != nullptr && sel.start == sel.end) {
                caret_n = sel.node;
                caret_off = sel.end;
            } else if (focused != nullptr && input::is_editable(focused) &&
                       html::selection_empty(sel)) {
                caret_n = focused;
                caret_off = static_cast<std::size_t>(std::max(focused->caret, 0));
            }
            const input::EditModel& em = interaction.edit_model();
            if (caret_n != nullptr) {
                html::Node* color_n = caret_n;
                if (color_n->tag == "#text" && color_n->parent != nullptr) {
                    color_n = color_n->parent;
                }
                const layout::CaretAffinity affinity =
                    (focused != nullptr && caret_n == focused) ? em.affinity
                                                               : layout::CaretAffinity::Downstream;
                painter.set_caret(caret_n, caret_off, caret_color_for(*color_n),
                                  interaction.caret_blink_on(), affinity);
                html::Node* damage_n = color_n;
                layout::Box* pb = shown_layout.principal_box(caret_n);
                if (pb == nullptr && caret_n->parent != nullptr) {
                    pb = shown_layout.principal_box(caret_n->parent);
                    damage_n = caret_n->parent;
                }
                if (pb != nullptr) {
                    const layout::CaretRect bar = layout::caret_rect_at(
                        *pb, html::BoundaryPoint{caret_n, caret_off}, affinity);
                    if (bar.ok && bar.height > 0.f) {
                        html::note_paint_damage_rect(
                            damage_n, html::PaintRect{bar.x, bar.y, bar.width, bar.height});
                    } else {
                        html::note_paint_damage_rect(
                            damage_n, html::PaintRect{pb->geo.world_x, pb->geo.world_y,
                                                      pb->geo.width, pb->geo.height});
                    }
                }
            } else {
                painter.clear_caret();
            }
            if (focused != nullptr && input::is_editable(focused) && em.composing.has_value() &&
                em.composing->start < em.composing->end) {
                painter.set_composition(focused, em.composing->start, em.composing->end);
            } else {
                painter.clear_composition();
            }
            shown_layout.publish_paint_map();
            painter.paint(*shown_layout.root(), list, static_cast<float>(fw),
                          static_cast<float>(fh));
        }
        if (!m_impl->debug_flow_path.empty()) {
            html::Node* wrap = html::find(m_impl->document, "flow_wrap");
            const float scroll = wrap ? wrap->scroll_y : 0.f;
            bool near_bottom = false;
            if (wrap != nullptr) {
                if (layout::Box* wb = m_impl->layouter.principal_box(wrap)) {
                    const float max_scroll = std::max(0.f, wb->geo.scroll_height - wb->geo.height);
                    near_bottom = max_scroll > 100.f && scroll > max_scroll * 0.70f;
                }
            }
            if (near_bottom) {
                std::ofstream out(m_impl->debug_flow_path, std::ios::app);
                out << "--- PAINT draw_text near_bottom scroll_y=" << scroll
                    << " reused=" << painter.retained_stats().reused
                    << " walked=" << painter.retained_stats().walked
                    << " items=" << list.items().size() << " ---\n";
                // ZUI_DEBUG_FLOW_ITEMS=<frames> lists every run the frame paints, with the
                // element that produced it. The filtered list below only knows a few sample
                // strings, so a stray line made of some other chapter's text is invisible to it.
                // The budget keeps a long scroll from writing hundreds of megabytes.
                const bool all_text = m_impl->debug_items_budget > 0;
                if (all_text) {
                    --m_impl->debug_items_budget;
                }
                int hits = 0;
                for (const auto& item : list.items()) {
                    if (item.op != paint::DisplayOp::DrawText || item.text.empty()) {
                        continue;
                    }
                    if (all_text) {
                        std::string owner = "-";
                        if (item.debug_owner != nullptr) {
                            owner = item.debug_owner->tag.empty() ? std::string("anon")
                                                                  : item.debug_owner->tag;
                            if (!item.debug_owner->id.empty()) {
                                owner += "#";
                                owner += item.debug_owner->id;
                            }
                        }
                        out << "  T@(" << item.rect.x << "," << item.rect.y
                            << ") px=" << item.font_px << " w=" << item.font_weight
                            << " owner=" << owner << " \"" << Diagnostics::preview_text(item.text, 40)
                            << "\"\n";
                    }
                    // UTF-8 literals kept as escapes so MSVC source charset cannot break them.
                    static const char kJianjie[] = "\xe7\xae\x80\xe4\xbb\x8b";  // jianjie
                    static const char kLixiang[] = "\xe7\xa6\xbb\xe4\xb9\xa1";  // lixiang
                    static const char kTiezhu[] = "\xe9\x93\x81\xe6\x9f\xb1";   // tiezhu
                    static const char kShun[] =
                        "\xe9\xa1\xba\xe4\xb8\xba\xe5\x87\xa1";  // shunweifan
                    static const char kCh1[] = "\xe7\xac\xac"
                                               "1"
                                               "\xe7\xab\xa0";
                    static const char kCh3[] = "\xe7\xac\xac"
                                               "3"
                                               "\xe7\xab\xa0";
                    static const char kCh4[] = "\xe7\xac\xac"
                                               "4"
                                               "\xe7\xab\xa0";
                    const bool interesting = item.text.find(kJianjie) != std::string::npos ||
                                             item.text.find(kLixiang) != std::string::npos ||
                                             item.text.find(kTiezhu) != std::string::npos ||
                                             item.text.find(kShun) != std::string::npos ||
                                             item.text.find(kCh1) != std::string::npos ||
                                             item.text.find(kCh3) != std::string::npos ||
                                             item.text.find(kCh4) != std::string::npos;
                    if (!interesting) {
                        continue;
                    }
                    ++hits;
                    out << "  text@(" << item.rect.x << "," << item.rect.y << ") \""
                        << Diagnostics::preview_text(item.text, 32) << "\"\n";
                }
                out << "  interesting_draw_text=" << hits << "\n";
            }
        }
        if (zui::trace::active()) {
            const paint::Painter::RetainedStats rs = painter.retained_stats();
            // Scroll-container listing traffic, not the box-walk work unit (`work.painted_walked`).
            // `paint.records` / `reused` / `items` come from FrameWork at publish.
            zui::trace::counter("paint.walked", static_cast<double>(rs.walked));
            zui::trace::counter("paint.refused", static_cast<double>(rs.refused));
        }
    }
    // IME rect uses the same service rect the paint walk stashed (P5.4 / P6 ordering).
    // The caret fill itself is a display-list item from the editable walk, not appended here.
    if (focused != nullptr && input::is_editable(focused)) {
        const paint::Painter::CaretBar bar = painter.last_caret_rect();
        if (bar.ok) {
            window->set_ime_rect(bar.x, bar.y, bar.w, bar.h);
        } else {
            layout::Box* pb = shown_layout.principal_box(focused);
            if (pb != nullptr) {
                const size_t caret = std::min(
                    static_cast<size_t>(std::max(focused->caret, 0)), focused->value.size());
                const input::CaretRect caret_rect = input::caret_rect(
                    *pb, shown_metrics, focused->value, caret, interaction.edit_model().affinity);
                window->set_ime_rect(caret_rect.x, caret_rect.y, caret_rect.width,
                                     caret_rect.height);
            }
        }
    }
    if (paint_now && drag_ghost.active && drag_ghost.source != nullptr) {
        // An app can point `data-drag-ghost` at a stand-in element (the kanban title
        // chip). That chip is what follows the pointer, 8px down and to the right, the
        // same offset the page's script uses. Otherwise the source element itself is
        // the image, pinned where the button went down.
        const html::Node* image = drag_ghost.source;
        bool chip = false;
        const auto ghost_attr = drag_ghost.source->attrs.find("data-drag-ghost");
        if (ghost_attr != drag_ghost.source->attrs.end() && !ghost_attr->second.empty()) {
            if (html::Node* stand = html::find(shown_document, ghost_attr->second)) {
                image = stand;
                chip = true;
            }
        }
        const layout::Box* box = shown_layout.principal_box(const_cast<html::Node*>(image));
        if (box != nullptr) {
            float dx = 0.f;
            float dy = 0.f;
            if (chip) {
                dx = drag_ghost.x + 8.f - box->geo.world_x;
                dy = drag_ghost.y + 8.f - box->geo.world_y;
            } else {
                if (m_impl->drag_pin_source != drag_ghost.source) {
                    m_impl->drag_pin_source = drag_ghost.source;
                    m_impl->drag_pin_x = drag_ghost.start_x - box->geo.world_x;
                    m_impl->drag_pin_y = drag_ghost.start_y - box->geo.world_y;
                }
                dx = drag_ghost.x - m_impl->drag_pin_x - box->geo.world_x;
                dy = drag_ghost.y - m_impl->drag_pin_y - box->geo.world_y;
            }
            list.append_node_ghost(image, dx, dy);
        }
    }
    m_impl->stage.paint = ms_since(paint_started);
    // Upload stays in the engine window. `present` is only the swapchain acquire/wait
    // (`gpu.present` inside `IRenderer::present`). `submit` opens `gpu.submit` itself, so that
    // slice is a direct child of `frame` and is not folded into present.
    renderer->begin_frame();
    renderer->submit(list);
    const auto present_started = std::chrono::steady_clock::now();
    {
        zui::trace::Zone zone("present", zui::trace::Category::App);
        renderer->present();
    }
    m_impl->stage.present = ms_since(present_started);
    const double frame_ms = ms_since(frame_started);
    m_impl->emit_stage_counters(frame_ms);
    m_impl->report_frame(frame_ms, fw, fh);
    return true;
}

}  // namespace zui
