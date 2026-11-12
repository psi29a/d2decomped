// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A walk at a unit (the merc's or a pet's) checks it first each frame
// (FUN_006503f0, rules::chase_check): within 0 of it, the walk is over; more
// than 5 subtiles off the path's goal on either axis, a new path to where it
// is now, the points reached coming off the budget; none left, over.

#include <ai.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <exception>

using namespace d2d::game;

namespace {

int subtile(float value) { return int(std::floor(value * 5)); }

float centre(int sub) { return (float(sub) + 0.5f) / 5; }

Level open_level() {
    Level level;
    level.ds1 = d2d::ds1::Map(16, 16, 1, 1);
    level.walk.assign(std::size_t(80 * 80), 0);
    return level;
}

}  // namespace

int main() try {
    const Level level = open_level();
    UnitState foe;
    foe.x = centre(40); foe.y = centre(20);
    UnitState unit;
    unit.x = centre(20); unit.y = centre(20);
    assert(set_off(level, unit, foe.x, foe.y, true) && unit.budget == 0x14);

    // Five subtiles off its goal: the path stands.
    foe.y = centre(25);
    assert(walk_on(level, unit, 4096, {}, &foe) && unit.goal_y == centre(20));

    // Six: a new path to it, the point reached off the budget.
    unit.path = { { centre(23), centre(20) }, { centre(30), centre(20) }, { centre(40), centre(20) } };
    unit.path_points = 3;
    while (unit.path.size() == 3) assert(walk_on(level, unit, 4096, {}, &foe));
    foe.y = centre(26);
    const int reached = unit.path_points - int(unit.path.size());
    assert(walk_on(level, unit, 4096, {}, &foe));
    assert(unit.goal_x == foe.x && unit.goal_y == foe.y && unit.budget == 0x14 - reached);
    assert(std::abs(subtile(unit.path.back().first) - 40) <= 1 && std::abs(subtile(unit.path.back().second) - 26) <= 1);

    // With the budget spent, a moved foe ends the walk.
    unit.budget = 0;
    foe.y = centre(33);
    assert(!walk_on(level, unit, 4096, {}, &foe) && unit.path.empty());

    // Beside it (unit_distance 0): over, on its subtile's centre.
    unit.x = centre(20) + 0.05f; unit.y = centre(20);
    foe.x = centre(22); foe.y = centre(20);
    assert(set_off(level, unit, centre(30), centre(20), true));
    assert(!walk_on(level, unit, 4096, {}, &foe) && unit.path.empty() && unit.x == centre(20));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_walk_at_unit: %s\n", error.what());
    return 1;
}
