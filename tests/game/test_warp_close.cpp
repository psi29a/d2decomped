// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A warp is taken (FUN_00548b00 case 5) when FUN_00641530's distance from
// the player (size 2) to its tile unit (size 0) is under 5 subtiles, the
// unit's spot, not the warp's cell (World::warp_close).

#include <gamedata.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdio>

using namespace d2d::game;

int main() {
    Level::Warp warp{};
    warp.x = 20; warp.y = 20;                        // its cell, far from its unit
    warp.unit_x = 10.1f; warp.unit_y = 10.1f;        // subtile (50, 50)
    const auto cell = [](int subtile) { return (float(subtile) + 0.5f) / 5; };
    assert(World::warp_close(warp, cell(50), cell(50)));
    assert(World::warp_close(warp, cell(54), cell(50)));     // 4 across: the table's 2, + 1 for a size-0 unit
    assert(!World::warp_close(warp, cell(55), cell(50)));    // 5 across: 5
    assert(World::warp_close(warp, cell(47), cell(53)));     // 3 and 3: 3
    assert(!World::warp_close(warp, cell(54), cell(54)));    // 4 and 4: 6
    assert(!World::warp_close(warp, 20.5f, 20.5f));      // on the warp's cell, the unit far off
    std::printf("OK: warp_close\n");
    return 0;
}
