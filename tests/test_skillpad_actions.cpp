// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// Issue #144: panel assignments survive a reload; Use works in inventory
// and shops through the right-click path (FUN_004917c0 / FUN_00487740).
#include "gamepad_map.hpp"
#include "skillpad.hpp"

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <exception>

/**
 * Check panel-opening assignments, held-button handoff and action-label/ID mappings. Round-trip
 * panel assignments through skill-sheet serialization.
 */
int main() try {
    using namespace d2d::client;
    const auto config = pad::skill_buttons_config({});
    assert(pad::clashes(config.bindings, true).empty());
    for (const auto context : { pad::Context::Panel, pad::Context::Shop }) {
        pad::Mapper mapper{ config };
        pad::Sample sample;
        sample.held[std::size_t(pad::Button::RightStick)] = true;
        const auto use = mapper.step(sample, context, 0.016f);
        assert(use.rclick_press && !use.recentre_cursor && !use.interact_press && use.keys.empty());
        assert(!mapper.step(sample, context, 0.016f).rclick_press);
        (void)mapper.step({}, context, 0.016f);
        assert(mapper.step(sample, context, 0.016f).rclick_press);
    }
    // A button assigned to open a panel must not click or use its contents
    // merely because that held button acquires a new meaning in the panel.
    for (const auto button : { pad::Button::South, pad::Button::East }) {
        pad::Mapper mapper{ config };
        pad::Sample sample;
        sample.held[std::size_t(button)] = true;
        (void)mapper.step(sample, pad::Context::Game, 0.016f);
        const auto opened = mapper.step(sample, pad::Context::Panel, 0.016f);
        assert(!opened.click_press && !opened.rclick_press);
    }
    skillpad::Sheet sheet;
    int slot = 0;
    for (const int action : skillpad::kActions) {
        if (skillpad::panel_index(action) < 0) continue;
        assert(skillpad::binds_action(action));
        assert(!skillpad::action_label(action, true).empty());
        sheet.set_slot(false, slot++, action);
    }
    assert(slot == 5);
    const auto loaded = skillpad::read(skillpad::write(sheet));
    assert(loaded == sheet);
    assert(skillpad::panel_index(loaded.slot(false, 0)) == 1);
    assert(skillpad::panel_index(skillpad::kInteract) == -1);
    assert(skillpad::panel_index(0) == -1);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_skillpad_actions: %s\n", error.what());
    return 1;
}
