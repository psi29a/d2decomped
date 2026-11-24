// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// FUN_00463740 (near_room_holder): the room holding a spot is the room's own,
// else the first of its near list, and the near list holds the next level's
// rooms: the answer names that room's level too, not only an index into this
// level's rooms. A near room not come up yet (room1 +0 null) is skipped.
// An act-offset level (world_x) tests in act subtiles, not its own.

#include <gamedata.hpp>
#include <level.hpp>
#include <spawning.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::game;

namespace {

Level level_of(int world_x, int world_y) {
    Level level{};
    level.world_x = world_x;
    level.world_y = world_y;
    level.rooms.resize(1);
    level.rooms[0].x = 0; level.rooms[0].y = 0; level.rooms[0].width = 8; level.rooms[0].height = 8;
    return level;
}

}  // namespace

int main() try {
    GameData data;
    Level here = level_of(100, 40);
    const Level west = level_of(92, 40);                         // its (0, 0) 8 tiles west of ours, its one room beside ours
    here.nearby.push_back({ &west, -8, 0 });

    // Own bounds: the room itself.
    const auto own = near_room_holder(data, here, 0, 10, 10);
    assert(own && own->level == &here && own->room == 0);
    // Past the west edge (subtile -3, tile -1): the neighbour's room, a different level.
    const auto beside = near_room_holder(data, here, 0, -3, 10);
    assert(beside && beside->level == &west && beside->room == 0);
    // Past both: none.
    assert(!near_room_holder(data, here, 0, -60, 10));

    // Rooms come up: the west level's room not up is skipped.
    Spawning spawning;
    spawning.levels[&here].order = { 0 };
    assert(!near_room_holder(data, here, 0, -3, 10, &spawning));
    spawning.levels[&west].order = { 0 };
    const auto come_up = near_room_holder(data, here, 0, -3, 10, &spawning);
    assert(come_up && come_up->level == &west);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_near_holder_level: %s\n", error.what());
    return 1;
}
