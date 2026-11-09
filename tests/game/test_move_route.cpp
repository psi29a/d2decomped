// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// test-includes: components/game/fight
// A think's type 7 move (FUN_00649970) keeps the player's path it found:
// round a wall the toward pather doesn't clear in 5 steps. Needs no data.

#include <ds1.hpp>
#include <internal.hpp>

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <exception>

using namespace d2d::game;

int main() try {
    Level here;
    here.ds1 = d2d::ds1::Map(16, 16, 1, 1);                       // 80 x 80 subtiles
    here.walk.assign(80 * 80, 0);
    for (int at_x = 36; at_x <= 44; ++at_x) here.walk[56 * 80 + std::size_t(at_x)] = 0x0001;   // 4 north of him
    const Crowd crowd;
    UnitState unit;
    unit.x = 8.1f; unit.y = 12.1f;                                // subtile (40, 60)
    const d2d::rules::MercMove move{ .x = 40, .y = 52, .type = 7 };
    const auto route = move_route(here, unit, 40, 60, move, crowd);
    assert(!route.empty());
    assert(std::floor(route.back().first * 5) == 40.f && std::floor(route.back().second * 5) == 52.f);
    std::puts("OK: a type 7 move keeps its player's path");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_move_route: %s\n", error.what());
    return 1;
}
