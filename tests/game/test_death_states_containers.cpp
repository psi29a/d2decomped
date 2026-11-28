// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Fight::die (FUN_0057f330 -> FUN_00639fb0) prunes the player's separately
// modeled state containers (charges, aura, curse/mark, regen, potions, shrine
// boost, scalar states) by each state's plrstaydeath, and leaves the merc's
// own containers alone (a separate, living unit). Made-up rows (test_data.hpp):
// the rule, not the game's numbers. Runs with no game data (unlike
// test_death_states.cpp, which also replays the real skill table).

#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <level.hpp>
#include <test_data.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>

using namespace d2d::game;

int main() try {
    d2d::test::TestData test;
    constexpr int kAura = 1, kCurse = 2, kCharge = 3;
    test.skill(kAura, "Aura1").aura = true;
    test.skill(kAura, "Aura1").aurastate = "myaura";
    test.skill(kCurse, "Curse1").auratarget = "mycurse";
    test.skill(kCharge, "Charge1").progressive = true;
    test.skill(kCharge, "Charge1").aurastate = "mycharge";
    for (const auto& name : { "myaura", "mycurse", "mycharge", "healthpot", "manapot", "antidote", "thawing",
                              "staminapot", "shrine_stamina", "cold", "inferno", "stunned", "nomanaregen", "skilldelay" })
        test.state(name);
    test.state("mycurse", true);                     // the curse state: curse = true, plrstaydeath stays false
    test.data.state_names.resize(16);
    for (int row = 0; auto& [name, info] : test.data.states) info.row = row++;
    test.data.shrines.push_back({});                  // Shrines.txt row 0: boost.shrine == 0 means "no boost" (Fight::die's shrine_row > 0 guard)
    test.data.shrines.push_back({ .code = 14 });      // Shrines.txt row 1: shrine_stamina (shrine_state(14))

    Level open;
    open.ds1 = d2d::ds1::Map(16, 16, 1, 1);
    open.walk.assign(80 * 80, 0);
    World world(&test.data, -1, -1);
    world.level = &open;
    world.fight.enter(&open);
    world.fight.skill_level = [](int) { return 1; };
    world.fight.skill_base = world.fight.skill_level;
    auto& hero = *world.actor;
    constexpr std::uint32_t kNow = 1000;
    hero.aura = kAura;
    hero.curses[0] = { kCurse, 1, kNow + 10000 };
    hero.marks[0] = { kCurse, 1, kNow + 10000 };
    hero.charges.push_back({ kCharge, 1, 3, kNow + 10000 });
    hero.regen = { { 1, 0, kNow + 10000, false }, { 0, 1, kNow + 10000, false } };
    hero.potion_states[0] = { { { d2d::d2s::kPoisonResist, 50 } }, kNow + 10000 };
    hero.potion_states[1] = { { { d2d::d2s::kColdResist, 50 } }, kNow + 10000 };
    hero.stamina_pot = { 100, kNow + 10000 };
    hero.chilled = hero.burning = hero.stunned = hero.skill_delay = kNow + 10000;
    hero.no_mana_regen = true;
    hero.boost = Fight::Hero::Boost{ 1, {}, kNow + 10000, 1 };
    hero.merc.emplace();
    hero.merc->hit_points = 100;
    hero.curses[1] = { kCurse, 1, kNow + 10000 };
    hero.merc_potion_states[0] = { { { d2d::d2s::kPoisonResist, 50 } }, kNow + 10000 };
    hero.merc_buff = { kCharge, 1, kNow + 10000 };
    hero.merc_cry = { kCharge, 1, kNow + 10000 };

    world.fight.die(kNow);
    assert(hero.curses[0].skill < 0 && hero.marks[0].skill < 0 && hero.charges.empty() && hero.regen.empty());
    assert(hero.potion_states[0].until == 0 && hero.potion_states[1].until == 0 && hero.stamina_pot.until == 0 && hero.boost.until == 0);
    assert(hero.aura == 0 && hero.chilled == 0 && hero.burning == 0 && hero.stunned == 0 && hero.skill_delay == 0 && !hero.no_mana_regen);
    assert(hero.merc && hero.merc->hit_points == 100 && hero.curses[1].skill == kCurse && hero.merc_potion_states[0].until > kNow);
    assert(hero.merc_buff.skill == kCharge && hero.merc_cry.skill == kCharge);
    std::puts("OK: Fight::die pruned every modeled player container by plrstaydeath, and left the merc's alone");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_death_states_containers: %s\n", error.what());
    return 1;
}
