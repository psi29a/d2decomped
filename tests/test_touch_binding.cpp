// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
#include "input/touch/binding.hpp"
#include "input/touch/input.hpp"
#include "skills/pad.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <exception>

int main() try {
    using namespace d2d::client;
    touch::BindingPicker picker;
    const int count = skillpad::kWheelShown + 3;
    assert(picker.visible(0).shown == 0 && picker.pages(0) == 1);
    picker.turn(0, -1);
    assert(picker.page == 0 && !picker.hit);
    picker.begin(12, skillpad::kBindWheelRx, 0);
    const float angle = skillpad::wedge_center(0, skillpad::kWheelShown);
    picker.track(std::cos(angle) * skillpad::kBindWheelRx, std::sin(angle) * skillpad::kBindWheelRy, count);
    assert(picker.hit && picker.selected == 0 && picker.finger == 12);
    picker.turn(count, 1);
    assert(picker.page == 1 && !picker.hit && picker.selected == -1 && picker.finger == -1);
    assert(picker.visible(count).start == skillpad::kWheelShown && picker.visible(count).shown == 3);
    picker.begin(13, 0, 0);
    assert(!picker.hit);
    const float final_angle = skillpad::wedge_center(2, 3);
    picker.track(std::cos(final_angle) * skillpad::kBindWheelRx, std::sin(final_angle) * skillpad::kBindWheelRy, count);
    assert(picker.hit && picker.selected == count - 1);
    picker.turn(count, 1);
    assert(picker.page == 0 && !picker.hit);
    picker.turn(count, -1);
    assert(picker.page == 1);
    picker.begin(14, 250, 250);
    picker.track(250, 250, count);
    assert(!picker.hit); // A press outside the icons/hub must not bind a wedge on release.
    picker.clear_press();
    assert(picker.finger == -1 && !picker.hit);
    assert(!touch::kBindingPrevious.contains(touch::kBindingNext.x, touch::kBindingNext.y));
    assert(touch::kBindingPrevious.width >= 64 && touch::kBindingNext.height >= 64);
    skillpad::Sheet sheet;
    sheet.main = 54;
    assert(!touch::assign_binding(sheet, touch::kAttackBind, skillpad::kInteract, false, [](const auto&) { return false; }));
    assert(sheet.main == 54);
    assert(touch::trigger_binding(sheet, false, 0) == 54);
    sheet.main = skillpad::kEmpty;
    assert(touch::trigger_binding(sheet, false, 0) == skillpad::kEmpty);
    sheet.main = skillpad::kUnset;
    assert(touch::trigger_binding(sheet, false, 0) == 0);
    assert(touch::assign_binding(sheet, touch::kAttackBind, skillpad::kInteract, false, [](const auto&) { return true; }));
    assert(sheet.main == skillpad::kInteract);
    assert(touch::assign_binding(sheet, 0, skillpad::kInventory, true, [](const auto&) { return true; }));
    assert(sheet.slot(true, 0) == skillpad::kInventory);
    assert(!touch::assign_binding(sheet, 5, 54, false, [](const auto&) { return true; }));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_touch_binding: %s\n", error.what());
    return 1;
}
