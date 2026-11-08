// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// FUN_00650660 moves a unit one step a frame: a point within the step (FUN_00650090) is landed
// on and the frame ends there, the rest of the step unused; a point given twice costs a frame.

#include <ai.hpp>
#include <gamedata.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::game;

int main() try {
    const Level walled{};                             // no grid: any step that doesn't arrive is blocked
    UnitState unit;
    unit.x = 21.0f; unit.y = 13.4f;
    unit.path = { { 21.05f, 13.4f }, { 21.05f, 13.4f }, { 21.1f, 13.4f } };
    assert(follow_path(walled, unit, 0.2f));          // lands on the first, the 0.15 left unused
    assert(unit.x == 21.05f && unit.path.size() == 2);
    assert(follow_path(walled, unit, 0.2f));          // the repeated point: a frame standing
    assert(unit.x == 21.05f && unit.path.size() == 1);
    assert(!follow_path(walled, unit, 0.2f));         // the last
    assert(unit.x == 21.1f && unit.path.empty());
    std::puts("OK: one path point a frame (FUN_00650660)");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_walk_point_frame: %s\n", error.what());
    return 1;
}
