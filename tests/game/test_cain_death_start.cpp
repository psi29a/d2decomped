// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Tristram Cain's first think with his spot noted already (FUN_005e77a0) puts him in mode 0xc
// (FUN_005ddfc0): DD's start (0x5a7390) runs the death steps FUN_005a6520 with the request's
// direction byte, which FUN_005a7e60 leaves 0, so his path facing becomes 0 (FUN_006488a0) and the
// idle's NU keeps it. tools/emu/cain_death_start.py runs that chain in game.exe and --dump writes
// the facing (tests/cain_death_start_cases.inc), replayed here against World::cain_step.

#include <ai.hpp>
#include <ai/units.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>
#include <level.hpp>
#include <npc.hpp>
#include <quests.hpp>
#include <test_data.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdio>
#include <exception>
#include <iterator>

using namespace d2d::game;

namespace {
// game.exe's path facing (64 steps) after Cain's DD then NU (FUN_005a6520 through 0x5a7390).
constexpr int kFacing[] = {
#include "cain_death_start_cases.inc"
};
constexpr int kStartFacing = 42;   // the oracle's start: any facing but the result
}   // namespace

// Drives World::cain_step's noted-spot first think and checks FUN_005a6520's facing.
int main() try {
    static_assert(std::size(kFacing) == 1);
    d2d::test::TestData test;
    World world(&test.data, -1, -1);
    Level tristram;
    tristram.id = d2d::rules::CainQuest::kTristram;
    tristram.ds1 = d2d::ds1::Map(16, 16, 1, 1);
    tristram.walk.assign(80 * 80, 0);
    tristram.npcs.push_back(Npc{});
    world.level = &tristram;
    world.npc_states.assign(1, UnitState{});
    auto& cain = world.npc_states[0];
    cain.x = (20 + 0.5f) / 5;
    cain.y = (30 + 0.5f) / 5;
    face(cain, missile_direction(kStartFacing, 16));
    assert(cain.facing.now != kFacing[0]);
    world.cain.note_spot(11, 12);
    world.cain_walk = { .npc = 0 };
    world.cain_step(1000, 0.04f);

    assert(world.cain_walk.lifeless);
    if (cain.facing.now != kFacing[0] || cain.facing.goal != kFacing[0] || cain.dir != missile_direction(kFacing[0], 16)) {
        std::printf("FAIL: d2d facing %d goal %d dir %d, game.exe facing %d\n", cain.facing.now, cain.facing.goal, cain.dir, kFacing[0]);
        return 1;
    }
    std::printf("OK: Cain's death start (FUN_005a6520) leaves facing %d, as game.exe\n", kFacing[0]);

    const auto at_x = cain.x, at_y = cain.y;
    world.npc_states.assign(1, UnitState{});
    face(world.npc_states[0], missile_direction(kStartFacing, 16));
    world.npc_states[0].x = at_x;
    world.npc_states[0].y = at_y;
    world.cain = {};
    world.cain_walk = { .npc = 0 };
    world.cain_step(1000, 0.04f);
    if (world.cain_walk.lifeless || world.npc_states[0].facing.now == kFacing[0]) {
        std::printf("FAIL: a fresh first think (no mode 0xc) changed his facing\n");
        return 1;
    }
    std::printf("OK: a fresh first think keeps his facing\n");
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
