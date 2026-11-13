// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A walk over the level's west or north edge into the level beside it (the
// way back into camp from the Blood Moor): d2d's places are the level's
// own, so it passes below 0. game.exe's subtiles are shorts (FUN_0064ff90's
// packed x / y), so subtile -1 is tested as -1, there, not as 65535.

#include <ai.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>

#include <cassert>
#include <cstdio>
#include <exception>

using namespace d2d::game;

namespace {

Level open_level() {
    Level level;
    level.ds1 = d2d::ds1::Map(8, 8, 1, 1);
    level.walk.assign(std::size_t(40 * 40), 0);
    return level;
}

}  // namespace

int main() try {
    Level here = open_level();
    const Level west = open_level();
    here.nearby.push_back({ &west, -8, 0 });                    // its (0, 0) 8 cells west of ours
    UnitState unit;
    unit.x = Fine::centre(2); unit.y = Fine::centre(20);
    unit.path = { { float(Fine::centre(-5)), float(Fine::centre(20)) } };
    int frames = 0;
    while (follow_path(here, unit, 0x1000)) assert(++frames < 20);
    assert(unit.x.subtile() == -5 && unit.y.subtile() == 20 && !here.inside(unit.x, unit.y));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_walk_off_level: %s\n", error.what());
    return 1;
}
