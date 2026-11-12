// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A quest death (FUN_005dfbf0, which calls FUN_0065a950 and FUN_0065ac70)
// takes monsters in the unit finder's rooms: the boss's own room when the
// square of the radius lies inside it (FUN_0065a6b0), otherwise the near
// list. FUN_0065a710 lets every near room through (bugs.md #23). A monster
// in no such room is missed, however close. Within one room the draws
// run newest first: FUN_0065ac70 walks the room's unit list from its head
// (room +0x74, next at +0xe8), and FUN_0064c2c0 inserts there. Andariel's
// hook (FUN_005dfe00) is radius 35, and each draw takes the next roll of
// her seed. No game data: the rooms are built here.

#include <ai.hpp>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <ds1.hpp>
#include <exception>
#include <gamedata.hpp>
#include <rules.hpp>
#include <world.hpp>

using namespace d2d::game;

namespace {

constexpr int kTiles = 24;
constexpr int kAndariel = 0x9c;

float centre(int subtile) { return (float(subtile) + 0.5f) / 5; }

Monster foe_at(int sub_x, int sub_y) {
    Monster foe;
    foe.type = 1;
    foe.align = 0;
    foe.hit_points = 100;
    foe.unit.x = centre(sub_x);
    foe.unit.y = centre(sub_y);
    return foe;
}

}  // namespace

int main() try {
    GameData data;
    data.mon_bin.assign(kAndariel + 1, 0);
    Level field;
    field.ds1 = d2d::ds1::Map(kTiles, kTiles, 1, 1);
    field.walk.assign(std::size_t(kTiles * 5) * std::size_t(kTiles * 5), 0);
    // Room 0 is tiles [0, 8). Room 1 shares its right edge (gap 0), so it is
    // near. Room 2 starts 6 tiles below room 0, so it is not near. Tiles
    // 8..13 on y are in no room.
    field.rooms.push_back({ .x = 0, .y = 0, .width = 8, .height = 8 });
    field.rooms.push_back({ .x = 8, .y = 0, .width = 8, .height = 8 });
    field.rooms.push_back({ .x = 0, .y = 14, .width = 8, .height = 8 });
    World world(&data, -1, -1);
    world.level = &field;
    auto& fight = world.fight;
    // Andariel stands on subtile (20, 39), the bottom of room 0. Radius 35
    // sticks out of that room, so the near list is used.
    fight.monsters.push_back(foe_at(20, 20));    // room 0, 19 off
    fight.monsters.push_back(foe_at(45, 39));    // room 1, 25 off
    fight.monsters.push_back(foe_at(20, 45));    // no room, 6 off
    fight.monsters.push_back(foe_at(20, 70));    // room 2, 31 off
    fight.monsters.push_back(foe_at(79, 39));    // room 1, 59 off
    fight.monsters.push_back(foe_at(18, 30));    // room 0, older of the two
    fight.monsters.push_back(foe_at(22, 30));    // room 0, newest
    Monster andariel;
    andariel.type = 0;
    andariel.unit.x = centre(20);
    andariel.unit.y = centre(39);
    // Room 0, then room 1. In room 0 the newest monster is drawn first, so
    // it takes the first seed roll (Andariel: 1 + rand(50)).
    d2d::rules::Rng rolls = andariel.seed;
    const auto due = [&] { return std::uint32_t(1 + rolls(50)) * 40; };
    const auto newest_due = due();
    const auto older_due = due();
    const auto first_due = due();
    const auto side_due = due();
    fight.quest_death(andariel, 0);
    assert(fight.monsters[0].killself_at == first_due);
    assert(fight.monsters[1].killself_at == side_due);
    assert(fight.monsters[2].killself_at == 0);
    assert(fight.monsters[3].killself_at == 0);
    assert(fight.monsters[4].killself_at == 0);
    assert(fight.monsters[5].killself_at == older_due);
    assert(fight.monsters[6].killself_at == newest_due);
    std::printf("quest death rooms: the near rooms, newest in a room first\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_quest_death_rooms: %s\n", error.what());
    return 1;
}
