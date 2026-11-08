// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A unique's colour (FUN_00466360 -> FUN_00477620): rand(30) + 9 on {unit
// id + the name seed's low byte, 666}, one step (the 0xac handler keeps
// only the name seed's low byte, 0x45f387: bugs.md #49).

#include <ai.hpp>
#include <uniques.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::game;

int main() try {
    World world(nullptr, -1, -1);
    Monster monster;
    monster.id = 0x1234;
    monster.name_seed = 0xabcd;
    monster.boss = d2d::rules::Boss::unique;
    assert(world.monster_colour(monster) == 26);                 // (0x1301 * 0x6ac690c5 + 666) low % 30 + 9
    monster.name_seed = 0x11cd;                                  // the high byte doesn't count
    assert(world.monster_colour(monster) == 26);
    std::printf("OK: a unique's colour rolls on {id + the name seed's low byte, 666}\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_unique_colour: %s\n", error.what());
    return 1;
}
