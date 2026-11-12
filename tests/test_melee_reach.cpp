// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A unit's melee reach, FUN_00622c40, and a moving target, FUN_00622dc0
// (tools/emu/melee_reach.py): how near a click on an NPC walks.
#include <melee_reach.hpp>
#include <unit_size.hpp>

#include <cassert>

using namespace d2d::rules;

int main() {
    const auto clear = [] { return false; };
    const auto cut = [] { return true; };
    // Bare hands (range 0): reach 1, 2 when the target walks; a wall between stops it.
    assert(melee_reach(1, 0, false, 0, clear));
    assert(!melee_reach(2, 0, false, 0, clear));
    assert(melee_reach(2, 0, false, 1, clear));
    assert(!melee_reach(1, 0, false, 0, cut));
    // At 0 or less, always, wall or not.
    assert(melee_reach(0, 0, false, 0, cut));
    assert(melee_reach(-1, 0, false, 0, cut));
    // A polearm's range 2: within 3.
    assert(melee_reach(3, 2, false, 0, clear));
    assert(!melee_reach(4, 2, false, 0, clear));
    // A tentacle under range + 8, through a wall; at range + 8 the usual test.
    assert(melee_reach(7, 0, true, 0, cut));
    assert(!melee_reach(8, 0, true, 0, clear));
    // The line is only asked within reach.
    bool asked = false;
    assert(!melee_reach(5, 0, false, 0, [&] { asked = true; return false; }));
    assert(!asked);
    // A walk to an NPC that used to stop at the server's 6 now goes on.
    assert(!melee_reach(6, 0, false, 0, clear));

    assert(unit_moving(UnitType::kPlayer, 2, 0) && unit_moving(UnitType::kPlayer, 3, 0) && unit_moving(UnitType::kPlayer, 6, 0));
    assert(!unit_moving(UnitType::kPlayer, 1, 0) && !unit_moving(UnitType::kPlayer, 0xf, 0));
    assert(unit_moving(UnitType::kMonster, 2, 0) && unit_moving(UnitType::kMonster, 0xf, 0));
    assert(!unit_moving(UnitType::kMonster, 1, 0) && !unit_moving(UnitType::kMonster, 3, 0) && !unit_moving(UnitType::kMonster, 6, 0));
    assert(unit_moving(UnitType::kMonster, 8, 0x6e) && unit_moving(UnitType::kMonster, 9, 0x6e));
    assert(!unit_moving(UnitType::kMonster, 8, 0x6d) && !unit_moving(UnitType::kMonster, 10, 0x6e) && !unit_moving(UnitType::kMonster, 4, 0x6e));
    assert(!unit_moving(UnitType::kObject, 2, 0) && !unit_moving(UnitType::kItem, 2, 0));
    return 0;
}
