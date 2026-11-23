// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// An exploding barrel (FUN_00584240) walks its own room's unit list: a
// monster within 3 subtiles but across in the next room isn't struck; one in
// its room is, its life taken in 256ths; a barrel of its room under 3 off
// goes off in the walk (FUN_00584330). No game data: the rooms are built here.
#include <ai/units.hpp>
#include <d2s_items.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>
#include <level.hpp>
#include <npc.hpp>
#include <object_ids.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <utility>

using namespace d2d::game;

int main() try {
    GameData data;
    data.monsters.types.resize(1);
    data.monsters.types[0].id = "zombie";
    data.monsters.types[0].size = 1;
    Level field;
    field.id = 2;
    field.ds1 = d2d::ds1::Map(16, 8, 1, 1);
    field.walk.assign(std::size_t(16 * 5) * std::size_t(8 * 5), 0);
    field.rooms.push_back({ .x = 0, .y = 0, .width = 8, .height = 8 });
    field.rooms.push_back({ .x = 8, .y = 0, .width = 8, .height = 8 });
    const auto barrel_at = [&](float x) {
        Npc barrel;
        barrel.root = "objects";
        barrel.object_id = d2d::rules::object_ids::kExplodingBarrel;
        barrel.x = x; barrel.y = 4.1f;
        barrel.size_x = 1;
        barrel.seed = d2d::rules::Rng{ 99 };
        field.npcs.push_back(std::move(barrel));
    };
    barrel_at(7.7f);                                               // room 0, by its east edge
    barrel_at(7.3f);                                               // room 0, two subtiles off
    World world(&data, -1, -1);
    world.level = &field;
    auto& fight = world.fight;
    fight.mon_level = &field;
    world.actor->player.x = 2; world.actor->player.y = 2;
    world.actor->character.stats.values[d2d::d2s::kLife] = 0;      // out of it
    Monster near_one, across;
    for (auto* monster : { &near_one, &across }) {
        monster->type = 0; monster->hit_points = monster->stats.hit_points = 400; monster->stats.level = 5; monster->unit.y = 4.1f;
    }
    near_one.unit.x = 7.5f;                                        // room 0, 1 subtile off
    across.unit.x = 8.1f;                                          // room 1, 2 subtiles off
    fight.monsters = { std::move(near_one), std::move(across) };
    // A merc beside it with a point and a 256th: any blast leaves it under one point, dead (as a player).
    auto& merc = world.actor->merc.emplace();
    merc.type = 0; merc.hit_points = merc.stats.hit_points = 2; merc.unit.x = 7.6f; merc.unit.y = 4.1f;
    merc.set_life_fine(257);
    fight.restamp();
    world.explode(0, 1000);
    assert(merc.hit_points == 0 && merc.life_fine() == 0);
    assert(fight.monsters[0].life_fine() < 400 * 256);
    assert(fight.monsters[1].life_fine() == 400 * 256);
    assert(world.operated.contains({ &field, 1 }));                // the second barrel went off too
    // A unit that walked into the room since the last restamp is listed as its newest: the walk
    // restamps first (FUN_0064c2c0 re-files a unit as it changes rooms).
    const auto stamp = fight.monsters[1].unit.room_stamp;
    fight.monsters[1].unit.x = 7.4f;                               // into room 0, no restamp
    world.operated.clear();
    world.explode(0, 2000);
    assert(fight.monsters[1].unit.room_stamp > stamp && fight.monsters[1].unit.room_stamp > fight.monsters[0].unit.room_stamp);
    std::puts("OK: a barrel's blast walks its own room's list (FUN_00584240)");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_barrel_room: %s\n", error.what());
    return 1;
}
