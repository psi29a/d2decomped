// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A summon whose life wraps to 0 or below (bugs.md 123, FUN_005c4470's
// MulDiv in 32 bits) keeps it as stats 7 / 6; the dead test is the mode
// (FUN_005541b0), so it lives and thinks, and a landed hit kills it, life
// <= 0 after one (0x57c8e2): `lifeless`, as a zero-life monster. A sound
// summon isn't. Synthetic rows: no game data.
#include <ai.hpp>
#include <cues.hpp>
#include <d2s_items.hpp>
#include <ds1.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <loot.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <test_data.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::game;

int main() try {
    d2d::test::TestData fixture;
    auto& zombie = fixture.monster(0, "zombie", -1, { 0, 0, 0 });
    for (auto& per_difficulty : zombie.diff) { per_difficulty.min_hp = per_difficulty.max_hp = 100; }
    for (int level = 0; level <= 31; ++level) fixture.mon_level(level, 4000, 1, 1, 1);
    auto& revive = fixture.skill(95, "Revive");
    revive.srvdofunc = d2d::rules::ServerDoFunction::kRevive;
    revive.target_corpse = true;
    auto& data = fixture.data;

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
    corpse.seed = d2d::rules::Rng{ 0xabcd };
    const auto raise = [&](const char* calc1) {
        revive.calc[0] = d2d::rules::compile_calc(calc1, data.skills.names);
        hero.pets.clear();
        fight.monsters = { corpse };
        fight.summon_one(revive, 0, 1, fight.calc_env(), 12, 12, 1000, 0);
        assert(hero.pets.size() == 1);
        return &hero.pets.back();
    };
    // 4000 life x 3000 % wraps in 32 bits (a negative life).
    assert(d2d::rules::summon_life(4000 << 8, 3000) <= 0);
    auto* pet = raise("3000");
    assert(pet->monster.lifeless && pet->monster.alive());
    fight.pet_hurt(*pet, 0, 2000);                                   // a landed hit of no damage
    assert(!pet->monster.alive());
    pet = raise("0");
    assert(!pet->monster.lifeless);
    fight.pet_hurt(*pet, 0, 2000);
    assert(pet->monster.alive());
    std::puts("OK: a summon whose life wraps lives till a landed hit (bugs.md 123)");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_summon_wrapped_life: %s\n", error.what());
    return 1;
}
