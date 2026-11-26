// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// FUN_0066c370: a room's near list takes, for each vis slot its +0x28 holds
// (walk-across or warp), the rooms of that slot's level whose +0x28 holds
// the bit of a slot of theirs naming this level (FUN_0066c220 ->
// FUN_0066be80), and for a walk across only those close to it. A room of
// the level next door that is close but lacks the bit isn't taken. Two
// made-up levels side by side; no game files.
#include <ds1.hpp>
#include <gamedata.hpp>
#include <level.hpp>
#include <test_data.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <initializer_list>
#include <memory>
#include <utility>

using namespace d2d::game;

namespace {

// A level of 8 x 8 rooms at act tile (world_x, 0), each room at (0, 8 k), flags as given.
std::unique_ptr<Level> level_of(int id, int world_x, std::initializer_list<std::uint32_t> flags) {
    auto level = std::make_unique<Level>();
    level->id = id;
    level->world_x = world_x;
    level->ds1 = d2d::ds1::Map(8, 8 * int(flags.size()), 0, 0);
    int row = 0;
    for (const auto flag : flags) {
        auto& room = level->rooms.emplace_back();
        room.x = 0, room.y = 8 * row++, room.width = 8, room.height = 8;
        level->room_flags.push_back(flag);
    }
    return level;
}

}  // namespace

int main() try {
    d2d::test::TestData test_data;
    auto& data = test_data.data;
    // Level 2 (act tiles 100..107) west of level 3 (108..115); each names the other in Vis0, Warp -1.
    // Level 3's room 0 lies beside level 2's room but lacks the slot bit; its room 1 (diagonal, close) has it.
    data.levels[2] = level_of(2, 100, { 0x10 });
    data.levels[3] = level_of(3, 108, { 0, 0x10 });
    Level& west = *data.levels[2];
    Level& east = *data.levels[3];
    west.vis[0] = 3; west.vis_warp[0] = -1;
    east.vis[0] = 2; east.vis_warp[0] = -1;
    west.nearby.push_back({ &east, 8, 0 });
    east.nearby.push_back({ &west, -8, 0 });

    const auto list = near_rooms(data, west, 4.5f, 4.5f);
    for (const auto& [level, room] : list) std::printf("near: level %d room %d\n", level->id, room);
    const auto has = [&](const Level* level, int room) { return std::ranges::contains(list, std::pair{ level, room }); };
    assert(has(&west, 0));
    assert(has(&east, 1));                                      // close, and its slot bit names level 2
    assert(!has(&east, 0));                                     // close, but no bit: FUN_0066be80 skips it
    std::puts("OK: a walk across takes the next level's close rooms that hold its slot bit (FUN_0066c370)");
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
