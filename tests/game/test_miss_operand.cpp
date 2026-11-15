// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// miss(row, op)'s misscalc operands (FUN_0064b340): a Missiles.txt row's
// Param1..4, sHitPar1..3, dParam1 / 2, Range + LevRange x lvl and the
// pairs by level (a + b x (lvl - 1), dm FUN_00645b20). No game data.

#include <gamedata.hpp>
#include <skill_text.hpp>
#include <skills.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::game;

int main() try {
    GameData game_data;
    auto& row = game_data.missiles["bolt"];
    row.name = "bolt";
    row.param1 = 10; row.param2 = 30; row.param3 = 3; row.param4 = 4;
    row.hit_par1 = 11; row.hit_par2 = 12; row.hit_par3 = 13;
    row.dmg_par1 = 50; row.dmg_par2 = 200;
    row.range = 10; row.lev_range = 2;
    game_data.skills.names.missiles["bolt"] = 9;
    const d2d::rules::CalcEnv env;
    const auto operand = [&](int code, int lvl) { return skill_text::miss_operand(game_data, env, 0, 9, code, lvl); };
    assert(operand(0, 5) == 10 && operand(1, 5) == 30 && operand(2, 5) == 3 && operand(3, 5) == 4);
    assert(operand(10, 5) == 11 && operand(11, 5) == 12 && operand(12, 5) == 13);
    assert(operand(16, 5) == 50 && operand(17, 5) == 200);
    assert(operand(18, 5) == 5);
    assert(operand(28, 5) == 20);                        // 2 x 5 + 10
    assert(operand(29, 5) == 130);                       // 10 + 30 x 4
    assert(operand(30, 5) == 20);                        // 10 + 20 x (550 / 11) / 100
    assert(operand(31, 1) == 3);
    assert(operand(37, 3) == 35 && operand(41, 2) == 250);
    assert(operand(42, 5) == 125);                       // 50 + 150 x 50 / 100
    assert(operand(5, 5) == 0 && operand(24, 5) == 0);   // CltParam1, damn: not loaded / built
    assert(skill_text::miss_operand(game_data, env, 0, 8, 0, 5) == 0);   // no such row
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_miss_operand: %s\n", error.what());
    return 1;
}
