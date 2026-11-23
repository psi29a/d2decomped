// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A pet's own swing with a curse's damagepercent (Weaken) in it: FUN_0057b420
// sums stat 25, the pet's own (summons.cpp) and the curse's, floors it at -90
// (0x57b5a8) and scales both ends of the unscaled A1 in 256ths before the roll
// (x + MulDiv(x, sum, 100)), so the blow's physical carries it (Fight::pet_swing:
// player_blow's 256ths and phys_pct); a Shadow's owner's fighter
// takes it in the same sum (Fight::shadow_fighter). Synthetic rows: no game data.

#include <combat.hpp>
#include <cues.hpp>
#include <d2s_items.hpp>
#include <ds1.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <loot.hpp>
#include <monsters.hpp>
#include <rules.hpp>
#include <test_data.hpp>

#include <algorithm>
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
    const auto scaled = [](int fine, int pct) { return fine + fine * std::max(pct, -90) / 100; };   // player_blow's ends
    const auto plain = fight.pet_swing(pet);
    assert(plain.phys_lo == 8 << 8 && plain.phys_hi == 16 << 8 && plain.phys_pct == 25 && plain.attack_rating == 30);
    assert(scaled(plain.phys_lo, plain.phys_pct) == 10 << 8 && scaled(plain.phys_hi, plain.phys_pct) == 20 << 8);
    pet.buff_stats[d2d::d2s::kDamagePercent] = -33;                   // Weaken: 25 - 33 = -8 % on 8..16 in 256ths
    const auto weak = fight.pet_swing(pet);
    assert(weak.phys_pct == -8 && scaled(weak.phys_lo, weak.phys_pct) == 1885);   // 7.36 points, not the old whole 7
    pet.buff_stats[d2d::d2s::kDamagePercent] = -300;                  // the sum floored at -90: the curse's share -115
    const auto floored = fight.pet_swing(pet);
    assert(floored.phys_pct == -90 && scaled(floored.phys_lo, floored.phys_pct) == 2048 - 2048 * 90 / 100);   // 205: a tenth of 8 points, kept in 256ths
    // A classic game's pet above Normal: its A1 is cut (classic_swing), and the curse's share is taken off the
    // same cut base.
    Fight::Pet classic_pet = pet;
    classic_pet.mirror = false;
    classic_pet.monster.classic = true; classic_pet.monster.difficulty = 2; classic_pet.monster.row_align = 0;
    classic_pet.damage_pct = 0;
    classic_pet.a1_unscaled = { 120, 120 };
    classic_pet.monster.stats.a1_min = classic_pet.monster.stats.a1_max = 120;
    classic_pet.buff_stats[d2d::d2s::kDamagePercent] = -33;
    auto cut = classic_pet.monster.stats;
    d2d::rules::classic_swing(cut, 2, 0);
    const auto classic_swing_blow = fight.pet_swing(classic_pet);
    assert(cut.a1_max < 120 && classic_swing_blow.phys_lo == cut.a1_min << 8 && classic_swing_blow.phys_pct == -33);
    assert(classic_swing_blow.phys_hi == (cut.a1_max > cut.a1_min ? cut.a1_max << 8 : (cut.a1_min << 8) + 0x100));   // a high not above the low: low + 1 point (0x57b4ea)
    // A Shadow swings its owner's fighter with the curse in its damage % sum before the roll (shadow_fighter),
    // floored at -90 with the rest, and in its kick's; pet_strike adds nothing after the roll.
    pet.buff_stats[d2d::d2s::kDamagePercent] = -33;
    d2d::rules::Fighter owner;
    owner.phys_lo = 10 << 8; owner.phys_hi = 10 << 8; owner.phys_pct = 50; owner.kick_pct = 20;
    const auto shadow = Fight::shadow_fighter(pet, owner);
    assert(shadow.phys_pct == 50 - 33 && shadow.kick_pct == 20 - 33 && shadow.phys_lo == owner.phys_lo);
    owner.phys_pct = -70;
    assert(Fight::shadow_fighter(pet, owner).phys_pct == -90);
    d2d::rules::Target target;
    d2d::rules::Rng roll{ 1 };
    const auto shadow_blow = d2d::rules::player_blow(Fight::shadow_fighter(pet, owner), target, 99, roll);
    if (shadow_blow.hit) assert(shadow_blow.phys == 1);                    // 10 points at -90 %, rolled on that
    auto& struck = fight.monsters.emplace_back();
    struck.type = 0; struck.id = 900; struck.stats.hit_points = struck.hit_points = 1000; struck.mode = "NU";
    d2d::rules::Blow blow{ .hit = true };
    blow.damage = blow.phys = 10; blow.phys_fine = 10 << 8;
    fight.pet_strike(pet, 0, blow, 1000);
    assert(struck.hit_points == 1000 - 10);
    std::puts("OK: a pet's swing, and a Shadow's owner's fighter, take a curse's damagepercent with their own, floored at -90, before the roll");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_pet_weaken: %s\n", error.what());
    return 1;
}
