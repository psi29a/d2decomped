// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// The belt popup (apps/d2d/belt_popup.hpp): the mouse onto the HUD's belt
// strip brings it up and leaving the popup takes it down (FUN_00499bb0),
// unless its key keeps it up (UI 0x1f: FUN_00456ee0); the game menu (UI 9)
// shuts it (FUN_00499040).
#include "belt_popup.hpp"

#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::client;

int main() try {
    // The strip on 800 x 600: x 423..545, y 561..590 (w/2+0x17..w/2+0x91, h-0x27..h-10).
    assert(belt_popup::on_strip(423, 561, 800, 600) && belt_popup::on_strip(545, 590, 800, 600));
    assert(!belt_popup::on_strip(422, 570, 800, 600) && !belt_popup::on_strip(546, 570, 800, 600));
    assert(!belt_popup::on_strip(480, 560, 800, 600) && !belt_popup::on_strip(480, 591, 800, 600));

    // Hover: the mouse onto the strip brings it up, staying on the popup
    // keeps it, off the popup takes it down.
    belt_popup::State state;
    belt_popup::step(state, false, false, true, false, false);
    assert(!state.shown);
    belt_popup::step(state, false, false, true, true, true);
    assert(state.shown && state.hovered);
    belt_popup::step(state, false, false, true, false, true);       // up into the upper rows
    assert(state.shown);
    belt_popup::step(state, false, false, true, false, false);      // off the popup
    assert(!state.shown && !state.hovered);

    // The key (UI 0x1f) keeps it up off the popup; let go, it comes down.
    belt_popup::step(state, true, false, true, false, false);
    assert(state.shown && !state.hovered);
    belt_popup::step(state, false, false, true, false, false);
    assert(!state.shown);
    // Brought up by hover, the key toggled on then off: down only once the
    // mouse is off the popup (0x7bef9c cleared there, FUN_00499bb0).
    belt_popup::step(state, false, false, true, true, true);
    belt_popup::step(state, true, false, true, false, true);
    belt_popup::step(state, false, false, true, false, true);
    assert(state.shown);
    belt_popup::step(state, false, false, true, false, false);
    assert(!state.shown);

    // No hover with the menu up (UI 9), and the menu takes it down, key or not.
    belt_popup::step(state, false, true, true, true, true);
    assert(!state.shown);
    belt_popup::step(state, true, true, true, false, false);
    assert(!state.shown);
    // No hover while the handler's gates fail (a dead player: FUN_00463df0).
    belt_popup::step(state, false, false, false, true, true);
    assert(!state.shown);

    // The mini-panel moves left while the popup is up or its key is on,
    // with a belt of more than one row (FUN_0047eb50 at 0x47ebbe).
    belt_popup::State shut;
    belt_popup::State popped;
    popped.shown = true;
    assert(!belt_popup::moves_mini_panel(shut, false, 16));
    assert(belt_popup::moves_mini_panel(popped, false, 16) && belt_popup::moves_mini_panel(popped, false, 8));
    assert(belt_popup::moves_mini_panel(shut, true, 12));
    // No belt (4 boxes, one row: FUN_004979f0 = 1): it stays.
    assert(!belt_popup::moves_mini_panel(popped, false, 4) && !belt_popup::moves_mini_panel(shut, true, 4));
    std::puts("ok");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
