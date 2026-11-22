// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// Frozen Armor answers a melee blow whose record's +8 is over 0 (FUN_005cac50,
// 0x5cac80): the physical after the reduction, in 256ths, no absorb on (Taken::physical_fine,
// what Foe::damaged_by now keys on), not the blow's whole-point total. A swing
// whose physical point (FUN_0057b420: at least 1) the flat reduction takes off
// does fire damage with a +8 of 0; half a physical point has a +8 over 0 and a
// whole-point total of 0.

#include <combat.hpp>
#include <monsters.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

int main() try {
    using namespace d2d::rules;
    Rng rng{ 5 };
    Fighter defender = simple_fighter(1, 1, 100, 0);
    defender.dr_flat = 5;                                   // damage reduced by 5: the 1..2 points of physical go

    MonStats fire_only;
    fire_only.level = 10;
    fire_only.a1_min = fire_only.a1_max = 1;
    fire_only.elements[0] = { 1, 100, 20, 20, 0, "A1" };
    const auto burnt = monster_blow(defender, 1, Motion::standing, fire_only, false, rng, false, true);
    assert(burnt.hit && burnt.damage > 0 && burnt.physical_fine <= 0);

    MonStats scratch;
    scratch.level = 10;
    scratch.a1_min = scratch.a1_max = 1;
    defender.dr_flat = 0;
    defender.dr_pct = 60;   // 1..2 points less 60 %: under a whole point
    const auto grazed = monster_blow(defender, 1, Motion::standing, scratch, false, rng, false, true);
    assert(grazed.hit && grazed.physical_fine > 0 && grazed.physical_fine < 256 && grazed.damage == 0);
    std::printf("OK: a swing the flat reduction strips to its fire has a +8 of 0 (%d points of damage), a fraction of a physical point a +8 of %d\n", burnt.damage, grazed.physical_fine);
    return 0;
} catch (const std::exception& error) {
    std::printf("test_frozen_armor_gate: %s\n", error.what());
    return 1;
}
