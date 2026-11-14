// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A walker faces the next point by game.exe's aim (FUN_0064fe40 ->
// FUN_006485f0: rules::aim_direction) and turns to it a step a frame
// (FUN_00648640), drawn in 16 directions as the client rounds its 64
// (missile_direction); a walk's end leaves it on its goal.

#include <ai.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>
#include <missiles.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::game;

int main() try {
    Level level{};
    level.ds1 = d2d::ds1::Map(40, 40, 1, 1);
    level.walk.assign(std::size_t(200 * 200), 0);
    UnitState unit;
    unit.x = Fine::centre(100); unit.y = Fine::centre(100);
    unit.dir = missile_direction(32, 16);                        // facing screen north
    unit.path = { { float(Fine::centre(110)), float(Fine::centre(100)) } };
    const int goal = d2d::rules::aim_direction(unit.x.raw, unit.y.raw, Fine::centre(110).raw, Fine::centre(100).raw);
    const int step = d2d::rules::kDiffDirections[std::size_t((goal - 32) & 0x3f)];
    assert(goal == 56 && step == 8);                              // screen south-east (world +x), 24 round: 8 a frame
    assert(follow_path(level, unit, 0x400));
    assert(unit.facing.goal == goal && unit.facing.now == 32 + step);
    assert(unit.dir == missile_direction(unit.facing.now, 16));
    int frames = 0;
    while (follow_path(level, unit, 0x400)) { ++frames; assert(frames < 60); }
    assert(unit.facing.now == goal && unit.dir == missile_direction(goal, 16));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_walk_facing: %s\n", error.what());
    return 1;
}
