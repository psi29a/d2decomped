// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A made monster's MonEquip gear: the unit's stats are its base list's plus
// its items' (FUN_00625480), so every item stat reaches what reads it
// (wear_gear_stats): resistances into its target, max life and its percent
// (after a boss's scaling, which is the base list's), the attack rating and
// its percent, fire / lightning / cold / magic damage on every blow (the
// enchants' slots), poison as its own poison stats. No game data.
#include <ai.hpp>
#include <ai/units.hpp>
#include <combat.hpp>
#include <d2s_items.hpp>
#include <gamedata.hpp>
#include <mon_equip.hpp>
#include <monsters.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <utility>

using namespace d2d::game;

int main() try {
    using namespace d2d::d2s;
    GameData data;
    data.monsters.types.resize(1);
    data.monsters.types[0].diff[0].res = { 0, 0, 10, 0, 0, 0 };
    Monster monster;
    monster.type = 0;
    monster.stats.hit_points = 100;
    monster.set_life_fine(80 * 256);
    monster.max_life_frac = 0;
    monster.stats.to_hit = monster.stats.to_hit2 = monster.stats.to_hit_s1 = 200;
    monster.stats.enchants[0] = std::pair{ 3, 6 };                // a fire enchanted boss's
    d2d::rules::GearSum gear;
    gear.sum[kFireResist] = 25; gear.sum[kDamageResist] = 10;
    gear.sum[kMaxLife] = 20; gear.sum[kMaxLifePercent] = 10;
    gear.sum[kToHit] = 15; gear.sum[kToHitPercent] = 50;
    gear.sum[kFireMinDamage] = 1; gear.sum[kFireMaxDamage] = 2;
    gear.sum[kMagicMinDamage] = 4; gear.sum[kMagicMaxDamage] = 9;
    gear.sum[kPoisonMinDamage] = 30; gear.sum[kPoisonMaxDamage] = 40; gear.sum[kPoisonLength] = 75;
    wear_gear_stats(monster, gear);
    const auto target = monster.target(data);
    assert(target.res[2] == 35 && target.res[0] == 10);
    assert(monster.max_life_fine() == (100 + 20 + 10) * 256);      // 20 flat, 10 % of the base 100
    assert(monster.life_fine() == (80 + 30) * 256);
    assert(monster.stats.to_hit == 200 + 15 + 100 && monster.stats.to_hit2 == 315 && monster.stats.to_hit_s1 == 315);
    assert(monster.stats.enchants[0] == (std::pair{ 4, 8 }));     // added to the boss's own
    assert(monster.stats.enchants[4] == (std::pair{ 4, 9 }));     // magic
    assert(!monster.stats.enchants[3]);                            // poison has its own stats
    assert(monster.stats.poison_min == 30 && monster.stats.poison_max == 40 && monster.stats.poison_len == 75);
    // A boss's scaling (FUN_005a0d20) reads its whole max life, the gear's in it, each time.
    d2d::rules::GearSum flat;
    flat.sum[kMaxLife] = 10;
    assert(boss_life_fine(100 * 256, { 50, 0 }, nullptr) == 150 * 256);
    assert(boss_life_fine(100 * 256, { 50, 0 }, &flat) == 165 * 256);        // (100 + 10) x 1.5, base list's
    assert(boss_life_fine(100 * 256, { 50, 20 }, &flat) == (165 + 10) * 256 * 6 / 5);
    std::puts("OK: a monster's gear stats reach it (wear_gear_stats, boss_life_fine)");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_monster_gear_stats: %s\n", error.what());
    return 1;
}
