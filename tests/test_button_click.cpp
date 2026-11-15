// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: dcc font game
// test-includes: apps/d2d
// A D2Win button clicks on a release over it only when a press on it armed
// it (FUN_00501090 sets +0x44 / +0x48 on mouse down; FUN_00501150 calls the
// callback on mouse up only with +0x48 set). Char select's double-click
// opens Select Difficulty on its second press, so that click's release over
// the box's NORMAL / NIGHTMARE / HELL must pick nothing.
#include "ui.hpp"

#include <atomic>
#include <cassert>
#include <cstdio>
#include <exception>
#include <initializer_list>

using d2d::client::Button;
using d2d::client::Mouse;
using d2d::client::Screen;

namespace {

struct Step { int x, y; bool down, press, release; };

// Runs the frames on a fresh copy of `button`; the last frame's click.
bool clicks(Button button, std::initializer_list<Step> frames) {
    std::atomic<bool> quit{ false };
    Screen screen = Screen::CharSelect;
    for (const auto& frame : frames) {
        Mouse mouse;
        mouse.x = frame.x; mouse.y = frame.y;
        mouse.down = frame.down; mouse.press_this_frame = frame.press; mouse.release_this_frame = frame.release;
        update_button(button, mouse, screen, quit);
    }
    return button.clicked;
}

}  // namespace

int main() try {
    const Button nightmare{ 264, 305, 272, 35 };                     // Select Difficulty's NIGHTMARE (bottom 340)
    constexpr Step kOverDown{ 300, 320, true, true, false }, kOverHeld{ 300, 320, true, false, false };
    constexpr Step kOverUp{ 300, 320, false, false, true }, kOffDown{ 100, 100, true, true, false };
    constexpr Step kOffHeld{ 100, 100, true, false, false }, kOffUp{ 100, 100, false, false, true };
    // A plain click: press and release in one frame (devctl's), or over two.
    assert(clicks(nightmare, { { 300, 320, false, true, true } }));
    assert(clicks(nightmare, { kOverDown, kOverUp }));
    // Held off the button and back, the press still holds it armed.
    assert(clicks(nightmare, { kOverDown, kOffHeld, kOverHeld, kOverUp }));
    // The double-click: the press that opened the box was not on its button.
    assert(!clicks(nightmare, { kOverUp }));
    assert(!clicks(nightmare, { kOffDown, kOverHeld, kOverUp }));
    // Pressed on it, released off it: nothing.
    assert(!clicks(nightmare, { kOverDown, kOffUp }));
    // Esc closed the box with the mouse held on NIGHTMARE (no update since);
    // the double-click reopens it and its release lands there: disarmed at
    // the open, nothing. Without the disarm the stale press would click.
    {
        Button stale = nightmare;
        std::atomic<bool> quit{ false };
        Screen screen = Screen::CharSelect;
        Mouse held;
        held.x = 300; held.y = 320; held.down = true; held.press_this_frame = true;
        update_button(stale, held, screen, quit);
        assert(stale.pressed);
        assert(clicks(stale, { kOverHeld, kOverUp }));
        disarm(stale);
        assert(!clicks(stale, { kOverHeld, kOverUp }));
    }
    // A switch button switches only on its click.
    Button ok{ 627, 537, 128, 35, nullptr, nullptr, Screen::InGame, true };
    std::atomic<bool> quit{ false };
    Screen screen = Screen::CharSelect;
    Mouse release;
    release.x = 650; release.y = 550; release.release_this_frame = true;
    assert(!update_button(ok, release, screen, quit) && screen == Screen::CharSelect);
    std::puts("ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_button_click: %s\n", error.what());
    return 1;
}
