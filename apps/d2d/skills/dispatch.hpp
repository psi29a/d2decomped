// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "input/gamepad_map.hpp"
#include "skills/pad.hpp"

#include <cstddef>

namespace d2d::client::skillpad {

// Interact can open a modal during this sequence: recheck the owning pad before each destination.
template <class Eligible, class Blocked, class Perform>
void dispatch_buttons(const pad::Actions& actions, const Sheet& sheet, HoldSet& slots, TriggerHold& trigger,
                      int right_skill, Eligible&& eligible, Blocked&& blocked, Perform&& perform) {
    if (blocked()) return;
    const int trigger_set = trigger.set_for(actions.main_skill, actions.main_skill_down, actions.modifier_held);
    if (trigger_set >= 0) {
        int skill = sheet.trigger(trigger_set == 1);
        if (skill == kUnset) skill = right_skill;
        perform(eligible(skill) ? skill : kEmpty, actions.main_skill, actions.main_skill_down, kSlots);
    }
    for (int index = 0; index < kSlots; ++index) {
        if (blocked()) return;
        const bool press = actions.skill_slot == index;
        const bool button_down = actions.slot_down[std::size_t(index)];
        const int set = slots.set_for(index, press, button_down, actions.modifier_held);
        if (set >= 0) perform(sheet.slot(set == 1, index), press, button_down, index);
    }
}

} // namespace d2d::client::skillpad
