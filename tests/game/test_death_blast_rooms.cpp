// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A death blast's area (FUN_0057e090, which calls FUN_0065a950 and
// FUN_0065ac70): the near rooms, or the unit's own room when the square of
// the radius lies inside it (FUN_0065a6b0). FUN_0065a710 lets every near
// room through (bugs.md #23). A monster in no such room is missed. The
// hits in one room run newest first: FUN_0065ac70 walks the room's unit
// list from its head (room +0x74, next at +0xe8), and FUN_0064c2c0 inserts
// there. The Fire Golem's blast (FUN_005a2840) is 100 physical and 100
// fire within 6 subtiles. No game data: the rooms are built here.

#include <ai.hpp>
#include <cassert>
#include <cstdio>
#include <ds1.hpp>
#include <exception>
#include <gamedata.hpp>
#include <utility>
#include <world.hpp>

using namespace d2d::game;

namespace {

constexpr int kTiles = 16;
constexpr int kLife = 1000;

float centre(int subtile) { return (float(subtile) + 0.5f) / 5; }

Monster foe_at(int sub_x, int sub_y) {
    Monster foe;
    foe.type = 1;
    foe.align = 0;
    foe.hit_points = foe.stats.hit_points = kLife;
    foe.unit.x = centre(sub_x);
    foe.unit.y = centre(sub_y);
    return foe;
}

}  // namespace

int main() try {
    GameData data;
    data.monsters.types.resize(2);
    data.monsters.types[0].base = 0;
    data.monsters.by_id.emplace("firegolem", 0);
    Level field;
    field.ds1 = d2d::ds1::Map(kTiles, kTiles, 1, 1);
    field.walk.assign(std::size_t(kTiles * 5) * std::size_t(kTiles * 5), 0);
    // Room 0 is tiles [0, 8). Room 1 starts on the shared edge, so it is near
    // (gap 0). Tile row 8 is in no room.
    field.rooms.push_back({ .x = 0, .y = 0, .width = 8, .height = 8 });
    field.rooms.push_back({ .x = 8, .y = 0, .width = 8, .height = 8 });
    World world(&data, -1, -1);
    world.level = &field;
    auto& fight = world.fight;
    // The golem stands on subtile 35. Radius 6 reaches 29..41, which sticks
    // out of room 0 (it ends at 40), so the near list is used.
    fight.monsters.push_back(foe_at(29, 35));   // room 0, 6 off
    fight.monsters.push_back(foe_at(40, 35));   // room 1, 5 off
    fight.monsters.push_back(foe_at(35, 41));   // no room, 6 off
    // Two more in room 0, pushed oldest first. One point each, so the kill
    // list is the hit order. The newer one (id 40) dies first.
    auto older = foe_at(34, 35);
    older.id = 30;
    older.hit_points = older.stats.hit_points = 1;
    older.no_drop = true;
    auto newer = foe_at(36, 35);
    newer.id = 40;
    newer.hit_points = newer.stats.hit_points = 1;
    newer.no_drop = true;
    fight.monsters.push_back(std::move(older));
    fight.monsters.push_back(std::move(newer));
    auto& pet = world.actor->pets.emplace_back();
    pet.where = world.level;
    pet.monster.type = 0;
    pet.monster.hit_points = 0;
    pet.monster.mode = "DT";
    pet.monster.unit.x = centre(35);
    pet.monster.unit.y = centre(35);
    pet.monster.unit.mode_ms = 0;
    assert(fight.blast_due(pet.monster));
    assert(fight.golem_blast(pet, 4 * 40));
    assert(fight.monsters[0].hit_points == kLife - 200);
    assert(fight.monsters[1].hit_points == kLife - 200);
    assert(fight.monsters[2].hit_points == kLife);
    assert(fight.kills.size() == 2);
    assert(fight.kills[0].id == 40);
    assert(fight.kills[1].id == 30);
    std::printf("death blast rooms: the near rooms, newest in a room first\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_death_blast_rooms: %s\n", error.what());
    return 1;
}
