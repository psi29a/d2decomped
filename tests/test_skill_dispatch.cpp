// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
#include "input/gamepad_map.hpp"
#include "skills/dispatch.hpp"
#include "skills/pad.hpp"

#include <cassert>
#include <cstdio>
#include <exception>
#include <vector>

int main() try {
    using namespace d2d::client;
    // The trigger's bound Interact must run, then prevent a simultaneous held slot from casting.
    for (const bool trigger_interact : { false, true }) {
        skillpad::Sheet sheet;
        sheet.main = trigger_interact ? skillpad::kInteract : skillpad::kEmpty;
        sheet.set_slot(false, 0, trigger_interact ? 54 : skillpad::kInteract);
        sheet.set_slot(false, 1, 56);
        skillpad::HoldSet slots;
        skillpad::TriggerHold trigger;
        pad::Actions actions;
        actions.main_skill = actions.main_skill_down = trigger_interact;
        actions.skill_slot = 0;
        actions.slot_down[0] = actions.slot_down[1] = true;
        // Slot 1 was already held before the interaction frame.
        (void)slots.set_for(1, true, true, false);
        bool open = false;
        std::vector<int> dispatched;
        const auto perform = [&](int skill, bool, bool, int) {
            dispatched.push_back(skill);
            if (skill == skillpad::kInteract) open = true;
        };
        const auto eligible = [](int) { return true; };
        skillpad::dispatch_buttons(actions, sheet, slots, trigger, 0, eligible, [&] { return open; }, perform);
        assert(open && dispatched == std::vector<int>{skillpad::kInteract});
        dispatched.clear();
        // An already-open chooser blocks the owning pad, including trigger dispatch.
        skillpad::dispatch_buttons(actions, sheet, slots, trigger, 0, eligible, [&] { return open; }, perform);
        assert(dispatched.empty());
        // A co-op partner's dispatch remains live while the primary pad's chooser is open.
        const bool primary_pad = false;
        skillpad::dispatch_buttons(actions, sheet, slots, trigger, 0, eligible, [&] { return primary_pad && open; }, perform);
        assert(!dispatched.empty() && dispatched.back() == 56);
    }
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
