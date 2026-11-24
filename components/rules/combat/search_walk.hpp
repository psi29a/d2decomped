// SPDX-License-Identifier: GPL-3.0-or-later
// What a shift-click's search (SearchEnemyXY, FUN_00467660, game.exe 1.14d)
// takes from the client besides the units themselves: the facing it starts
// from (FUN_006238c0) and the order FUN_00641cb0 walks the units in, the near
// rooms' list. cursor-units.md "The skill's own target search".
#pragma once

#include "monsters.hpp"
#include "search_enemy.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace d2d::rules {

// FUN_006238c0: the player's facing in 64 as FUN_00467660 reads it. Standing on
// its path's target (+0x10 / +0x12, both subtiles) it is the path's own facing
// (+0x65); anywhere else the way from its subtile to that target
// (FUN_00621dc0 -> FUN_0064fdc0, rules::direction64). tools/emu/search_facing.py.
[[nodiscard]] inline int search_facing(int unit_x, int unit_y, int target_x, int target_y, int new_dir) {
    if (unit_x == target_x && unit_y == target_y) return new_dir;
    return direction64(unit_x, unit_y, target_x, target_y);
}

// FUN_00467660 over the units as FUN_00641cb0 walks them: the near rooms in
// the list's order (`rooms`, ids as `listed[i].room`), each room's units by
// stamp, the highest first (equal: the later unit first; RoomUnit, room_order).
// A unit in none of the rooms is never reached. Returns the pick's index in
// `units` as they came, -1 none. tools/emu/search_walk.py.
template <class UnitBits>
[[nodiscard]] int search_enemy_xy_walk(int dir, int player_x, int player_y, bool targetable_only, std::span<const SearchUnit> units,
                                       std::span<const RoomUnit> listed, std::span<const int> rooms, UnitBits&& unit_bits) {
    const auto order = room_order(listed, rooms);
    std::vector<SearchUnit> walked;
    walked.reserve(order.size());
    for (const int index : order) walked.push_back(units[std::size_t(index)]);
    const int pick = search_enemy_xy(dir, player_x, player_y, targetable_only, walked, unit_bits);
    return pick < 0 ? -1 : order[std::size_t(pick)];
}

}  // namespace d2d::rules
