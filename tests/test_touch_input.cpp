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
    input.event(1, Phase::Down, 110, 450);
    input.event(1, Phase::Move, 175, 450);
    assert(input.move_x == 1 && input.move_y == 0);
    input.event(2, Phase::Down, 636, 506, fire);
    input.event(2, Phase::Move, 660, 500);
    assert(input.aiming && input.dragged && input.casts.empty());
    input.event(3, Phase::Down, 674, 570);
    assert(input.alternate && input.move_x == 1 && input.preview.skill == 54);
    input.event(2, Phase::Cancel, 610, 450);
    assert(!input.aiming && input.casts.empty() && input.move_x == 1);
    input.event(2, Phase::Down, 636, 506, fire);
    input.event(2, Phase::Up, 380, 170);
    assert(input.casts.empty() && input.move_x == 1); // visible cancel region
    input.event(2, Phase::Down, 636, 506, fire);
    input.event(1, Phase::Up, 175, 450);
    assert(input.aiming && input.casts.empty() && input.move_x == 0);
    input.event(2, Phase::Up, 636, 506);
    assert(input.casts.size() == 1 && input.casts[0].skill == 54 && input.casts[0].unit == 7);
    input.frame();
    input.event(1, Phase::Down, 110, 450);
    input.event(1, Phase::Move, 155, 450);
    input.event(2, Phase::Down, 636, 506, fire, true);
    assert(input.casts.empty()); // even self casts are cancellable until lift
    input.event(2, Phase::Cancel, 636, 506);
    assert(input.casts.empty() && input.move_x > 0);
    input.event(2, Phase::Down, 636, 506, fire);
    input.clear(); // a panel/focus change invalidates all pending gestures
    input.event(2, Phase::Up, 636, 506);
    assert(input.casts.empty() && input.move_x == 0 && !input.aiming);
    input.event(3, Phase::Down, 746, 546);
    assert(input.attack_press && input.attack_down);
    input.frame();
    assert(!input.attack_press && input.attack_down);
    input.event(3, Phase::Up, 746, 546);
    assert(!input.attack_down);
    // Single-pointer adapters have a separate valid owner, not the no-finger sentinel.
    input.clear();
    input.event(-2, Phase::Down, 746, 546);
    assert(input.attack_down);
    input.event(-2, Phase::Up, 746, 546);
    assert(!input.attack_down);
    assert(!chrome_hit(400, 300));
    assert(chrome_hit(572, 400, true)); // kInteract only blocks field taps when visible
    safe_inset = {};
    const auto field = layout_boxes();
    assert(field.menu.x < 20.f && field.done.x + field.done.width > 740.f);
    assert(field.attack.x + kAttackSize > field.skills[0].x);
    assert(std::abs(field.belts[0].x - (field.attack.x - 72.f)) < 1.f);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_touch_input: %s\n", error.what());
    return 1;
}
