// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The monsters a client has: those standing in the near list of the
// player's room (unit +0x1b4, d2d_act5_rooms_near). FUN_00537b50 sends a
// room's units as it joins that list (FUN_0053a8e0 -> FUN_00571f90) and
// hides a room that leaves it (FUN_0053a9b0): World::view's monsters, not a
// radius. No game data: the rooms are built here.

#include <ai.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::game;

namespace {

Monster monster_at(int id, float x, float y) {
    Monster monster;
    monster.id = id;
    monster.type = 0;
    monster.hit_points = monster.stats.hit_points = 100;
    monster.unit.x = x;
    monster.unit.y = y;
    return monster;
}

bool shown(const View& view, int id) { return std::ranges::any_of(view.monsters, [&](const Monster& monster) { return monster.id == id; }); }

}  // namespace

int main() try {
    GameData data;
    data.monsters.types.resize(1);
    Level field;
    field.ds1 = d2d::ds1::Map(40, 40, 1, 1);
    field.walk.assign(std::size_t(40 * 5) * std::size_t(40 * 5), 0);
    // Room 0 holds the player; room 1 shares its edge (near) and runs on to
    // x 40; room 2 is 12 tiles off room 0 (not near). Tiles y 8..20 are in none.
    field.rooms.push_back({ .x = 0, .y = 0, .width = 8, .height = 8 });
    field.rooms.push_back({ .x = 8, .y = 0, .width = 32, .height = 8 });
    field.rooms.push_back({ .x = 0, .y = 20, .width = 8, .height = 8 });
    World world(&data, -1, -1);
    world.level = &field;
    world.fight.mon_level = &field;
    world.actor->player.x = 4.5f;
    world.actor->player.y = 4.5f;
    world.fight.monsters = { monster_at(1, 6.5f, 4.5f),      // room 0
                             monster_at(2, 37.5f, 4.5f),     // room 1, 33 cells off: near all the same
                             monster_at(3, 4.5f, 22.5f),     // room 2, 18 cells off: not near
                             monster_at(4, 4.5f, 12.5f) };   // in no room
    const auto view = world.view();
    assert(shown(view, 1) && shown(view, 2));
    assert(!shown(view, 3) && !shown(view, 4));
    std::printf("view: the monsters of the player's near rooms\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_view_client_rooms: %s\n", error.what());
    return 1;
}
