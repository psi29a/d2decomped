// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: apps/d2d
// Skill-button sheets: the wheel's angle, which hand a skill uses, and the
// per-character file. Assigning again keeps the skill; Clear is the only
// way to empty a slot.
#include "skillpad.hpp"

#include <cassert>
#include <cmath>

int main() {
    using d2d::client::skillpad::Hand;
    using d2d::client::skillpad::Sheet;
    using d2d::client::skillpad::kEmpty;
    using d2d::client::skillpad::kInteract;
    using d2d::client::skillpad::kShowItems;
    using d2d::client::skillpad::kUnset;
    using d2d::client::skillpad::swings;
    using d2d::client::skillpad::wedge;
    assert(swings({ true, false, "h2h" }));
    assert(!swings({ true, true, "h2h" }));
    assert(!swings({ true, false, "none" }));
    assert(!swings({ false, false, "h2h" }));

    assert(wedge(0.f, 0.f, 4, 0.15f) == -1);
    assert(wedge(0.1f, 0.1f, 4, 0.15f) == -1);
    // Straight right is the edge of wedge 0, whose centre is up and to the right.
    assert(wedge(1.f, 0.f, 4, 0.15f) == 0);
    for (int index = 0; index < 4; ++index) {
        const float angle = d2d::client::skillpad::wedge_center(index, 4);
        assert(wedge(std::cos(angle), std::sin(angle), 4, 0.15f) == index);
    }
    using d2d::client::skillpad::wheel_page;
    auto page = wheel_page(20, 0, 8, 0);
    assert(page.shown == 8 && page.start == 0);
    page = wheel_page(20, 8, 8, page.start);
    assert(page.start == 1 && page.shown == 8);
    page = wheel_page(20, 19, 8, 0);
    assert(page.start == 12 && page.shown == 8);
    page = wheel_page(3, 1, 8, 0);
    assert(page.shown == 3 && page.start == 0);
    d2d::client::skillpad::HoldSet held_set;
    assert(held_set.set_for(0, true, true, false) == 0);
    assert(held_set.set_for(0, false, true, true) == 0);
    assert(held_set.set_for(0, false, false, true) == -1);
    assert(held_set.set_for(1, true, true, true) == 1);

    Sheet sheet;
    assert(sheet.main == kUnset && sheet.slot(false, 0) == kEmpty && sheet.slot(true, 5) == kEmpty);
    sheet.set_slot(false, 0, 54);
    sheet.set_slot(false, 0, 54);
    assert(sheet.slot(false, 0) == 54);
    sheet.set_slot(true, 0, 12);
    assert(sheet.slot(false, 0) == 54 && sheet.slot(true, 0) == 12);
    sheet.main = 36;
    const Sheet loaded = d2d::client::skillpad::read(d2d::client::skillpad::write(sheet));
    assert(loaded == sheet);
    sheet.set_slot(false, 0, kEmpty);
    assert(sheet.slot(false, 0) == kEmpty && sheet.slot(true, 0) == 12);
    const Sheet seeded = d2d::client::skillpad::read("s1 7\n");
    assert(seeded.main == kUnset && seeded.slot(false, 1) == 7 && seeded.slot(false, 0) == kEmpty);
    assert(d2d::client::skillpad::read("main nope\n").main == kUnset);
    assert(d2d::client::skillpad::read("main -1\n").main == kEmpty);
    sheet.set_slot(false, 2, kInteract);
    sheet.main = kShowItems;
    const Sheet actions = d2d::client::skillpad::read(d2d::client::skillpad::write(sheet));
    assert(actions.slot(false, 2) == kInteract && actions.main == kShowItems);
    // Resurrect's sentinel (0x7ffe) is not a Skills.txt id. The sidecar keeps it.
    // This does not run Town::apply_skill_pad or body_near.
    sheet.set_slot(true, 4, 0x7ffe);
    const Sheet raised = d2d::client::skillpad::read(d2d::client::skillpad::write(sheet));
    assert(raised.slot(true, 4) == 0x7ffe);

    // An older file has one RT skill. Both triggers use it, and the other slots stay.
    const Sheet shared = d2d::client::skillpad::read("main 36\ns0 4\na0 8\n");
    assert(shared.alt_main == kUnset && shared.trigger(false) == 36 && shared.trigger(true) == 36);
    assert(shared.slot(false, 0) == 4 && shared.slot(true, 0) == 8 && shared.slot(false, 1) == kEmpty);
    assert(d2d::client::skillpad::write(shared).find("amain") == std::string::npos);
    const Sheet shared_again = d2d::client::skillpad::read(d2d::client::skillpad::write(shared));
    assert(shared_again == shared);

    // Assigning LT+RT writes its own line. Clear is -1, not "missing".
    Sheet split = shared;
    split.alt_main = 99;
    const Sheet split_back = d2d::client::skillpad::read(d2d::client::skillpad::write(split));
    assert(split_back.main == 36 && split_back.alt_main == 99 && split_back.trigger(true) == 99);
    assert(split_back.slot(false, 0) == 4 && split_back.slot(true, 0) == 8);
    split.alt_main = kEmpty;
    const Sheet cleared = d2d::client::skillpad::read(d2d::client::skillpad::write(split));
    assert(cleared.trigger(false) == 36 && cleared.trigger(true) == kEmpty && cleared.slot(false, 0) == 4);

    d2d::client::skillpad::TriggerHold trigger_hold;
    assert(trigger_hold.set_for(true, true, false) == 0);
    assert(trigger_hold.set_for(false, true, true) == 0);
    assert(trigger_hold.set_for(false, false, true) == -1);
    assert(trigger_hold.set_for(true, true, true) == 1);

    assert(d2d::client::skillpad::tray_visible(true, false, true, 3));
    assert(d2d::client::skillpad::tray_visible(true, false, false, 0));
    assert(!d2d::client::skillpad::tray_visible(true, true, true, 3));
    assert(!d2d::client::skillpad::tray_visible(false, false, true, 3));

    d2d::client::skillpad::MenuHandoff handoff;
    assert(!handoff.drop(true));
    handoff.armed = true;
    assert(handoff.drop(true) && handoff.armed);
    assert(handoff.drop(false) && !handoff.armed);
    assert(!handoff.drop(true));

    using d2d::client::skillpad::aura_choice;
    const auto turned_on = aura_choice(0, 98, 0, false, 0);
    assert(turned_on.next == 98 && turned_on.remembered == 0);
    const auto off = aura_choice(98, 98, 98, true, turned_on.remembered);
    assert(off.next == 0 && off.remembered == 0);
    const auto swapped = aura_choice(98, 99, 98, true, 0);
    assert(swapped.next == 99 && swapped.remembered == 0);
    const auto restored = aura_choice(99, 99, 99, true, swapped.remembered);
    assert(restored.next == 0 && restored.remembered == 0);
    return 0;
}
