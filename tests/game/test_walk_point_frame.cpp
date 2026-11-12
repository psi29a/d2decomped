// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// FUN_00650660 moves a unit one step a frame: a point within the step (FUN_00650090) is landed
// on and the frame ends there, the rest of the step unused; a point it then stands on (one
// given twice) is passed at once (FUN_0064fe40). tools/emu/walk_step.py; test_walk_step.cpp.

#include <ai.hpp>
#include <gamedata.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::game;

int main() try {
    const Level walled{};                             // no grid: a new subtile is blocked
    UnitState unit;
    unit.x = 21.02f; unit.y = 13.5f;                  // subtile (105, 67), short of its centre
    unit.path = { { 21.1f, 13.5f }, { 21.1f, 13.5f }, { 21.3f, 13.5f } };   // the centre twice, then the next subtile's
    assert(follow_path(walled, unit, 4096));          // lands on the first, the rest of the step unused; its repeat is passed
    assert(unit.x == 21.1f && unit.path.size() == 1);
    assert(!follow_path(walled, unit, 4096));         // the last is on a blocked subtile: over, where it stood
    assert(unit.x == 21.1f && unit.path.empty());
    std::puts("OK: one path point a frame (FUN_00650660)");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_walk_point_frame: %s\n", error.what());
    return 1;
}
