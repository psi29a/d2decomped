// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Before Valkyrie gear, FUN_00573cb0's look has sixteen zero bounds and
// advances nothing; its life roll advances her unit seed once. The order of
// the draws, not the game's numbers, so synthetic rows: no game data.

#include <ai/units.hpp>
#include <cues.hpp>
#include <d2s_items.hpp>
#include <ds1.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <level.hpp>
#include <loot.hpp>
#include <monsters.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <test_data.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>

using namespace d2d::game;

int main() try {
    d2d::test::TestData fixture;
    auto& valkyrie_type = fixture.monster(0, "valkyrie");
    for (auto& per_difficulty : valkyrie_type.diff) { per_difficulty.min_hp = 50; per_difficulty.max_hp = 100; }
    valkyrie_type.no_ratio = true;   // every player summon's row is noRatio: its life as written
    for (int level = 0; level <= 31; ++level) fixture.mon_level(level, 100, 1, 1, 1);
    auto& skill = fixture.skill(80, "Valkyrie");
    skill.srvdofunc = d2d::rules::ServerDoFunction::kValkyrie;
    skill.summon = "valkyrie";
    skill.pettype = "valkyrie";
    skill.petmax.code = { { d2d::rules::Calc::Const, 1 } };
    auto& data = fixture.data;
    data.mon_equip_first.assign(1, -1);   // no MonEquip rows: the seed is observed exactly at the gear boundary

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

    // Her unit seed is the game seed's next step as she's made (FUN_00555230 -> FUN_00552df0), the
    // zero-bound look draws nothing, and FUN_00573cb0's inclusive life-range draw steps it once.
    auto game_seed = fight.spawning.game;
    d2d::rules::Rng expected{ game_seed.next() };
    const auto& type = data.monsters.types[0];
    const auto look = d2d::rules::monster_look(nullptr, type.choices, expected);
    assert(std::ranges::count_if(type.choices, [](std::uint8_t count) { return count > 0; }) == 0);
    (void)look;
    expected.next();

    assert(fight.summon_one(skill, 0, 1, fight.calc_env(), 12, 12, 1000) >= 0);
    assert(hero.pets.size() == 1);
    const auto& actual = hero.pets.front().monster.seed;
    assert(actual.low == expected.low && actual.high == expected.high);
    std::puts("OK: Valkyrie gear starts after her life draw; her zero-bound look draws nothing");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
}
