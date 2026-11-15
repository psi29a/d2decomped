// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// FUN_0057c1e0 (0x57c1e0) scales the record fields whose row of the table
// at 0x732980 has +0x20 set: poison damage (row 8, +0x28) is, the poison
// length (row 7, +0x2c) isn't.
#include <combat.hpp>
#include <monsters.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

int main() try {
    d2d::rules::MonStats attacker;
    attacker.a1_min = attacker.a1_max = 10;
    attacker.elements[0] = { 3, 100, 5, 5, 25, "A1" };     // 50 poison a tick, 50 ticks
    const auto blow = [&](bool prime_evil, int pct) {
        attacker.prime_evil = prime_evil;
        d2d::rules::Fighter defender;
        defender.prime_evil_pct = pct;
        d2d::rules::Rng rng{ 1 };
        return d2d::rules::monster_blow(defender, 1, d2d::rules::Motion::standing, attacker, false, rng, false, true, true);
    };
    const auto plain = blow(false, 400), pet = blow(true, 400), merc = blow(true, 200);
    assert(plain.poison == 50 && plain.poison_ticks == 50);
    assert(pet.poison == 200 && pet.poison_ticks == 50);
    assert(merc.poison == 100 && merc.poison_ticks == 50);
    attacker.elements[0] = {};
    attacker.poison_min = attacker.poison_max = 30;          // its own stats 57 / 58
    attacker.poison_len = 10;
    const auto own = blow(true, 400);
    assert(own.poison == 120 && own.poison_ticks == 10);
    std::printf("prime evil poison: ok\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
