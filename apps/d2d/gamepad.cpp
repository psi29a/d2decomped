// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for gamepad.hpp.
#include "gamepad.hpp"
#include "gamepad_map.hpp"

#include <log.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <utility>

namespace d2d::client {

namespace {
float axis(SDL_Gamepad* pad, SDL_GamepadAxis which) {
    return std::clamp(float(SDL_GetGamepadAxis(pad, which)) / 32767.f, -1.f, 1.f);
}
}

void Gamepads::open(SDL_JoystickID id) {
    if (std::ranges::any_of(pads_, [&](const Pad& pad) { return pad.id == id; })) return;
    SDL_Gamepad* handle = SDL_OpenGamepad(id);
    if (!handle) { d2d::log::warn("gamepad: {}", SDL_GetError()); return; }
    const Uint16 vendor = SDL_GetGamepadVendor(handle), product = SDL_GetGamepadProduct(handle);
    const char* serial = SDL_GetGamepadSerial(handle);
    const char* name = SDL_GetGamepadName(handle);
    Pad pad{ handle, id, vendor == 0x28de && product == 0x1205, std::format("{:04x}:{:04x}:{}", vendor, product, serial ? serial : ""), name ? name : "unnamed pad" };
    switch (SDL_GetGamepadType(handle)) {
        case SDL_GAMEPAD_TYPE_PS3: case SDL_GAMEPAD_TYPE_PS4: case SDL_GAMEPAD_TYPE_PS5: pad.family = pad::Family::PlayStation; break;
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO: case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT: case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: pad.family = pad::Family::Nintendo; break;
        default: pad.family = pad.built_in ? pad::Family::Deck : pad::Family::Xbox; break;
    }
    // What the Deck check rests on: the real device's ids and type, behind Steam Input's virtual pad.
    d2d::log::info("gamepad: {} connected (id {}, vendor {:04x}, product {:04x}, type {}, real type {}, serial '{}'{})", name ? name : "unnamed pad", id,
                   vendor, product, int(SDL_GetGamepadType(handle)), int(SDL_GetRealGamepadType(handle)), serial ? serial : "", pad.built_in ? ", the Deck's own" : "");
    changes_.came.push_back(pad);
    pads_.push_back(std::move(pad));
}

void Gamepads::close() {
    for (const auto& pad : pads_) SDL_CloseGamepad(pad.handle);
    pads_.clear();
}

void Gamepads::open_connected() {
    int count = 0;
    if (SDL_JoystickID* ids = SDL_GetGamepads(&count)) {
        for (int i = 0; i < count; ++i) open(ids[i]);
        SDL_free(ids);
    }
}

void Gamepads::handle_event(const SDL_Event& event) {
    if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
        open(event.gdevice.which);
    } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
        const auto gone = std::ranges::find(pads_, event.gdevice.which, &Pad::id);
        if (gone == pads_.end()) return;
        d2d::log::info("gamepad: {} disconnected (id {})", SDL_GetGamepadName(gone->handle) ? SDL_GetGamepadName(gone->handle) : "unnamed pad", gone->id);
        SDL_CloseGamepad(gone->handle);
        changes_.went.push_back(*gone);
        pads_.erase(gone);
    }
}

pad::Family Gamepads::family(SDL_JoystickID id) const {
    const auto found = std::ranges::find(pads_, id, &Pad::id);
    return found == pads_.end() ? pad::Family::Xbox : found->family;
}

bool Gamepads::external() const { return std::ranges::any_of(pads_, [](const Pad& pad) { return !pad.built_in; }); }

auto Gamepads::changes() -> Changes { return std::exchange(changes_, {}); }

pad::Sample Gamepads::sample() const { return pads_.empty() ? pad::Sample{} : sample(pads_.front().id); }

pad::Sample Gamepads::sample(SDL_JoystickID id) const {
    pad::Sample out;
    const auto found = std::ranges::find(pads_, id, &Pad::id);
    if (found == pads_.end()) return out;
    SDL_Gamepad* handle = found->handle;
    out.lx = axis(handle, SDL_GAMEPAD_AXIS_LEFTX);   out.ly = axis(handle, SDL_GAMEPAD_AXIS_LEFTY);
    out.rx = axis(handle, SDL_GAMEPAD_AXIS_RIGHTX);  out.ry = axis(handle, SDL_GAMEPAD_AXIS_RIGHTY);
    out.left_trigger = std::max(0.f, axis(handle, SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
    out.right_trigger = std::max(0.f, axis(handle, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
    using pad::Button;
    const auto set = [&](Button which, SDL_GamepadButton button) { out.held[std::size_t(which)] = SDL_GetGamepadButton(handle, button); };
    set(Button::South, SDL_GAMEPAD_BUTTON_SOUTH);              set(Button::East, SDL_GAMEPAD_BUTTON_EAST);
    set(Button::West, SDL_GAMEPAD_BUTTON_WEST);                set(Button::North, SDL_GAMEPAD_BUTTON_NORTH);
    set(Button::Back, SDL_GAMEPAD_BUTTON_BACK);                set(Button::Start, SDL_GAMEPAD_BUTTON_START);
    set(Button::LeftShoulder, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    set(Button::RightShoulder, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    set(Button::LeftStick, SDL_GAMEPAD_BUTTON_LEFT_STICK);     set(Button::RightStick, SDL_GAMEPAD_BUTTON_RIGHT_STICK);
    set(Button::DpadUp, SDL_GAMEPAD_BUTTON_DPAD_UP);           set(Button::DpadDown, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    set(Button::DpadLeft, SDL_GAMEPAD_BUTTON_DPAD_LEFT);       set(Button::DpadRight, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    return out;
}

SDL_Keycode pad_keycode(pad::Key key) {
    using pad::Key;
    switch (key) {
        case Key::Escape:    return SDLK_ESCAPE;
        case Key::Return:    return SDLK_RETURN;
        case Key::Up:        return SDLK_UP;
        case Key::Down:      return SDLK_DOWN;
        case Key::Left:      return SDLK_LEFT;
        case Key::Right:     return SDLK_RIGHT;
        case Key::Inventory: return SDLK_I;
        case Key::Character: return SDLK_C;
        case Key::SkillTree: return SDLK_T;
        case Key::QuestLog:  return SDLK_Q;
        case Key::Automap:   return SDLK_TAB;
        case Key::ToggleRun: return SDLK_R;
        case Key::Belt1:     return SDLK_1;
        case Key::Belt2:     return SDLK_2;
        case Key::Belt3:     return SDLK_3;
        case Key::Belt4:     return SDLK_4;
        case Key::SwapWeapons: return SDLK_W;
    }
    return SDLK_UNKNOWN;
}

}  // namespace d2d::client
