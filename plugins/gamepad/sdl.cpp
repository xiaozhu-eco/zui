#include "ext/plugin_export.h"
#include "ext/slots.h"

#include <cstdint>
#include <vector>

#if defined(ZUI_WITH_SDL)
#include <SDL3/SDL.h>
#endif

namespace zui {
namespace {

#if defined(ZUI_WITH_SDL)

struct ButtonMap {
    SDL_GamepadButton sdl;
    int index;
};

constexpr ButtonMap kButtons[] = {
    {SDL_GAMEPAD_BUTTON_SOUTH, 0},         {SDL_GAMEPAD_BUTTON_EAST, 1},
    {SDL_GAMEPAD_BUTTON_WEST, 2},          {SDL_GAMEPAD_BUTTON_NORTH, 3},
    {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, 4}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, 5},
    {SDL_GAMEPAD_BUTTON_BACK, 8},          {SDL_GAMEPAD_BUTTON_START, 9},
    {SDL_GAMEPAD_BUTTON_LEFT_STICK, 10},   {SDL_GAMEPAD_BUTTON_RIGHT_STICK, 11},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, 12},      {SDL_GAMEPAD_BUTTON_DPAD_DOWN, 13},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, 14},    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, 15},
    {SDL_GAMEPAD_BUTTON_GUIDE, 16},
};

class SdlPads final : public GamepadService {
public:
    void startup() override {
        if (refs_++ == 0) {
            SDL_Init(SDL_INIT_GAMEPAD);
        }
    }

    void shutdown() override {
        if (refs_ == 0) {
            return;
        }
        if (--refs_ != 0) {
            return;
        }
        for (SDL_Gamepad* pad : pads_) {
            SDL_CloseGamepad(pad);
        }
        pads_.clear();
        pads_state_.clear();
        listed_ = false;
    }

    void scan() override {
        if (listed_) {
            return;
        }
        listed_ = true;
        int count = 0;
        if (SDL_JoystickID* ids = SDL_GetGamepads(&count); ids != nullptr) {
            for (int i = 0; i < count; ++i) {
                added(static_cast<std::uint32_t>(ids[i]));
            }
            SDL_free(ids);
        }
    }

    void added(std::uint32_t id) override {
        const SDL_JoystickID which = static_cast<SDL_JoystickID>(id);
        for (SDL_Gamepad* pad : pads_) {
            if (SDL_GetGamepadID(pad) == which) {
                return;
            }
        }
        if (SDL_Gamepad* pad = SDL_OpenGamepad(which); pad != nullptr) {
            pads_.push_back(pad);
        }
    }

    void removed(std::uint32_t id) override {
        const SDL_JoystickID which = static_cast<SDL_JoystickID>(id);
        for (std::size_t i = 0; i < pads_.size(); ++i) {
            if (SDL_GetGamepadID(pads_[i]) != which) {
                continue;
            }
            SDL_CloseGamepad(pads_[i]);
            pads_.erase(pads_.begin() + static_cast<std::ptrdiff_t>(i));
            return;
        }
    }

    void poll() override {
        pads_state_.clear();
        for (SDL_Gamepad* pad : pads_) {
            GamepadState state;
            state.connected = true;
            if (const char* name = SDL_GetGamepadName(pad); name != nullptr) {
                state.id = name;
            }
            state.axes[0] = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
            state.axes[1] = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
            state.axes[2] = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX);
            state.axes[3] = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY);
            const float left_trigger = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
            const float right_trigger = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
            state.buttons[6].value = left_trigger;
            state.buttons[6].pressed = left_trigger >= 0.5f;
            state.buttons[7].value = right_trigger;
            state.buttons[7].pressed = right_trigger >= 0.5f;
            for (const ButtonMap& map : kButtons) {
                const bool down = SDL_GetGamepadButton(pad, map.sdl);
                state.buttons[map.index].pressed = down;
                state.buttons[map.index].value = down ? 1.f : 0.f;
            }
            pads_state_.push_back(std::move(state));
        }
    }

    std::vector<GamepadState> snapshot() const override { return pads_state_; }

private:
    int refs_ = 0;
    bool listed_ = false;
    std::vector<SDL_Gamepad*> pads_;
    std::vector<GamepadState> pads_state_;
};

#endif

}  // namespace

void zui_plugin_gamepad_register(ExtensionRegistry& registry) {
#if defined(ZUI_WITH_SDL)
    registry.add_gamepad(std::make_unique<SdlPads>());
#else
    (void)registry;
#endif
}

}  // namespace zui
