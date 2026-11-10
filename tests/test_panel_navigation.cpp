// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// Controller UX decisions, not original-game parity claims.
#include "gamepad_map.hpp"
#include "skillpad.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <exception>

int main() try {
    using namespace d2d::client;
    assert(!skillpad::assignable_name("kick"));
    assert(!skillpad::assignable_name("scroll of identify"));
    const std::array<pad::Point, 4> nodes{ pad::Point{0, 0}, {50, 30}, {0, 150}, {0, 300} };
    assert(pad::tree_pick(0, 0, 0, 1, nodes) == 2);
    assert(pad::tree_pick(0, 150, 0, -1, nodes) == 0);
    assert(pad::tree_pick(0, 150, 0, 1, nodes) == 3);
    assert(pad::tree_pick(0, 0, 1, 0, nodes) == 1);
    assert(skillpad::next_page(0, 19, 1) == 8);
    assert(skillpad::next_page(8, 19, 1) == 16);
    assert(skillpad::next_page(16, 19, 1) == 0);
    assert(skillpad::next_page(0, 19, -1) == 16);
    assert(skillpad::stable_wedge(.1f, .1f, 8, 3) == 3);
    for (int index = 0; index < 8; ++index) {
        const float angle = skillpad::wedge_center(index, 8);
        assert(skillpad::stable_wedge(std::cos(angle), std::sin(angle), 8, -1) == index);
    }
    // Near the boundary between wedges zero and one: keep the existing choice.
    const float boundary = -skillpad::kHalfPi + skillpad::kTau / 8;
    assert(skillpad::stable_wedge(std::cos(boundary + .03f), std::sin(boundary + .03f), 8, 0) == 0);
    assert(skillpad::stable_wedge(std::cos(boundary + .2f), std::sin(boundary + .2f), 8, 0) == 1);
    pad::Mapper mapper{pad::skill_buttons_config({})};
    pad::Sample sample;
    sample.rx = .2f;
    assert(!mapper.step(sample, pad::Context::Game, .016f, pad::SkillUi::Wheel).aim_active);
    sample.rx = .5f;
    assert(mapper.step(sample, pad::Context::Game, .016f, pad::SkillUi::Wheel).aim_active);
    sample = {};
    sample.held[std::size_t(pad::Button::RightShoulder)] = true;
    assert(mapper.step(sample, pad::Context::Panel, .016f).tab_step == 1);
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_panel_navigation: %s\n", error.what());
    return 1;
}
