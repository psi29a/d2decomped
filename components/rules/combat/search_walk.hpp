// SPDX-License-Identifier: GPL-3.0-or-later
// What a click's own target search (game.exe 1.14d) takes from the client
// besides the units themselves: SearchEnemyXY's (FUN_00467660) facing
// (FUN_006238c0) and the order FUN_00641cb0 walks the units in, SearchEnemyNear's
// (FUN_00467490) order in the unit finder (FUN_0065ac70): the near rooms' list,
// each room's units newest first. cursor-units.md "The skill's own target search".
#pragma once

#include "monsters.hpp"
#include "search_enemy.hpp"

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

namespace d2d::rules {

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

// FUN_00467490 over the units as the finder FUN_0065ac70 walks them (the near list's rooms
// in order, each room's units by stamp as above): the first of equally near units is the
// first walked. A unit in none of the rooms is never found (it stays a candidate for the
// click's `target` only, as an index). Returns the pick's (or the target's) index in
// `units` as they came, -1 none; the spot as search_enemy_near leaves it.
// tools/emu/search_walk_near.py.
[[nodiscard]] inline int search_enemy_near_walk(bool target_corpse, bool iron_golem, bool town, int self, int target, int& spot_x, int& spot_y,
                                                std::span<const NearUnit> units, std::span<const RoomUnit> listed, std::span<const int> rooms) {
    std::vector<int> origin = room_order(listed, rooms);
    std::vector<NearUnit> walked;
    walked.reserve(units.size());
    for (const int index : origin) walked.push_back(units[std::size_t(index)]);
    for (std::size_t index = 0; index < units.size(); ++index)
        if (std::ranges::find(origin, int(index)) == origin.end()) {
            origin.push_back(int(index));
            walked.push_back(units[index]);
            walked.back().hit = false;   // not found: the finder never reaches it
        }
    const auto where = [&](int index) { return index < 0 ? -1 : int(std::ranges::find(origin, index) - origin.begin()); };
    const int pick = search_enemy_near(target_corpse, iron_golem, town, where(self), where(target), spot_x, spot_y, walked);
    return pick < 0 ? -1 : origin[std::size_t(pick)];
}

}  // namespace d2d::rules
