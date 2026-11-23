// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions for input/pointer_events.hpp.
#include "input/pointer_events.hpp"

#include "ui.hpp"

#include <SDL3/SDL_events.h>

#include <cstdint>

namespace d2d::client {

// game.exe handles each window message on its own: the D2Win button's
// mouse down (FUN_00501090) tests the press where it was and its mouse up
// (FUN_00501150) the release where that was. d2d reads a frame's events as
// one snapshot (press_this_frame against the frame's last x / y), so a
// press and a release with a move between them in one frame tested the
// press at the release (#545): off a button and onto it, a click. Ending
// the frame's poll at the first pointer event after a press keeps every
// reader of the snapshot right; the rest comes a frame (~16 ms) later.
bool waits_for_next_frame(const Mouse& mouse, std::uint32_t next_type) {
    if (!mouse.press_this_frame && !mouse.rpress_this_frame) return false;
    switch (next_type) {
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_CANCELED:
            return true;
        default:
            return false;
    }
}

}  // namespace d2d::client
