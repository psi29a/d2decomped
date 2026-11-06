// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A blocked step ends the walk on the centre of the subtile the unit is on
// (FUN_00650150, FUN_006507b0) and says so; a step that arrives doesn't.
// test_walk_stop.cpp has the stop and the centre through the World's tick.

#include <ai.hpp>
#include <gamedata.hpp>

#include <cassert>
#include <cmath>

using namespace d2d::game;

namespace {

bool near_value(float value, float expected) { return std::abs(value - expected) < 0.001f; }

}  // namespace

int main() {
    const Level walled{};                             // no grid: every subtile is blocked
    UnitState unit;
    unit.x = 21.0f; unit.y = 13.4f;                   // on a corner of subtile (105, 67)
    unit.path = { { 20.9f, 13.7f } };
    bool blocked = false;
    assert(!follow_path(walled, unit, 0.1f, {}, &blocked));
    assert(blocked && unit.path.empty());
    assert(near_value(unit.x, 21.1f) && near_value(unit.y, 13.5f));

    // On the centre already it stays there.
    const float centre_x = unit.x, centre_y = unit.y;
    unit.path = { { 20.9f, 13.7f } };
    blocked = false;
    assert(!follow_path(walled, unit, 0.1f, {}, &blocked));
    assert(blocked && unit.x == centre_x && unit.y == centre_y);

    // A leg that arrives tests nothing, and isn't a block.
    unit.x = 21.0f; unit.y = 13.4f;
    unit.path = { { 21.05f, 13.4f } };
    blocked = false;
    assert(!follow_path(walled, unit, 0.1f, {}, &blocked));
    assert(!blocked && near_value(unit.x, 21.05f));
    return 0;
}
