// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for gamepad.hpp.
#include "gamepad.hpp"
#include "gamepad_map.hpp"

#include <log.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <string_view>
#include <system_error>
#include <utility>

namespace d2d::client {

namespace {
float axis(SDL_Gamepad* pad, SDL_GamepadAxis which) {
    return std::clamp(float(SDL_GetGamepadAxis(pad, which)) / 32767.f, -1.f, 1.f);
}

// Who's behind `pad.handle` as of now: SDL looks a Steam Input virtual pad's
// real ids and name up in Steam's file each call, and Steam can put another
// pad behind the same virtual one when pads come and go (it moves the
// Deck's own controls to the last slot docked).
void identify(Gamepads::Pad& pad) {
    const Uint16 vendor = SDL_GetGamepadVendor(pad.handle), product = SDL_GetGamepadProduct(pad.handle);
    const char* serial = SDL_GetGamepadSerial(pad.handle);
    const char* name = SDL_GetGamepadName(pad.handle);
    pad.name = name ? name : "unnamed pad";
    pad.built_in = (vendor == 0x28de && product == 0x1205) || pad.name.contains("Steam Deck");
    pad.key = std::format("{:04x}:{:04x}:{}", vendor, product, serial ? serial : "");
    switch (SDL_GetGamepadType(pad.handle)) {
        case SDL_GAMEPAD_TYPE_PS3: case SDL_GAMEPAD_TYPE_PS4: case SDL_GAMEPAD_TYPE_PS5: pad.family = pad::Family::PlayStation; break;
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO: case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT: case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: pad.family = pad::Family::Nintendo; break;
        default: pad.family = pad.built_in ? pad::Family::Deck : pad::Family::Xbox; break;
    }
}

void log_pad(const Gamepads::Pad& pad, std::string_view what) {
    // What the Deck check rests on: the real device's ids and type, behind Steam Input's virtual pad.
    d2d::log::info("gamepad: {} {} (id {}, key {}, type {}, real type {}{})", pad.name, what, pad.id, pad.key, int(SDL_GetGamepadType(pad.handle)),
                   int(SDL_GetRealGamepadType(pad.handle)), pad.built_in ? ", the Deck's own" : "");
}
}

void Gamepads::open(SDL_JoystickID id) {
    if (std::ranges::any_of(pads_, [&](const Pad& pad) { return pad.id == id; })) return;
    SDL_Gamepad* handle = SDL_OpenGamepad(id);
    if (!handle) { d2d::log::warn("gamepad: {}", SDL_GetError()); return; }
    Pad pad{ handle, id };
    identify(pad);
    log_pad(pad, "connected");
    changes_.came.push_back(pad);
    pads_.push_back(std::move(pad));
}

// A pad that's someone else now (held kSettleMs, past Steam's reshuffle):
// the one before went, this one came.
void Gamepads::refresh(std::uint64_t now_ms) {
    for (auto& pad : pads_) {
        Pad now = pad;
        identify(now);
        if (now.key == pad.key && now.built_in == pad.built_in) { pad.name = now.name; pad.settling.clear(); continue; }
        if (now.key != pad.settling) { pad.settling = now.key; pad.settle_from = now_ms; continue; }
        if (now_ms - pad.settle_from < kSettleMs) continue;
        log_pad(now, std::format("now behind id {} (was {})", pad.id, pad.name));
        now.settling.clear();
        changes_.went.push_back(pad);
        changes_.came.push_back(now);
        pad = std::move(now);
    }
}

void Gamepads::close() {
    for (const auto& pad : pads_) SDL_CloseGamepad(pad.handle);
    pads_.clear();
}

void Gamepads::open_connected() {
    // SDL tells Steam Input's virtual pads apart (the Deck's own from an
    // external one) by the file Steam names here; unread, every pad is 28de:11ff.
    if (const char* info = std::getenv("SteamVirtualGamepadInfo")) {
        std::error_code error;                        // the sandbox may refuse to look: not here, and no throw at startup
        const bool here = std::filesystem::exists(info, error) && !error;
        d2d::log::info("gamepad: Steam's pad info '{}' ({})", info, here ? "readable" : "not here: the Deck's own controls can't be told apart");
    }
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
        d2d::log::info("gamepad: {} disconnected (id {})", gone->name, gone->id);
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

auto Gamepads::changes() -> Changes {
    refresh(SDL_GetTicks());
    return std::exchange(changes_, {});
}

auto Gamepads::primary() const -> const Pad* {
    if (pads_.empty()) return nullptr;
    const auto external = std::ranges::find(pads_, false, &Pad::built_in);
    return external != pads_.end() ? &*external : &pads_.front();
}

pad::Sample Gamepads::sample() const {
    const Pad* pad = primary();
    return pad ? sample(pad->id) : pad::Sample{};
}

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
