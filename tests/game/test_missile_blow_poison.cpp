// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A skill-sourced monster missile's poison (andariel_missile -> missile_blow)
// through the real monster_blow: the record's per-frame 256ths over ELen
// frames (FUN_0064b860 slots +0x34 / +0x38 / +0x3c), not a whole-point total
// read back in the MonStats El convention (roll x 10 a frame for 2 x dur frames).
// No game data: the row is typed in (Andariel's poison bolt at level 1:
// 32..64 a frame for 800 frames, 100 to 200 points in all; mm_launch.py of the
// monster-missile card ran game.exe's record).

#include <ai.hpp>
#include <combat.hpp>
#include <gamedata.hpp>
#include <rules.hpp>

#include <cassert>

using namespace d2d::game;

int main() {
    const GameData::MissileInfo info;
    const auto stats = missile_blow(info, d2d::rules::MissileDamage{ .etype = 3, .elo = 32, .ehi = 64, .elen = 800 }, 30);
    assert(stats.poison_min == 32 && stats.poison_max == 64 && stats.poison_len == 800);
    assert(stats.elements[0].type < 0);                         // not an El row
    d2d::rules::Rng rng(11);
    d2d::rules::Fighter defender;
    auto taken = d2d::rules::monster_blow(defender, 1, d2d::rules::Motion::standing, stats, true, rng, true);
    for (int i = 0; i < 50 && !taken.hit; ++i) taken = d2d::rules::monster_blow(defender, 1, d2d::rules::Motion::standing, stats, true, rng, true);
    assert(taken.hit && taken.poison >= 32 && taken.poison <= 64 && taken.poison_ticks == 800);   // up to 200 points, not 8,812
    const auto fire = missile_blow(info, d2d::rules::MissileDamage{ .etype = 0, .elo = 5 << 8, .ehi = 7 << 8, .elen = 0 }, 30);
    assert(fire.elements[0].type == 0 && fire.elements[0].min == 5 && fire.elements[0].max == 7 && fire.poison_max == 0);
    return 0;
}
