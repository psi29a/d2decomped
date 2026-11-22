// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: rules
// A chicken's or bunny's nearest unit, FUN_0046c600 (tools/emu/critter_neighbour.py);
// a bird's flight end (FUN_00648e70: 77 steps on the longer axis).
#include <critters.hpp>
#include <monster_ids.hpp>

#include <cassert>
#include <cmath>

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
    return 0;
}
