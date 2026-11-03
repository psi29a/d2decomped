// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Past Normal a hostile monster's swing grows by its players (FUN_005a4f50
// in an expansion game: FUN_005a4f20 on stat 0x64, table 0x6e21f4 =
// 0, 0, 8, 16 .. 56, then n x 8 - 16, in 128ths of each value).

#include <ai.hpp>
#include <monsters.hpp>

#include <cassert>

using namespace d2d::game;

int main() {
    assert(d2d::rules::player_swing_128ths(1) == 0 && d2d::rules::player_swing_128ths(2) == 8 && d2d::rules::player_swing_128ths(8) == 56);
    assert(d2d::rules::player_swing_128ths(9) == 56 && d2d::rules::player_swing_128ths(10) == 64);
    Monster monster;
    monster.stats.a1_min = 100; monster.stats.a1_max = 200; monster.stats.to_hit = 64;
    monster.stats.elements[0] = { 0, 100, 32, 64, 0, "A1" };
    monster.swing_mode = "A1"; monster.element_on[0] = true;
    monster.players = 3; monster.difficulty = 1;
    auto swung = monster.swing_stats();                         // 16 / 128 more
    assert(swung.a1_min == 112 && swung.a1_max == 225 && swung.to_hit == 72 && swung.elements[0].min == 36 && swung.elements[0].max == 72);
    monster.difficulty = 0;
    assert(monster.swing_stats().a1_min == 100);                // Normal: none
    monster.difficulty = 2; monster.align = 2;
    assert(monster.swing_stats().a1_min == 100);                // a player's side: none
    return 0;
}
