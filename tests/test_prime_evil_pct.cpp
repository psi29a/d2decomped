// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// FUN_0057c060 (0x57c060)'s pair percent: a prime evil's blow on a
// player's pet is 400 %, on a hireling 200 %; FUN_0057c1e0 (0x57c1e0)
// scales each damage field above 0 first, before the reductions.
#include <combat.hpp>
#include <monsters.hpp>

#include <cassert>
#include <cstdio>

int main() {
    d2d::rules::MonStats attacker;
    attacker.a1_min = attacker.a1_max = 10;
    attacker.elements[0] = { 0, 100, 5, 5, 0, "A1" };      // 5 fire
    const auto blow = [&](bool prime_evil, int pct, int resist) {
        attacker.prime_evil = prime_evil;
        d2d::rules::Fighter defender;
        defender.prime_evil_pct = pct;
        defender.res[0] = resist;
        d2d::rules::Rng rng{ 1 };
        return d2d::rules::monster_blow(defender, 1, d2d::rules::Motion::standing, attacker, false, rng, false, true, true);
    };
    assert(blow(false, 100, 0).damage == 15);
    assert(blow(true, 100, 0).damage == 15);                // a player: 100 here
    assert(blow(false, 400, 0).damage == 15);               // not a prime evil
    const auto pet = blow(true, 400, 0), merc = blow(true, 200, 0);
    assert(pet.damage == 60 && pet.raw == 40 * 256 && pet.element_raw[0] == 20);
    assert(merc.damage == 30 && merc.raw == 20 * 256);
    assert(blow(true, 400, 50).damage == 40 + 10);          // the resistance after the scaling
    std::printf("prime evil pair percent: ok\n");
    return 0;
}
