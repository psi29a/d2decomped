// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A monster missile whose row names a Skill carries that skill's physical,
// not the row's (FUN_0064b860's Skill branch: FUN_00647bc0 / FUN_00647d00).
// No game data: the row and the skill are typed in.

#include <ai.hpp>
#include <ai/units.hpp>
#include <combat.hpp>
#include <gamedata.hpp>
#include <rules.hpp>
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
    // Blockable from the record's final physical (FUN_005ad730: can-block is record + 8, the carried skill's physical when the row names a Skill).
    // A Death Mauler's maul (DeathMaul): the row has no physical, the skill has (MinDam 10, MaxDam 20, MinLevDam1 4, HitShift 8).
    row.min = 0; row.max = 0;
    d2d::rules::Skill maul;
    maul.id = 8; maul.mindam = 10; maul.maxdam = 20; maul.mindam_lev = { 4, 0, 0, 0, 0 }; maul.maxdam_lev = { 4, 0, 0, 0, 0 }; maul.hitshift = 8;
    andariel_missile(game_data, monster, "bolt", 1, 0, 0, missiles, 4, &maul);
    assert(missiles[2].src.a2_min == 22 && missiles[2].src.a2_max == 32 && !missiles[2].src.unblockable);   // the row's 0 does not make the maul unblockable
    d2d::rules::Skill none;                                              // a carried skill with no physical: the record's physical is 0
    none.id = 9;
    row.min = 40 << 8; row.max = 50 << 8;                                // even when the row names a physical
    andariel_missile(game_data, monster, "bolt", 1, 0, 0, missiles, 4, &none);
    assert(missiles[3].src.a2_max == 0 && missiles[3].src.unblockable);
    row.skill.clear();
    row.min = 0; row.max = 0;                                            // no Skill: the row's own (a Fallen Shaman's fire has none)
    andariel_missile(game_data, monster, "bolt", 1, 0, 0, missiles, 4, &maul);
    assert(missiles[4].src.unblockable);
    for (const int index : { 2, 3, 4 }) {                                // through the real monster_blow against Block 75, 400 shots
        auto shot = missiles[std::size_t(index)].src;
        shot.no_hit_roll = true;
        d2d::rules::Fighter defender;
        defender.block = 75;
        d2d::rules::Rng rng(11);
        int blocked = 0;
        for (int trial = 0; trial < 400; ++trial) blocked += d2d::rules::monster_blow(defender, 1, d2d::rules::Motion::standing, shot, true, rng, true).blocked ? 1 : 0;
        assert((blocked > 0) == (index == 2));
    }
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_carried_missile: %s\n", error.what());
    return 1;
}
