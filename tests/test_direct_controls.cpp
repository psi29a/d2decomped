// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// UX decisions; original aura selection uses SelectSkill, not an attack.
#include "gamepad_map.hpp"
#include "item_use.hpp"
#include "skillpad.hpp"

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <exception>

int main() try {
    using namespace d2d::client;
    const auto config = pad::skill_buttons_config({});
    for (bool release_modifier_first : { false, true }) {
        pad::Mapper mapper{ config };
        pad::Sample sample;
        sample.left_trigger = 1;
        (void)mapper.step(sample, pad::Context::Game, .01f);
        sample.held[std::size_t(pad::Button::South)] = true;
        assert(mapper.step(sample, pad::Context::Game, .01f).skill_slot == 0);
        if (release_modifier_first) sample.left_trigger = 0;
        else sample.held[std::size_t(pad::Button::South)] = false;
        assert(!mapper.step(sample, pad::Context::Game, .01f).swap_tap);
        assert(!mapper.step({}, pad::Context::Game, .01f).swap_tap);
    }
    pad::Mapper mapper{ config };
    pad::Sample south;
    south.held[std::size_t(pad::Button::South)] = true;
    (void)mapper.step(south, pad::Context::Game, .01f);
    pad::Sample both = south; both.left_trigger = 1;
    (void)mapper.step(both, pad::Context::Game, .01f);
    assert(!mapper.step(south, pad::Context::Game, .01f).swap_tap);
    (void)mapper.step({}, pad::Context::Game, .01f);
    pad::Sample east; east.held[std::size_t(pad::Button::East)] = true;
    const auto close = mapper.step(east, pad::Context::Panel, .01f);
    assert(!close.rclick_press && close.keys.size() == 1 && close.keys[0] == pad::Key::Escape);
    (void)mapper.step(south, pad::Context::Panel, .01f, pad::SkillUi::Wheel);
    assert(mapper.step(south, pad::Context::Game, .01f).skill_slot == -1);
    (void)mapper.step({}, pad::Context::Game, .01f);
    assert(mapper.step(south, pad::Context::Game, .01f).skill_slot == 0);
    const skillpad::Sheet fresh;
    assert(fresh.slot(false, 0) == 0);
    const auto old = skillpad::read("main 36\ns0 -1\n");
    assert(old.trigger(true) == 36 && old.slot(false, 0) == skillpad::kEmpty);
    assert(skillpad::read(skillpad::write(old)) == old);
    assert(item_use_action("ibk", 0, false, false, false, true).kind == ItemUseKind::None);
    assert(item_use_action("ibk", 1, false, false, false, true).kind == ItemUseKind::Identify);
    assert(item_use_action("tbk", 1, false, true, false, true).kind == ItemUseKind::None);
    assert(item_use_action("tbk", 1, false, false, false, true).label == "Open portal");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_direct_controls: %s\n", error.what());
    return 1;
}
