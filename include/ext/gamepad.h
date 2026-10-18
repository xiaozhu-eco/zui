#ifndef ZUI_EXT_GAMEPAD_H
#define ZUI_EXT_GAMEPAD_H

#include <zui/host_types.h>

#include <cstdint>
#include <vector>

namespace zui {

/// Polled gamepad source. Hot-plug hooks have empty defaults, so a service that
/// only polls does not implement them. `snapshot` is an owned copy of the pads
/// the last `poll` produced.
class GamepadService {
public:
    virtual ~GamepadService() = default;
    virtual void poll() = 0;
    virtual std::vector<GamepadState> snapshot() const = 0;
    virtual void startup() {}
    virtual void shutdown() {}
    virtual void scan() {}
    virtual void added(std::uint32_t id) { (void)id; }
    virtual void removed(std::uint32_t id) { (void)id; }
};

}  // namespace zui

#endif
