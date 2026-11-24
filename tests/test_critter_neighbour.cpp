// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A chicken's or bunny's nearest unit, FUN_0046c600 (tools/emu/critter_neighbour.py);
// a bird's flight end (FUN_00648e70: 77 steps on the longer axis); the client's
// room lists the rats' corpse walk takes (FUN_0046c570).
#include <critters.hpp>
#include <monster_ids.hpp>
#include <monsters.hpp>

#include <cassert>
#include <cmath>
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
    // A flight 120 subtiles east, 60 south stops 77 east, 38.5 south; a diagonal 100 x 100 at 77 x 77 (not
    // 77 / sqrt 2 each); one under 77 steps goes all the way.
    const auto [east_x, east_y] = flight_end(0, 0, 24.f, 12.f);
    assert(std::abs(east_x * 5 - 77.f) < 1e-3f && std::abs(east_y * 5 - 38.5f) < 1e-3f);
    const auto [diag_x, diag_y] = flight_end(10, 10, 30.f, 30.f);
    assert(std::abs((diag_x - 10) * 5 - 77.f) < 1e-3f && std::abs((diag_y - 10) * 5 - 77.f) < 1e-3f);
    const auto [near_x, near_y] = flight_end(0, 0, 10.f, -15.f);
    assert(near_x == 10.f && near_y == -15.f);
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
