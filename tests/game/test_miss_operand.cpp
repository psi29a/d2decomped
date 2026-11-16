// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// miss(row, op)'s misscalc operands (FUN_0064b340): a Missiles.txt row's
// Param1..5, CltParam1..5, sHitPar1..3, cHitPar1..3, dParam1 / 2, Range + LevRange x lvl and the
// pairs by level (a + b x (lvl - 1), dm FUN_00645b20), the physical part
// (FUN_0064af20 / FUN_0064aff0). No game data.

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
    row.param5 = 5; row.clt_params = { 21, 22, 23, 24, 25 }; row.clt_hit_pars = { 31, 32, 33 };
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
    assert(operand(4, 5) == 5);
    assert(operand(5, 5) == 21 && operand(9, 5) == 25);   // CltParam1, CltParam5 (+0x58, +0x68)
    assert(operand(13, 5) == 31 && operand(15, 5) == 33);   // cHitPar1, cHitPar3 (+0x6c, +0x74)
    assert(operand(33, 3) == 65 && operand(35, 3) == 71 && operand(39, 3) == 95);   // a + b x (lvl - 1)
    assert(operand(34, 1) == 21 && operand(36, 1) == 23 && operand(40, 1) == 31);   // dm at level 1: a
    row.min = 3; row.max = 7; row.min_lev[0] = 2; row.max_lev[0] = 3;   // HitShift 8
    assert(operand(24, 5) == 11 && operand(25, 5) == 19);   // codes 24 / 25: 3 + 2 x 4, 7 + 3 x 4
    assert(operand(26, 5) == 11 << 8 && operand(27, 5) == 19 << 8);   // 26 / 27 in 256ths
    assert(skill_text::miss_operand(game_data, env, 0, 8, 0, 5) == 0);   // no such row
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_miss_operand: %s\n", error.what());
    return 1;
}
