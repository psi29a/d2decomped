// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A room's unit list (room +0x74, next +0xe8) runs newest first:
// FUN_0064c2c0 puts a unit at its head as it's made or comes into the room.
// Fight::restamp gives a unit seen in a new room the next stamp, and
// rules::room_order lists each room by stamp, highest first (the index on a
// tie, newest first). A unit that crosses to another level is stamped anew
// there, whatever room index it lands in. No game data: the rooms are built here.
#include <ds1.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <level.hpp>
#include <monsters.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <utility>
#include <vector>

using namespace d2d::game;

int main() try {
    // The rule: two rooms, stamps out of index order.
    const std::vector<d2d::rules::RoomUnit> units{ { 0, true, 5 }, { 0, true, 9 }, { 1, true, 2 }, { 0, false, 99 }, { 0, true, 0 } };
    const std::vector<int> rooms{ 0, 1 };
    assert((d2d::rules::room_order(units, rooms) == std::vector<int>{ 1, 0, 4, 2 }));
    const std::vector<d2d::rules::RoomUnit> unstamped{ { 0, true }, { 0, true }, { 0, true } };
    assert((d2d::rules::room_order(unstamped, rooms) == std::vector<int>{ 2, 1, 0 }));   // index, newest first, as before

    GameData data;
    Level field;
    field.ds1 = d2d::ds1::Map(16, 16, 1, 1);
    field.walk.assign(std::size_t(16 * 5) * std::size_t(16 * 5), 0);
    field.rooms.push_back({ .x = 0, .y = 0, .width = 8, .height = 8 });
    field.rooms.push_back({ .x = 8, .y = 0, .width = 8, .height = 8 });
    World world(&data, -1, -1);
    world.level = &field;
    auto& fight = world.fight;
    fight.monsters.resize(3);
    fight.monsters[0].unit.x = 2; fight.monsters[0].unit.y = 2;     // room 0
    fight.monsters[1].unit.x = 3; fight.monsters[1].unit.y = 2;     // room 0
    fight.monsters[2].unit.x = 10; fight.monsters[2].unit.y = 2;    // room 1
    fight.restamp();
    const auto order = [&] {
        std::vector<d2d::rules::RoomUnit> listed;
        for (const auto& monster : fight.monsters) listed.push_back({ room_of(field, monster.unit.x, monster.unit.y), true, monster.unit.room_stamp });
        return d2d::rules::room_order(listed, rooms);
    };
    assert((order() == std::vector<int>{ 1, 0, 2 }));
    // The oldest walks into room 1: its list's head now, room 0 keeps the other.
    fight.monsters[0].unit.x = 9;
    fight.restamp();
    assert((order() == std::vector<int>{ 1, 0, 2 }));
    assert(fight.monsters[0].unit.room_stamp > fight.monsters[2].unit.room_stamp);
    // A step inside its room keeps its place.
    const auto stamp = fight.monsters[0].unit.room_stamp;
    fight.monsters[0].unit.x = 9.5f;
    fight.restamp();
    assert(fight.monsters[0].unit.room_stamp == stamp);
    // The player crosses to another level and lands in a room of the same index: a new room, a new
    // stamp (UnitState::room_level), not the old level's kept.
    auto& hero = *world.actor;
    hero.player.x = 2; hero.player.y = 2;
    fight.restamp();
    const auto here = hero.player.room_stamp;
    Level next = std::move(field);
    world.level = &next;
    fight.monsters.clear();
    fight.restamp();
    assert(hero.player.room_stamp > here && hero.player.room_level == &next);
    std::puts("OK: each room's units newest first by when they came in (FUN_0064c2c0)");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_room_stamps: %s\n", error.what());
    return 1;
}
