// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// An opendoors walker (mask 0x3401) paths through a door's 0x800, but its
// steps test 0x3c01 (FUN_0064ff90), so the door stops it; the step or's the
// door's bit into path +0x54 (UnitState::path_word), which FUN_00648eb0
// reads at the next think, and a new path zeroes it (FUN_00649970).

#include <ai.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <exception>

using namespace d2d::game;

namespace {

int subtile(float value) { return int(std::floor(value * 5)); }

float centre(int sub) { return (float(sub) + 0.5f) / 5; }

}  // namespace

int main() try {
    Level level;
    level.ds1 = d2d::ds1::Map(16, 16, 1, 1);                       // 80 x 80 subtiles
    level.walk.assign(std::size_t(80 * 80), 0);
    level.walk[std::size_t(20 * 80 + 25)] = 0x0800;                // a closed door's bit, 5 east
    UnitState unit;
    unit.x = centre(20); unit.y = centre(20);
    unit.shape = UnitShape{ 0, 1, 0x3401 };                         // opendoors, its own subtile only
    const bool set = set_off(level, unit, centre(30), centre(20), false);   // the pather's mask lets the door by
    assert(set && unit.path_word == 0);
    int frames = 0;
    while (walk_on(level, unit, 4096)) { ++frames; assert(frames < 20); }
    assert(subtile(unit.x) == 24 && subtile(unit.y) == 20);       // stopped short of the door
    assert((unit.path_word & 0x0800) != 0);
    const bool again = set_off(level, unit, centre(24), centre(23), false);
    assert(again && unit.path_word == 0);                          // a new path: +0x54 zeroed
    std::printf("OK: the door stops the walk at x %d and its bit stays in +0x54\n", subtile(unit.x));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_walk_door_word: %s\n", error.what());
    return 1;
}
