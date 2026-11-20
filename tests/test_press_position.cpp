// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: dcc font game SDL3::SDL3
// test-sources: apps/d2d/pointer_events.cpp
// test-includes: apps/d2d
// A press is tested where it happened (#545). game.exe handles each window
// message on its own: the D2Win button's mouse down (FUN_00501090) arms it
// only when the press is over it, its mouse up (FUN_00501150) clicks only
// an armed one. d2d reads a frame's events as one snapshot, so the frame's
// poll ends at the first pointer event after a press
// (waits_for_next_frame): a press off a button and a release on it, both
// in one frame's queue, no longer click it.
#include "pointer_events.hpp"
#include "ui.hpp"

#include <SDL3/SDL_events.h>

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <vector>

using d2d::client::Button;
using d2d::client::Mouse;
using d2d::client::Screen;
using d2d::client::waits_for_next_frame;

namespace {

struct Pointer { std::uint32_t type; int x, y; };

// The left button's part of handle_sdl_events (window.cpp).
void apply(Mouse& mouse, const Pointer& event) {
    mouse.x = event.x; mouse.y = event.y;
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) { mouse.down = true; mouse.press_this_frame = true; }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) { mouse.down = false; mouse.release_this_frame = true; }
}

// Polls `queue` frame by frame as main.cpp's loop does, updating `button`
// each frame; whether it clicked, and how many frames that took.
struct Run { bool clicked = false; int frames = 0; };
Run run(Button button, const std::vector<Pointer>& queue) {
    std::atomic<bool> quit{ false };
    Screen screen = Screen::CharSelect;
    Mouse mouse;
    Run result;
    std::size_t next = 0;
    while (next < queue.size()) {
        mouse.press_this_frame = mouse.release_this_frame = mouse.rpress_this_frame = mouse.rrelease_this_frame = false;
        while (next < queue.size() && !waits_for_next_frame(mouse, queue[next].type)) apply(mouse, queue[next++]);
        update_button(button, mouse, screen, quit);
        result.clicked = result.clicked || button.clicked;
        ++result.frames;
    }
    return result;
}

}  // namespace

int main() try {
    const Button nightmare{ 264, 305, 272, 35 };   // Select Difficulty's NIGHTMARE (as test_button_click)
    // Off it, press, a fast move onto it, release: one frame's queue.
    const auto drag_on = run(nightmare, { { SDL_EVENT_MOUSE_MOTION, 100, 100 }, { SDL_EVENT_MOUSE_BUTTON_DOWN, 100, 100 },
                                          { SDL_EVENT_MOUSE_MOTION, 300, 320 }, { SDL_EVENT_MOUSE_BUTTON_UP, 300, 320 } });
    assert(!drag_on.clicked && drag_on.frames == 2);
    // A plain click on it (devctl's motion, down, up) still clicks, over two frames.
    const auto click = run(nightmare, { { SDL_EVENT_MOUSE_MOTION, 300, 320 }, { SDL_EVENT_MOUSE_BUTTON_DOWN, 300, 320 },
                                        { SDL_EVENT_MOUSE_BUTTON_UP, 300, 320 } });
    assert(click.clicked && click.frames == 2);
    // Pressed on it, a fast move off, released there: nothing.
    assert(!run(nightmare, { { SDL_EVENT_MOUSE_BUTTON_DOWN, 300, 320 }, { SDL_EVENT_MOUSE_MOTION, 100, 100 },
                             { SDL_EVENT_MOUSE_BUTTON_UP, 100, 100 } }).clicked);

    // The predicate itself: only pointer events wait, and only after a press.
    Mouse idle;
    assert(!waits_for_next_frame(idle, SDL_EVENT_MOUSE_MOTION));
    Mouse pressed;
    pressed.press_this_frame = true;
    for (const std::uint32_t type : { SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP,
                                      SDL_EVENT_FINGER_DOWN, SDL_EVENT_FINGER_UP, SDL_EVENT_FINGER_MOTION, SDL_EVENT_FINGER_CANCELED })
        assert(waits_for_next_frame(pressed, type));
    assert(!waits_for_next_frame(pressed, SDL_EVENT_KEY_DOWN) && !waits_for_next_frame(pressed, SDL_EVENT_MOUSE_WHEEL));
    Mouse right;
    right.rpress_this_frame = true;
    assert(waits_for_next_frame(right, SDL_EVENT_MOUSE_BUTTON_UP));
    std::puts("ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_press_position: %s\n", error.what());
    return 1;
}
