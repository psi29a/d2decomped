// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A pet's own swing with a curse's damagepercent (Weaken) in it: FUN_0057b420
// sums stat 25, the pet's own (summons.cpp, baked into A1) and the curse's,
// floors it at -90 (0x57b5a8) and scales both ends before the roll, so the
// blow's physical carries it (Fight::pet_swing). Synthetic rows: no game data.

#include <combat.hpp>
#include <cues.hpp>
#include <d2s_items.hpp>
#include <ds1.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <loot.hpp>
#include <rules.hpp>
#include <test_data.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::game;

int main() try {
    d2d::test::TestData fixture;
    fixture.monster(0, "skeleton");
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
    loot.ground_level = level;
    Fight fight{ &data, level, random, loot, cues };
    Fight::Hero hero;
    fight.act_as(hero);
    Fight::Pet pet;
    pet.monster.type = 0;
    pet.a1_unscaled = { 8, 16 };
    pet.damage_pct = 25;                                              // its own: A1 8..16 + 25 % = 10..20
    pet.monster.stats.a1_min = 10; pet.monster.stats.a1_max = 20;
    pet.monster.stats.to_hit = 30;
    const auto plain = fight.pet_swing(pet);
    assert(plain.min == 10 && plain.max == 20 && plain.attack_rating == 30);
    pet.buff_stats[d2d::d2s::kDamagePercent] = -33;                   // Weaken: 25 - 33 = -8 % on 8..16
    const auto weak = fight.pet_swing(pet);
    assert(weak.min == 10 + 8 * -33 / 100 && weak.max == 20 + 16 * -33 / 100 && weak.phys_lo == weak.min << 8 && weak.phys_hi == weak.max << 8);
    pet.buff_stats[d2d::d2s::kDamagePercent] = -300;                  // the sum floored at -90: the curse's share -115
    const auto floored = fight.pet_swing(pet);
    assert(floored.min == 10 + 8 * -115 / 100 && floored.max == 20 + 16 * -115 / 100);
    // pet_strike: the curse is in the swing now, so nothing more comes off a pet's blow after the roll; a
    // Shadow (it swings its owner's fighter) still takes its share there.
    pet.buff_stats[d2d::d2s::kDamagePercent] = -33;
    auto& struck = fight.monsters.emplace_back();
    struck.type = 0; struck.id = 900; struck.stats.hit_points = struck.hit_points = 1000; struck.mode = "NU";
    d2d::rules::Blow blow{ .hit = true };
    blow.damage = blow.phys = 10; blow.phys_fine = 10 << 8;
    fight.pet_strike(pet, 0, blow, 1000, true);                       // a pet_swing blow: the curse is in it
    assert(struck.hit_points == 1000 - 10);
    fight.pet_strike(pet, 0, blow, 2000);                             // another fighter's (a Shadow's): its share here
    assert(struck.hit_points == 1000 - 10 - (10 + 10 * -33 / 100));
    std::puts("OK: a pet's swing takes a curse's damagepercent with its own, floored at -90, before the roll; pet_strike adds it only to a blow from another fighter");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_pet_weaken: %s\n", error.what());
    return 1;
}
