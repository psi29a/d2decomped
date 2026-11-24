// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A chicken's or bunny's nearest unit, FUN_0046c600 (tools/emu/critter_neighbour.py);
// a critter's walk (FUN_00649970: the toward pather, a bird's 77 steps); the client's
// room lists the rats' corpse walk takes (FUN_0046c570).
#include <critters.hpp>
#include <monster_ids.hpp>
#include <monsters.hpp>

#include <cassert>
#include <utility>
#include <vector>

using namespace d2d::rules;

int main() {
    constexpr int kZombie = 2;
    // A live critter: any player and any monster but a chicken or bunny, live or
    // dead, since game.exe's dead test reads the critter (bugs.md row 110).
    assert(critter_neighbour(false, kCritterPlayer, 0));
    assert(critter_neighbour(false, kCritterMonster, kZombie));
    assert(critter_neighbour(false, kCritterMonster, monster_ids::kAkara));
    assert(critter_neighbour(false, kCritterMonster, monster_ids::kRat));
    assert(!critter_neighbour(false, kCritterMonster, monster_ids::kChicken));
    assert(!critter_neighbour(false, kCritterMonster, monster_ids::kBunny));
    // A critter's walk (FUN_00649970 -> the toward pather, rules::critter_path): a clear line is one
    // point however far (a bird's 77 steps don't cut it); none over 100 off on an axis or at its own
    // subtile; a wall cuts the line, then up to `steps` 8-way steps go round it.
    const auto open = [](int, int) { return false; };
    assert((critter_path(0, 0, 90, 45, 0x4d, open) == std::vector<std::pair<int, int>>{ { 90, 45 } }));
    assert(critter_path(0, 0, 101, 0, 0x4d, open).empty() && critter_path(0, 0, 0, -101, 0x4d, open).empty());
    assert(critter_path(3, 3, 3, 3, 0x4d, open).empty());
    const auto post = [](int sub_x, int sub_y) { return sub_x == 10 && sub_y == 0; };   // one blocked subtile on the line
    const auto around = critter_path(0, 0, 40, 0, 0x4d, post);
    assert(!around.empty() && around.front() == std::pair(9, 0) && around.back().first == 39);
    const auto distant = critter_path(0, 0, 95, 0, 0x4d, post);               // 77 steps on from the cut, then it stops
    assert(distant.front() == std::pair(9, 0) && distant.back().first == 9 + 77);
    assert(critter_path(0, 0, 95, 0, 100, post).back().first > 9 + 77);
    for (int type = 2; type <= 5; ++type) assert(!critter_neighbour(false, type, kZombie));   // objects, missiles, items, tiles
    // A dead critter counts nothing.
    assert(!critter_neighbour(true, kCritterPlayer, 0));
    assert(!critter_neighbour(true, kCritterMonster, kZombie));
    // The client's room lists (FUN_0046c570): filed at the head as a unit shows or moves rooms,
    // walked over the near list in its order, each room newest first (room_order).
    ClientRooms lists;
    const auto first = lists.file(1, 10, 3).stamp, second = lists.file(1, 11, 3).stamp;
    assert(second > first && lists.file(1, 10, 3).stamp == first);             // the same room: no new stamp
    const auto moved = lists.file(1, 10, 5).stamp;
    assert(moved > second);                                                    // a room change files it anew
    const auto other = lists.file(2, 10, 3).stamp;                             // another kind, its own entry
    assert(other > moved);
    const std::vector<RoomUnit> units{ { 3, true, second }, { 5, true, moved }, { 3, true, other } };
    const std::vector<int> near_list{ 5, 3 };
    assert((room_order(units, near_list) == std::vector<int>{ 1, 2, 0 }));
    // A unit gone from view and back is new: A leaves, B comes into room 3, A returns to room 3 at the head.
    ClientRooms view;
    view.file(1, 1, 3);
    view.drop_unseen();
    view.drop_unseen();                                                        // A not filed this frame: forgotten
    const auto unit_b = view.file(1, 2, 3).stamp;
    const auto unit_a = view.file(1, 1, 3).stamp;
    view.drop_unseen();
    assert(unit_a > unit_b && view.file(1, 2, 3).stamp == unit_b);             // B, filed each frame, keeps its stamp
    return 0;
}
