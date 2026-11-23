// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A pet's get-hit ends (#683): the mode's end (FUN_005a8030) goes to NU,
// whose start (FUN_005a73e0) files the next think aidel on. Before, the pet
// stood in GH for good, as its think (pet_walking) runs only from NU.
// No game data.
#include <ai/units.hpp>
#include <ds1.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <test_data.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::game;

int main() try {
    d2d::test::TestData fixture;
    auto& skeleton = fixture.monster(0, "necroskeleton");
    skeleton.ai_name = "NecroPet"; skeleton.size = 1; skeleton.velocity = 6; skeleton.run = 6; skeleton.melee_rng = 1;
    for (auto& per_difficulty : skeleton.diff) per_difficulty.aidel = 5;
    Level field;
    field.id = 2;
    field.ds1 = d2d::ds1::Map(8, 8, 1, 1);
    field.walk.assign(std::size_t(8 * 5) * std::size_t(8 * 5), 0);
    field.rooms.push_back({ .x = 0, .y = 0, .width = 8, .height = 8 });
    World world(&fixture.data, -1, -1);
    world.level = &field;
    world.fight.mon_level = &field;
    auto& hero = *world.actor;
    hero.player.x = hero.target_x = hero.player.goal_x = 3;
    hero.player.y = hero.target_y = hero.player.goal_y = 3;
    auto& pet = hero.pets.emplace_back();
    pet.where = &field;
    pet.monster.type = 0;
    pet.monster.hit_points = pet.monster.stats.hit_points = 50;
    pet.monster.unit.x = 4; pet.monster.unit.y = 3;
    pet.monster.mode = "GH";
    pet.monster.mode_until = 1040;
    pet.monster.next_act = 0;
    const Crowd crowd;
    world.fight.pets_turn(1000, 0.04f, crowd);
    assert(pet.monster.mode == "GH");                             // still struck
    world.fight.pets_turn(1040, 0.04f, crowd);
    assert(pet.monster.mode == "NU" && pet.monster.next_act == 1040 + 5 * 40);
    std::puts("OK: a struck pet leaves GH at its end, its next think aidel on");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_pet_get_hit: %s\n", error.what());
    return 1;
}
