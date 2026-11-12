// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A monster's walk ends at its path's last point, short of its goal or not:
// FUN_006507b0 zeroes the path's count, and FUN_00650840 runs the re-path
// check (FUN_006503f0) only while points are left (tools/emu/walk_step.py
// asserts the count). The budget isn't spent there.

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

}  // namespace

int main() try {
    Level level;
    level.ds1 = d2d::ds1::Map(16, 16, 1, 1);
    level.walk.assign(std::size_t(80 * 80), 0);
    UnitState unit;
    unit.x = centre(20); unit.y = centre(20);
    unit.goal_x = centre(40); unit.goal_y = centre(20);       // far past the path's end
    unit.budget = 0x14;
    unit.path = { { centre(23), centre(20) } };
    unit.path_points = 1;
    int frames = 0;
    while (walk_on(level, unit, 4096)) assert(++frames < 10);
    assert(subtile(unit.x) == 23 && subtile(unit.y) == 20 && unit.x == centre(23));
    assert(unit.path.empty() && unit.budget == 0x14 && unit.goal_x == centre(40));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_walk_path_end: %s\n", error.what());
    return 1;
}
