// SPDX-License-Identifier: GPL-3.0-or-later
// SDL3 gamepads: hotplug, each pad open, one frame's Sample of a pad. The
// mapping to actions is gamepad_map.hpp's; SDL types stay in this client layer.
#pragma once

#include "gamepad_map.hpp"
#include "platform.hpp"

#include <string>
#include <vector>

namespace d2d::client {

class Gamepads {
public:
    Gamepads() = default;
    ~Gamepads() { close(); }
    Gamepads(const Gamepads&) = delete;
    Gamepads& operator=(const Gamepads&) = delete;

    // An open pad. `built_in`: the Steam Deck's own controls (USB 0x28de /
    // 0x1205, as SDL reports the device behind Steam Input's virtual pad),
    // which never take a co-op seat. `key`: its model and serial (pad::Seats).
    // `name`, `family`: what to call it and its buttons.
    struct Pad { SDL_Gamepad* handle = nullptr; SDL_JoystickID id = 0; bool built_in = false; std::string key, name; pad::Family family = pad::Family::Xbox; };

    // Open the pads already plugged in (SDL_INIT_GAMEPAD is up).
    void open_connected();
    // Hotplug events; other events are ignored.
    void handle_event(const SDL_Event& event);
    // The first pad as of now (single player: any pad, the Deck's own
    // controls too): a default Sample (all let go) when none is.
    [[nodiscard]] pad::Sample sample() const;
    // Pad `id` as of now (let go when it's gone).
    [[nodiscard]] pad::Sample sample(SDL_JoystickID id) const;
    [[nodiscard]] bool connected() const { return !pads_.empty(); }
    [[nodiscard]] const std::vector<Pad>& pads() const { return pads_; }
    // Whether a pad other than the Deck's own controls is in.
    [[nodiscard]] bool external() const;
    // The family of pad `id`'s buttons (Xbox when it's gone).
    [[nodiscard]] pad::Family family(SDL_JoystickID id) const;
    // The pads that came and went since the last call.
    struct Changes { std::vector<Pad> came, went; };   // a gone Pad's handle is closed
    Changes changes();

    void close();

private:
    void open(SDL_JoystickID id);
    std::vector<Pad> pads_;
    Changes changes_;
};

// The SDL key a pad's Key stands for.
[[nodiscard]] SDL_Keycode pad_keycode(pad::Key key);

}  // namespace d2d::client
