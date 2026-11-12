// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A step onto a blocked subtile ends the walk on the centre of the subtile the
// unit is on (FUN_00650150, FUN_006507b0) and says so; one inside its own
// subtile tests nothing (only a new subtile is tested, LAB_006501c0).
// test_walk_stop.cpp has the stop and the centre through the World's tick.

#include <ai.hpp>
#include <gamedata.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <exception>

using namespace d2d::game;

namespace {

bool near_value(float value, float expected) { return std::abs(value - expected) < 0.001f; }

}  // namespace

int main() try {
    const Level walled{};                             // no grid: every subtile is blocked
    UnitState unit;
    unit.x = 21.0f; unit.y = 13.4f;                   // on a corner of subtile (105, 67)
    unit.path = { { 20.9f, 13.7f } };
    bool blocked = false;
    assert(!follow_path(walled, unit, 2048, {}, &blocked));
    assert(blocked && unit.path.empty());
    assert(near_value(unit.x, 21.1f) && near_value(unit.y, 13.5f));

    // On the centre already it stays there.
    const float centre_x = unit.x, centre_y = unit.y;
    unit.path = { { 20.9f, 13.7f } };
    blocked = false;
    assert(!follow_path(walled, unit, 4096, {}, &blocked));   // a subtile a frame: off its own
    assert(blocked && unit.x == centre_x && unit.y == centre_y);

    // A leg that stays on its subtile tests nothing, and isn't a block.
    unit.x = 21.02f; unit.y = 13.42f;
    unit.path = { { 21.1f, 13.5f } };
    blocked = false;
    assert(!follow_path(walled, unit, 4096, {}, &blocked));
    assert(!blocked && near_value(unit.x, 21.1f) && near_value(unit.y, 13.5f));

    // No route (LAB_006508ff): the same centre, and it counts as a block.
    unit.x = 21.0f; unit.y = 13.4f;
    unit.path.clear();
    blocked = false;
    assert(!follow_path(walled, unit, 4096, {}, &blocked));
    assert(blocked && unit.path.empty());
    assert(near_value(unit.x, 21.1f) && near_value(unit.y, 13.5f));
    const float stayed_x = unit.x, stayed_y = unit.y;
    blocked = false;
    assert(!follow_path(walled, unit, 4096, {}, &blocked));
    assert(blocked && unit.x == stayed_x && unit.y == stayed_y);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_walk_blocked_centre: %s\n", error.what());
    return 1;
}
