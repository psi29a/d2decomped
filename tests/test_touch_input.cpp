// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// Actual touch adapter: independent fingers, latched page/skill, safe cancellation.
#include "touch_input.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
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
    input.event(1, Phase::Down, 126, 462);
    input.event(1, Phase::Move, 191, 462);
    assert(input.move_x == 1 && input.move_y == 0);
    input.event(2, Phase::Down, 598, 530, fire);
    input.event(2, Phase::Move, 630, 500);
    assert(input.aiming && input.dragged && input.casts.empty());
    input.event(3, Phase::Down, 736, 440);
    assert(input.alternate && input.move_x == 1 && input.preview.skill == 54);
    input.event(2, Phase::Cancel, 610, 450);
    assert(!input.aiming && input.casts.empty() && input.move_x == 1);
    input.event(2, Phase::Down, 598, 530, fire);
    input.event(2, Phase::Up, 380, 170);
    assert(input.casts.empty() && input.move_x == 1); // visible cancel region
    input.event(2, Phase::Down, 598, 530, fire);
    input.event(1, Phase::Up, 191, 462);
    assert(input.aiming && input.casts.empty() && input.move_x == 0);
    input.event(2, Phase::Up, 598, 530);
    assert(input.casts.size() == 1 && input.casts[0].skill == 54 && input.casts[0].unit == 7);
    input.frame();
    input.event(1, Phase::Down, 126, 462);
    input.event(1, Phase::Move, 171, 462);
    input.event(2, Phase::Down, 598, 530, fire, true);
    assert(input.casts.empty()); // even self casts are cancellable until lift
    input.event(2, Phase::Cancel, 598, 530);
    assert(input.casts.empty() && input.move_x > 0);
    input.event(2, Phase::Down, 598, 530, fire);
    input.clear(); // a panel/focus change invalidates all pending gestures
    input.event(2, Phase::Up, 598, 530);
    assert(input.casts.empty() && input.move_x == 0 && !input.aiming);
    input.event(3, Phase::Down, 736, 536);
    assert(input.attack_press && input.attack_down);
    input.frame();
    assert(!input.attack_press && input.attack_down);
    input.event(3, Phase::Up, 736, 536);
    assert(!input.attack_down);
    // Single-pointer adapters have a separate valid owner, not the no-finger sentinel.
    input.clear();
    input.event(-2, Phase::Down, 736, 536);
    assert(input.attack_down);
    input.event(-2, Phase::Up, 736, 536);
    assert(!input.attack_down);
    assert(!chrome_hit(400, 300));
    assert(!chrome_hit(390, 454));
    assert(chrome_hit(390, 454, true)); // kInteract only blocks field taps when visible
    // layout_boxes positions depend on safe_inset and kUiScale only, not on pad_field().
    safe_inset = {};
    const auto field = layout_boxes();
    assert(field.menu.x < 20.f && field.done.x + field.done.width > 740.f);
    assert(field.attack.x + field.attack.width > 790.f && field.attack.y + field.attack.height > 590.f);
    const auto centre = [](Box box) { return std::pair{ box.x + box.width * .5f, box.y + box.height * .5f }; };
    const auto gap = [&](Box left, Box right) {
        const auto [left_x, left_y] = centre(left);
        const auto [right_x, right_y] = centre(right);
        return std::hypot(left_x - right_x, left_y - right_y) - left.width * .5f - right.width * .5f;
    };
    assert(gap(field.attack, field.page) >= 8.f);
    float skill_top = field.skills[0].y;
    for (const auto& skill : field.skills) {
        skill_top = std::min(skill_top, skill.y);
        assert(gap(field.attack, skill) >= 8.f);
        assert(gap(field.page, skill) >= 8.f);
        for (const auto& belt : field.belts) assert(gap(skill, belt) >= 8.f);
    }
    for (int i = 0; i < 4; ++i)
        for (int j = i + 1; j < 4; ++j) assert(gap(field.skills[std::size_t(i)], field.skills[std::size_t(j)]) >= 8.f);
    float belt_bottom = 0.f;
    for (const auto& belt : field.belts) belt_bottom = std::max(belt_bottom, belt.y + belt.height);
    assert(belt_bottom + 8.f <= skill_top);
    assert(std::abs(field.belts[0].x - field.belts[3].x) < 1.f);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_touch_input: %s\n", error.what());
    return 1;
}
