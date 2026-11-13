// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A player's path at a unit (type 7, flag 0x800) lifts the target's stamp
// with its own while it's made (FUN_00649970 -> FUN_00649560): the line
// runs onto the target's subtile. With the stamp in place it stops short.

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
    UnitState self;
    self.x = centre(20); self.y = centre(20);
    UnitState foe;
    foe.x = centre(30); foe.y = centre(20);
    foe.shape = UnitShape::monster(2, false, false, false);
    const Crowd crowd{ { &self, &foe } };

    const auto lifted = player_walk(level, self.x, self.y, foe.x, foe.y, true, crowd, &self, &foe);
    assert(!lifted.empty() && subtile(lifted.back().first) == 30 && subtile(lifted.back().second) == 20);

    const auto stamped = player_walk(level, self.x, self.y, foe.x, foe.y, true, crowd, &self);
    assert(!stamped.empty() && subtile(stamped.back().first) < 30);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_walk_lift_target: %s\n", error.what());
    return 1;
}
