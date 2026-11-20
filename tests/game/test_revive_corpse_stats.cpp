// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Revive (do 58, FUN_005c56c0) raises the corpse itself: the pet keeps the
// corpse's stats (level, damage, defence, to-hit, MonProp stats) and its
// unit seed, and its life is rolled on that seed, not the game's. A MonStats
// Level of 0 stays 0 (FUN_00573cb0 0x573e18..0x573e70, MonLvl row 0).
// Synthetic rows: no game data.
#include <ai.hpp>
#include <cues.hpp>
#include <d2s_items.hpp>
#include <ds1.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <loot.hpp>
#include <monsters.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <test_data.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <utility>

using namespace d2d::game;

int main() try {
    d2d::test::TestData fixture;
    auto& zombie = fixture.monster(0, "zombie", -1, { 0, 0, 0 });
    for (auto& per_difficulty : zombie.diff) { per_difficulty.min_hp = 100; per_difficulty.max_hp = 200; per_difficulty.a1_min = 50; per_difficulty.a1_max = 60; }
    fixture.mon_level(0, 1, 1, 1, 1);
    fixture.mon_level(1, 7, 6, 8, 2);
    for (int level = 2; level <= 30; ++level) fixture.mon_level(level, 10 * level, 5 * level, 3 * level, level);
    auto& revive = fixture.skill(95, "Revive");
    revive.srvdofunc = d2d::rules::ServerDoFunction::kRevive;
    revive.target_corpse = true;
    auto& data = fixture.data;

    // A MonStats Level of 0 is level 0, its life at row 0 (1 x 100..200 %: 1..2).
    d2d::rules::Rng roll{ 3 };
    const auto level_zero = d2d::rules::monster_stats(data.monsters, 0, 0, roll);
    assert(level_zero.level == 0 && level_zero.hit_points >= 1 && level_zero.hit_points <= 2);

    Level arena;
    arena.id = 2;
    arena.ds1 = d2d::ds1::Map(30, 30, 0, 1);
    arena.walk.resize(150 * 150);
    const Level* level = &arena;
    d2d::rules::Rng random{ 7 };
    Cues cues;
    cues.game_data = &data;
    Loot loot{ &data, level, random, cues };
    Fight fight{ &data, level, random, loot, cues };
    Fight::Hero hero;
    fight.act_as(hero);
    hero.character.stats.values[d2d::d2s::kLevel] = 40;
    hero.player.x = hero.player.y = 10;

    Monster corpse;
    corpse.type = 0;
    corpse.hit_points = 0;
    corpse.mode = "DD";
    corpse.unit.x = corpse.unit.y = 12;
    corpse.stats.level = 30;
    corpse.stats.a1_min = 70; corpse.stats.a1_max = 90;
    corpse.stats.armor_class = 321;
    corpse.stats.to_hit = 222;
    corpse.props.thorns = 5;
    corpse.seed = d2d::rules::Rng{ 0xabcd };
    fight.monsters = { std::move(corpse) };
    fight.summon_one(revive, 0, 1, fight.calc_env(), 12, 12, 1000, 0);
    assert(hero.pets.size() == 1);
    const auto& pet = hero.pets.back().monster;
    assert(pet.stats.level == 30 && pet.stats.a1_min == 70 && pet.stats.a1_max == 90);
    assert(pet.stats.armor_class == 321 && pet.stats.to_hit == 222 && pet.props.thorns == 5);
    // The life roll stepped the corpse's seed (min + rand(max - min + 1) at level 30).
    auto expect = d2d::rules::Rng{ 0xabcd };
    const auto range = d2d::rules::monster_ranges(data.monsters, 0, 0, 30);
    const int life = range.min_hp + expect(range.max_hp - range.min_hp + 1);
    assert(pet.seed.low == expect.low && pet.seed.high == expect.high);
    assert(pet.max_life_fine() == d2d::rules::summon_life(life << 8, 0));
    std::puts("OK: a revived corpse keeps its stats and seed (FUN_005c56c0)");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
