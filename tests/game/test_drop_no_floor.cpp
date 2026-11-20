// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A drop with no floor (FUN_00555da0): FUN_0064dea0 keeps the start subtile
// when its search finds nothing (FUN_00555da0's flag, which FUN_0055a550
// passes as 1) only while FUN_00463740 finds a room there; with none,
// FUN_00555da0 returns 0 and FUN_0055a550 makes no item. drop_place says
// which: the spot, the start kept, or nullopt. drop_spot (the corpse-less
// callers' version) still falls back to the dropper's own subtile.

#include <gamedata.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <utility>
#include <vector>

using namespace d2d::game;

int main() try {
    const std::vector<std::uint8_t> no_field;   // no walk field: every spot walks
    // A clear room: the start, (x + 2, y + 3).
    assert(drop_place(no_field, 10, 10, [](int, int) { return 0; }) == std::pair(12, 13));
    // The start blocked by a wall (0x01): the nearest clear one.
    const auto wall_at_start = [](int at_x, int at_y) { return at_x == 12 && at_y == 13 ? 0x01 : 0; };
    assert(drop_place(no_field, 10, 10, wall_at_start) == std::pair(11, 13));
    // Everything in the rooms blocked: the start stays, in a room.
    assert(drop_place(no_field, 10, 10, [](int, int) { return 0x01; }) == std::pair(12, 13));
    // No room anywhere (0x27, FUN_00463740 finds none): no spot, no item.
    assert(!drop_place(no_field, 10, 10, [](int, int) { return 0x27; }));
    assert(drop_spot(no_field, 10, 10, [](int, int) { return 0x27; }) == std::pair(10, 10));
    // (x + 2, y + 3) off the rooms, (x, y) in one and blocked: the dropper's
    // own subtile is the start, kept.
    const auto edge = [](int at_x, int at_y) { return at_x == 10 && at_y == 10 ? 0x01 : at_x == 12 && at_y == 13 ? 0x27 : 0x01; };
    assert(drop_place(no_field, 10, 10, edge) == std::pair(10, 10));
    // A free spot in a room the start is not in (the next room over): found.
    const auto next_room = [](int at_x, int at_y) { return at_x == 9 && at_y == 10 ? 0 : 0x27; };
    assert(drop_place(no_field, 10, 10, next_room) == std::pair(9, 10));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_drop_no_floor: %s\n", error.what());
    return 1;
}
