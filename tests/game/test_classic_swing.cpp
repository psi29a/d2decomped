// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A classic game's swing (FUN_005a4f50 0x5a5112, game +0x70 0): past Normal
// a monster whose MonStats Align isn't 1 swings for x 10 / 12 damage and
// x 10 / 15 to-hit, its elements as they are, no player-count growth.

#include <ai.hpp>
#include <monsters.hpp>

#include <cassert>

using namespace d2d::game;

int main() {
    Monster monster;
    monster.stats.a1_min = 100; monster.stats.a1_max = 200; monster.stats.a2_min = 7; monster.stats.to_hit = 64;
    monster.stats.elements[0] = { 0, 100, 32, 64, 0, "A1" };
    monster.swing_mode = "A1"; monster.element_on[0] = true;
    monster.players = 3; monster.difficulty = 1; monster.classic = true;
    auto swung = monster.swing_stats();
    assert(swung.a1_min == 83 && swung.a1_max == 166 && swung.a2_min == 5 && swung.to_hit == 42);
    assert(swung.elements[0].min == 32 && swung.elements[0].max == 64);   // elements as they are, no growth
    monster.difficulty = 0;
    assert(monster.swing_stats().a1_min == 100 && monster.swing_stats().to_hit == 64);   // Normal: none
    monster.difficulty = 2; monster.row_align = 1;
    assert(monster.swing_stats().a1_min == 100);                // Align 1 (a summon, a hireling): none
    monster.row_align = 2;
    assert(monster.classic_stats().a1_max == 166);              // Align 2 (cow, camel) is cut
    monster.classic = false; monster.row_align = 0;
    assert(monster.swing_stats().a1_min == 112);                // expansion: the player growth instead
    return 0;
}
