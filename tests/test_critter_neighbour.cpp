// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A chicken's or bunny's nearest unit, FUN_0046c600 (tools/emu/critter_neighbour.py).
#include <critters.hpp>
#include <monster_ids.hpp>

#include <cassert>

using namespace d2d::rules;

int main() {
    constexpr int kZombie = 2;
    // A live critter: any player and any monster but a chicken or bunny, live or
    // dead, since game.exe's dead test reads the critter (bugs.md row 110).
    assert(critter_neighbour(false, kCritterPlayer, 0));
    assert(critter_neighbour(false, kCritterMonster, kZombie));
    assert(critter_neighbour(false, kCritterMonster, monster_ids::kAkara));
    assert(critter_neighbour(false, kCritterMonster, monster_ids::kRat));
    assert(!critter_neighbour(false, kCritterMonster, monster_ids::kChicken));
    assert(!critter_neighbour(false, kCritterMonster, monster_ids::kBunny));
    for (int type = 2; type <= 5; ++type) assert(!critter_neighbour(false, type, kZombie));   // objects, missiles, items, tiles
    // A dead critter counts nothing.
    assert(!critter_neighbour(true, kCritterPlayer, 0));
    assert(!critter_neighbour(true, kCritterMonster, kZombie));
    return 0;
}
