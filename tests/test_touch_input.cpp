// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// Actual touch adapter: independent fingers, latched page/skill, safe cancellation.
#include "touch_input.hpp"

#include <cassert>
#include <cstdio>
#include <exception>

/**
 * Exercise independent touch ownership, page latching, safe cancellation and attack press/hold
 * edges, including the single-pointer owner.
 */
int main() try {
    using namespace d2d::client::touch;
    Input input;
    const Target fire{ 54, 7, 5, 6, false, true };
    input.event(1, Phase::Down, 100, 430);
    input.event(1, Phase::Move, 160, 430);
    assert(input.move_x == 1 && input.move_y == 0);
    input.event(2, Phase::Down, 560, 470, fire);
    input.event(2, Phase::Move, 610, 450);
    assert(input.aiming && input.dragged && input.casts.empty());
    input.event(3, Phase::Down, 680, 280);
    assert(input.alternate && input.move_x == 1 && input.preview.skill == 54);
    input.event(2, Phase::Cancel, 610, 450);
    assert(!input.aiming && input.casts.empty() && input.move_x == 1);
    input.event(2, Phase::Down, 560, 470, fire);
    input.event(2, Phase::Up, 600, 200);
    assert(input.casts.empty() && input.move_x == 1); // visible cancel region
    input.event(2, Phase::Down, 560, 470, fire);
    input.event(1, Phase::Up, 160, 430);
    assert(input.aiming && input.casts.empty() && input.move_x == 0);
    input.event(2, Phase::Up, 560, 470);
    assert(input.casts.size() == 1 && input.casts[0].skill == 54 && input.casts[0].unit == 7);
    input.frame();
    input.event(1, Phase::Down, 100, 430);
    input.event(1, Phase::Move, 140, 430);
    input.event(2, Phase::Down, 560, 470, fire, true);
    assert(input.casts.empty()); // even self casts are cancellable until lift
    input.event(2, Phase::Cancel, 560, 470);
    assert(input.casts.empty() && input.move_x > 0);
    input.event(2, Phase::Down, 560, 470, fire);
    input.clear(); // a panel/focus change invalidates all pending gestures
    input.event(2, Phase::Up, 560, 470);
    assert(input.casts.empty() && input.move_x == 0 && !input.aiming);
    input.event(3, Phase::Down, 700, 470);
    assert(input.attack_press && input.attack_down);
    input.frame();
    assert(!input.attack_press && input.attack_down);
    input.event(3, Phase::Up, 700, 470);
    assert(!input.attack_down);
    // Single-pointer adapters have a separate valid owner, not the no-finger sentinel.
    input.clear();
    input.event(-2, Phase::Down, 700, 470);
    assert(input.attack_down);
    input.event(-2, Phase::Up, 700, 470);
    assert(!input.attack_down);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_touch_input: %s\n", error.what());
    return 1;
}
