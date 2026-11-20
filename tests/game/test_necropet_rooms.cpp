// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A NecroPet's foe (FUN_005dd7f0 -> FUN_005dd0b0 mode 5) comes from its near
// rooms' units, in their lists' order: a monster in a room off that list
// isn't seen, however close. The owner's path words its follow compares are
// the path's target and the target it was last worked out to (+0x10 / +0x18,
// SP1 / SP3: Hero::target_x, player.goal_x). No game data.
#include <ai.hpp>
#include <ds1.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::game;

int main() try {
    GameData data;
    data.monsters.types.resize(2);
    auto& skeleton = data.monsters.types[0];
    skeleton.id = "necroskeleton"; skeleton.ai_name = "NecroPet"; skeleton.size = 1; skeleton.velocity = 6; skeleton.run = 6; skeleton.melee_rng = 1;
    for (auto& per_difficulty : skeleton.diff) per_difficulty.aidel = 5;
    auto& zombie = data.monsters.types[1];
    zombie.id = "zombie"; zombie.size = 1; zombie.threat = 4;
    data.mon_npc.resize(2);
    Level field;
    field.id = 2;
    field.ds1 = d2d::ds1::Map(30, 8, 1, 1);
    field.walk.assign(std::size_t(30 * 5) * std::size_t(8 * 5), 0);
    // Rooms 0 and 1 share an edge (near); room 2 starts six tiles past room 1 (under six is near).
    field.rooms.push_back({ .x = 0, .y = 0, .width = 8, .height = 8 });
    field.rooms.push_back({ .x = 8, .y = 0, .width = 8, .height = 8 });
    field.rooms.push_back({ .x = 22, .y = 0, .width = 8, .height = 8 });
    World world(&data, -1, -1);
    world.level = &field;
    auto& fight = world.fight;
    fight.mon_level = &field;
    const auto listed = near_rooms(data, field, 15.9f, 4.5f);
    assert(listed.size() == 2);                                      // rooms 1 and 0
    auto& hero = *world.actor;
    hero.player.x = 12; hero.player.y = 4.5f;
    hero.target_x = hero.player.goal_x = 12; hero.target_y = hero.player.goal_y = 4.5f;
    auto& pet = hero.pets.emplace_back();
    pet.where = &field;
    pet.monster.type = 0;
    pet.monster.hit_points = pet.monster.stats.hit_points = 50;
    pet.monster.unit.x = 15.9f; pet.monster.unit.y = 4.5f;
    Monster in_room, off_list;
    for (auto* monster : { &in_room, &off_list }) { monster->type = 1; monster->hit_points = monster->stats.hit_points = 50; monster->unit.y = 4.5f; }
    in_room.id = 1; in_room.unit.x = 9.2f;                         // room 1, 33 subtiles off
    off_list.id = 2; off_list.unit.x = 22.2f;                      // room 2, 31 subtiles off: nearer, but not in a near room
    fight.monsters = { in_room, off_list };
    fight.restamp();
    const Crowd crowd;
    fight.necropet_turn(pet, 1000, 0.04f, crowd);
    assert(pet.target == 0 && pet.chase);                          // after the one in its near rooms
    std::puts("OK: a NecroPet's foe comes from its near rooms only, in their order");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_necropet_rooms: %s\n", error.what());
    return 1;
}
