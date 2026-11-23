// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A charge's run (FUN_005cf6b0) caps its path at 0x14 steps (FUN_00648e70:
// path +0x91), a walk's start at 5 (FUN_005a63f0 -> FUN_005a6290). Past a
// wall the toward pather (FUN_00679c80) takes up to that many 8-way steps,
// so the charge's path reaches four times as far.

#include <ai.hpp>
#include <ai/units.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <exception>

using namespace d2d::game;

namespace {

float centre(int sub) { return (float(sub) + 0.5f) / 5; }

}  // namespace

int main() try {
    Level level;
    level.ds1 = d2d::ds1::Map(16, 16, 1, 1);                       // 80 x 80 subtiles
    level.walk.assign(std::size_t(80 * 80), 0);
    level.walk[std::size_t(40 * 80 + 21)] = 0x0001;                // one subtile east of it: the line stops at once
    const Crowd crowd;
    const auto path_end = [&](int path_steps) {
        Monster monster;
        monster.unit.x = centre(20); monster.unit.y = centre(40);
        monster.unit.shape = UnitShape{ 0, 1, 0x3c01 };              // its own subtile only
        monster.path_steps = path_steps;
        const bool found = path_to(level, monster, 60, 40, false, crowd);
        assert(found && !monster.steps.empty());
        return monster.steps.back().first;
    };
    const int walk = path_end(5), charge = path_end(0x14);
    assert(walk > 20 && walk <= 25);                               // one subtile east a step at most
    assert(charge >= 30 && charge <= 40);
    std::printf("OK: a walk's path ends at x %d, a charge's at x %d\n", walk, charge);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_charge_steps: %s\n", error.what());
    return 1;
}
