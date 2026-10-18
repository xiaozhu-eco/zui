#ifndef ZUI_EVENT_H
#define ZUI_EVENT_H

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace zui {

class App;
namespace input {
/// C-tier (`source/input`): builds the Event the engine hands to a handler. Declared here so
/// the constructor can stay private; application code never names it.
class Interaction;
}  // namespace input

/// Read-only payload of one keyboard firing, valid only for the duration of the callback.
///
/// `key` is spelled the way `KeyboardEvent.key` is ("Enter" / "Escape" / "s" / "ArrowDown" /
/// " "), and the modifier flags describe the key that fired, not the state of the keyboard now.
struct Key {
    std::string_view key;
    bool ctrl = false;       ///< primary accelerator; Command on macOS
    bool shift = false;
    bool alt = false;
    bool meta = false;
    bool composing = false;  ///< IME composition in progress
};

/// Read-only payload of one file-drop firing (`ondrop`), valid only for the callback.
///
/// Paths are UTF-8 and stay valid only until the callback returns. The storage is not
/// a pointer the callback can free. Read them with `path(i)`.
class Drop {
public:
    std::size_t count = 0;

    std::string_view path(std::size_t i) const noexcept {
        return (paths_ != nullptr && i < count) ? paths_[i] : std::string_view{};
    }

private:
    friend void bind_drop_paths(Drop& drop, const std::string_view* paths, std::size_t count) noexcept;
    const std::string_view* paths_ = nullptr;
};

inline void bind_drop_paths(Drop& drop, const std::string_view* paths, std::size_t count) noexcept {
    drop.paths_ = paths;
    drop.count = count;
}

/// Read-only payload of one wheel firing (`onwheel`), valid only for the callback.
///
/// Deltas use the same sign as layout scroll (CDP / Interaction::wheel): positive `delta_y`
/// increases scrollTop. Units are CSS pixels after any `deltaMode` conversion. Modifier flags
/// describe the keyboard at the moment of the wheel, not a key that fired.
struct Wheel {
    float delta_x = 0;
    float delta_y = 0;
    bool ctrl = false;   ///< primary accelerator; Command on macOS
    bool shift = false;
    bool alt = false;
    bool meta = false;
};

/// Which device produced a pointer event. Devices are not exclusive: a desktop can hold a mouse,
/// a touchscreen and a pen at once, and each contact reports its own identity.
enum class PointerType : std::uint8_t { Mouse, Pen, Touch };

/// `dataTransfer.dropEffect` for one HTML drag-and-drop firing. `None` means the drop is rejected.
enum class DropEffect : std::uint8_t { None, Copy, Move, Link };

/// `dataTransfer.effectAllowed`. zui keeps this at `Uninitialized` (every effect is allowed): there
/// is no script, and `Event` has no setter besides `prevent_default()`.
enum class EffectAllowed : std::uint8_t {
    Uninitialized,
    None,
    Copy,
    CopyLink,
    CopyMove,
    Link,
    LinkMove,
    Move,
    All
};

/// Read-only payload of one HTML drag-and-drop firing (`dragstart` / `drag` / `dragenter` /
/// `dragover` / `dragleave` / `drop` / `dragend`). Valid only for the callback.
///
/// This is the element-to-element drag. An OS file drop stays on `Drop` / `is_drop()` and leaves
/// `is_drag()` null. There is no `setData` / `setDragImage`: the engine fills `text` (text/plain)
/// and `uri` (text/uri-list). Identity of the dragged element is `drag_data()` on `Event`, not
/// `data()` — `data()` is the element that fired (the drop target, for `dragover` / `drop`).
struct Drag {
    float x = 0;
    float y = 0;
    /// `id` of the element being dragged. Empty when that element has no id.
    std::string_view source_id;
    /// text/plain. An `<a href>` or `<img src>` contributes that URL; any other draggable
    /// contributes its text.
    std::string_view text;
    /// First text/uri-list entry. Set for `<a href>` and `<img src>`; otherwise empty.
    std::string_view uri;
    /// Drop effect after this firing's default action. `None` until a `dragover` accepts the drop.
    DropEffect effect = DropEffect::None;
    EffectAllowed allowed = EffectAllowed::Uninitialized;
};

/// Read-only payload of one pointer firing (`click` / `contextmenu` / `auxclick` /
/// `pointermove` / `mousemove` / `pointerdown` / `pointerup`), valid only for the callback.
///
/// Coordinates are CSS logical pixels (same space as layout / hit-test). `button` is the
/// changed button ("left" / "right" / "middle" / …); `buttons` is the MouseEvent.buttons
/// bitmask (1=left, 2=right, 4=middle) at the moment of the firing.
struct Pointer {
    float x = 0;
    float y = 0;
    std::string_view button;  ///< "left" / "right" / "middle" / "" when none changed
    int buttons = 0;          ///< MouseEvent.buttons bitmask
    int count = 1;            ///< click count (1 = single, 2 = double, …)
    bool ctrl = false;        ///< primary accelerator; Command on macOS
    bool shift = false;
    bool alt = false;
    bool meta = false;
    /// Pointer Events identity (PR-INPUT-2, appended; the fields above keep their meaning).
    /// `pointer_id` is stable while the contact lives; the mouse keeps 1.
    std::int32_t pointer_id = 1;
    PointerType pointer_type = PointerType::Mouse;
    /// Pointer Events `isPrimary`: the first contact of its type.
    bool is_primary = true;
    /// 0..1; a device without a pressure axis reports 0.5 (the Pointer Events default).
    float pressure = 0.5f;
};

/// One dispatch of a named handler from the .zui document.
///
/// Created by the library for the duration of one Document::bind callback. Views
/// are valid only until that callback returns; copy anything that must last.
class Event {
public:
    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;

    /// Standard event kind: "click" / "input" / "change" / "keydown" / "beforeunload" / ...
    /// It is the HTML attribute name without the "on" prefix (onclick -> "click").
    std::string_view type() const noexcept { return m_type; }

    /// Handler name, matching the HTML attribute value (onclick="on_save").
    std::string_view name() const noexcept { return m_name; }

    /// id of the element that fired, or empty when the element has no id.
    std::string_view source() const noexcept { return m_source; }

    /// Payload for this firing (textbox text, checkbox "true"/"false", ...).
    std::string_view value() const noexcept { return m_value; }

    /// Reads the `data-<name>` attribute of the element that fired (source()).
    /// @p name is the suffix without the `data-` prefix: data-id="" -> data("id").
    /// Other attributes are not reachable here. Empty when absent, or for a firing with no
    /// element. Read identity here, not from value().
    std::string_view data(std::string_view name) const noexcept {
        return m_data != nullptr ? m_data(m_element, name) : std::string_view{};
    }

    /// Keyboard payload, or nullptr unless type() is "keydown" / "keyup".
    const Key* is_key() const noexcept { return m_key; }

    /// OS file-drop payload, or nullptr unless this firing is a window file drop.
    /// An element `drop` from HTML drag-and-drop leaves this null and sets `is_drag()`.
    const Drop* is_drop() const noexcept { return m_drop; }

    /// HTML drag-and-drop payload, or nullptr unless type() is a drag kind (`dragstart`, `drag`,
    /// `dragenter`, `dragover`, `dragleave`, `drop`, `dragend`).
    const Drag* is_drag() const noexcept { return m_drag; }

    /// `data-<name>` on the element being dragged. Empty unless `is_drag()`, or when that element
    /// has no such attribute. `name` is the suffix without the `data-` prefix, same as `data()`.
    std::string_view drag_data(std::string_view name) const noexcept {
        return (m_drag != nullptr && m_data != nullptr) ? m_data(m_drag_origin, name)
                                                        : std::string_view{};
    }

    /// Wheel payload, or nullptr unless type() is "wheel".
    const Wheel* is_wheel() const noexcept { return m_wheel; }

    /// Pointer payload, or nullptr unless type() is a pointer kind (click / contextmenu /
    /// auxclick / pointermove / mousemove / pointerdown / pointerup).
    const Pointer* is_pointer() const noexcept { return m_pointer; }

    /// Cancels the engine's default action for this firing: `beforeunload` does not close the
    /// window, Enter in a <textarea> does not insert a line break, `wheel` does not scroll,
    /// `dragstart` does not start the drag, and a cancelled `dragover` is what allows the drop
    /// (HTML: the default action of `dragover` is to reject). `drop` follows the same rule: cancel
    /// it when the handler performed the drop. The engine reads the flag after the callback
    /// returns; it is the only mutable state on an Event.
    void prevent_default() noexcept { m_prevented = true; }

    /// Whether the handler asked for the default action to be cancelled.
    bool default_prevented() const noexcept { return m_prevented; }

private:
    friend class App;
    friend class input::Interaction;

    /// Resolves `data-<name>` on the element that fired. Supplied by the engine, so `include/`
    /// never names a document node type.
    using DataLookup = std::string_view (*)(const void* element, std::string_view name);

    Event(std::string_view type, std::string_view name, std::string_view source,
          std::string_view value, const void* element, DataLookup data,
          const Key* key = nullptr, const Drop* drop = nullptr, const Wheel* wheel = nullptr,
          const Pointer* pointer = nullptr, const Drag* drag = nullptr,
          const void* drag_origin = nullptr) noexcept
        : m_type(type), m_name(name), m_source(source), m_value(value), m_element(element),
          m_data(data), m_key(key), m_drop(drop), m_wheel(wheel), m_pointer(pointer), m_drag(drag),
          m_drag_origin(drag_origin) {}

    std::string_view m_type;
    std::string_view m_name;
    std::string_view m_source;
    std::string_view m_value;
    const void* m_element = nullptr;
    DataLookup m_data = nullptr;
    const Key* m_key = nullptr;
    const Drop* m_drop = nullptr;
    const Wheel* m_wheel = nullptr;
    const Pointer* m_pointer = nullptr;
    const Drag* m_drag = nullptr;
    const void* m_drag_origin = nullptr;
    bool m_prevented = false;
};

}  // namespace zui

#endif  // ZUI_EVENT_H
