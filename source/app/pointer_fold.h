#ifndef ZUI_APP_POINTER_FOLD_H
#define ZUI_APP_POINTER_FOLD_H

#include "backends/window.h"

namespace zui {
namespace app {

/// The single-pointer view the engine still drives while PR-INPUT-2 is pending.
///
/// Before PR-INPUT-1 `IWindow` reported one pointer and `Frame` read it field by field. This folds
/// the per-contact frame back to that shape: an active touch/pen contact wins while it is down or
/// has an edge this frame, otherwise the mouse. PR-INPUT-2 deletes this header and routes every
/// contact through `Interaction` on its own (`HOST_ARCHITECTURE.md` PR-INPUT-2).
struct FoldedPointer {
    float x = 0;
    float y = 0;
    /// Identity of the chosen contact, so the events it fires carry the right pointer (PR-INPUT-2).
    std::int32_t id = 1;
    backends::PointerKind kind = backends::PointerKind::Mouse;
    bool primary = true;
    float pressure = 0.5f;
    bool left_down = false;
    bool left_pressed = false;
    bool left_released = false;
    bool right_down = false;
    bool right_pressed = false;
    bool right_released = false;
    bool middle_down = false;
    bool middle_pressed = false;
    bool middle_released = false;
    bool cancelled = false;
    int buttons = 0;
    int click_count = 1;
    float wheel_x = 0;
    float wheel_y = 0;
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
    bool meta = false;
};

/// Index of the contact the single-pointer path uses, or `npos` when the frame holds none.
///
/// PR-INPUT-1 keeps the old rule: an active touch/pen contact wins while it is down or has an edge
/// this frame, otherwise the mouse. The other contacts still get their own pointer events
/// (PR-INPUT-2) — they are just not the ones driving focus / click / scroll.
inline std::size_t chosen_contact_index(const backends::PointerFrame& frame) {
    constexpr std::size_t npos = static_cast<std::size_t>(-1);
    std::size_t mouse = npos;
    std::size_t active = npos;
    for (std::size_t i = 0; i < frame.contacts.size(); ++i) {
        const backends::PointerContact& contact = frame.contacts[i];
        if (contact.kind == backends::PointerKind::Mouse) {
            if (mouse == npos) {
                mouse = i;
            }
            continue;
        }
        if (active == npos &&
            (contact.down || contact.pressed || contact.released || contact.cancelled)) {
            active = i;
        }
    }
    return active != npos ? active : mouse;
}

inline FoldedPointer fold_pointer_frame(const backends::PointerFrame& frame) {
    FoldedPointer out;
    out.wheel_x = frame.wheel_x;
    out.wheel_y = frame.wheel_y;
    out.ctrl = frame.ctrl;
    out.shift = frame.shift;
    out.alt = frame.alt;
    out.meta = frame.meta;

    const std::size_t chosen_index = chosen_contact_index(frame);
    if (chosen_index == static_cast<std::size_t>(-1)) {
        return out;
    }
    const backends::PointerContact* chosen = &frame.contacts[chosen_index];
    out.x = chosen->x;
    out.y = chosen->y;
    out.id = chosen->id;
    out.kind = chosen->kind;
    out.primary = chosen->primary;
    out.pressure = chosen->pressure;
    out.buttons = chosen->buttons;
    out.left_down = (chosen->buttons & 1) != 0;
    out.right_down = (chosen->buttons & 2) != 0;
    out.middle_down = (chosen->buttons & 4) != 0;
    out.click_count = chosen->click_count > 0 ? chosen->click_count : 1;
    out.cancelled = chosen->cancelled;
    if (chosen->cancelled) {
        // A cancelled contact ends without a release edge: the gesture is aborted, not completed.
        return out;
    }
    if (chosen->pressed) {
        out.left_pressed = (chosen->changed_button & 1) != 0;
        out.right_pressed = (chosen->changed_button & 2) != 0;
        out.middle_pressed = (chosen->changed_button & 4) != 0;
    }
    if (chosen->released) {
        out.left_released = (chosen->changed_button & 1) != 0;
        out.right_released = (chosen->changed_button & 2) != 0;
        out.middle_released = (chosen->changed_button & 4) != 0;
    }
    return out;
}

}  // namespace app
}  // namespace zui

#endif  // ZUI_APP_POINTER_FOLD_H
