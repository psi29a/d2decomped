// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Holy Bolt's hit (FUN_005a9fb0) puts the missile row's ProgOverlay (+0x36,
// over 0) on the pet it heals as a one-shot (FUN_00621e40), whatever the
// amount: here calc1 and calc2 are 0, so the life stays and the overlay
// still comes. Made-up data (test_data.hpp); test_holy_bolt has the real rows.

#include <ai/units.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <overlays.hpp>
#include <test_data.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <utility>

using namespace d2d::game;

int main() try {
    d2d::test::TestData test;
    test.skill(101, "Holy Bolt").target_pet = true;
    test.monster(0, "pet");
    auto& bolt = test.data.missiles["holybolt"];
    bolt.hit_func = 7;
    bolt.hit_par1 = 1;
    bolt.prog_overlay = "healing";
    test.data.overlay_rows = { nullptr, &test.data.overlays["healing"] };   // healing is Overlay.txt row 1

    World world(&test.data, -1, -1);
    auto& fight = world.fight;
    Monster pet_monster;
    pet_monster.type = 0;
    pet_monster.id = 42;
    pet_monster.stats.hit_points = 50;
    pet_monster.hit_points = 40;                                             // below full, so any heal would show
    Fight::Pet pet;
    pet.monster = pet_monster;
    world.actor->pets.push_back(std::move(pet));
    fight.monsters.push_back(std::move(pet_monster));

    Missile missile;
    missile.info = &bolt;
    missile.skill = 101;
    missile.level = 1;
    missile.friendly = true;
    missile.seed = d2d::rules::Rng{ 9 };
    const int life = fight.monsters.back().life_fine();
    assert(fight.skill_missile_hits(missile, 0, 1000));                       // it ends there, striking nothing
    assert(fight.monsters.back().life_fine() == life);                         // a heal of 0
    assert((fight.monsters.back().one_shots == d2d::rules::OneShots{ { 1, 1000 } }));
    assert((world.actor->pets.back().monster.one_shots == d2d::rules::OneShots{ { 1, 1000 } }));   // the pet's own copy too
    std::printf("holy bolt overlay: ok\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_holy_bolt_overlay: %s\n", error.what());
    return 1;
}
