#ifndef ZUI_BACKENDS_SDL_FINGER_TEST_H
#define ZUI_BACKENDS_SDL_FINGER_TEST_H

#include "backends/window.h"

#include <zui/export.h>

#include <cstdint>

namespace zui {
namespace backends {

/// Queue one finger edge onto the SDL window `window` belongs to.
/// `nx` and `ny` are normalized window coordinates in 0..1, the same space as
/// `SDL_EVENT_FINGER_*`. Returns false when `window` is not the SDL backend.
/// Shipped `IWindow::poll` is what turns the event into a touch contact.
ZUI_EXPORT bool push_finger_for_test(IWindow& window, bool down, float nx, float ny,
                                     std::int64_t finger, float pressure);

}  // namespace backends
}  // namespace zui

#endif
