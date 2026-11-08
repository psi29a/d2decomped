// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A monster missile whose row names a Skill carries that skill's physical,
// not the row's (FUN_0064b860's Skill branch: FUN_00647bc0 / FUN_00647d00).
// No game data: the row and the skill are typed in.

#include <ai.hpp>
#include <gamedata.hpp>
#include <skills.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <vector>

using namespace d2d::game;

int main() try {
    GameData game_data;
    auto& row = game_data.missiles["bolt"];
    row.name = "bolt"; row.min = 40 << 8; row.max = 50 << 8; row.range = 10;
    d2d::rules::Skill carried;
    carried.id = 7; carried.mindam = 3; carried.maxdam = 5; carried.mindam_lev = { 1, 1, 1, 1, 1 }; carried.hitshift = 8;
    const Monster monster;
    std::vector<Missile> missiles;
    andariel_missile(game_data, monster, "bolt", 1, 0, 0, missiles, 4, &carried);
    assert(missiles.size() == 1 && missiles[0].src.a2_min == 40 && missiles[0].src.a2_max == 50);   // no Skill on the row: its own
    row.skill = "carried";
    andariel_missile(game_data, monster, "bolt", 1, 0, 0, missiles, 4, &carried);
    assert(missiles[1].src.a2_min == 6 && missiles[1].src.a2_max == 6);   // 3 + a point a level past 1; the max no lower
    assert(missiles[1].skill == 7);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_carried_missile: %s\n", error.what());
    return 1;
}
