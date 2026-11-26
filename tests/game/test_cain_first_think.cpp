// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Tristram Cain's first think (FUN_005e77a0) through World::cain_step, made-up data. With the spot not
// yet noted (FUN_005944b0 returns 1) it notes his subtile and heads 3 on, life kept. With it noted
// already it takes FUN_005944f0 (camp Cain due), then his life goes (FUN_00627260(unit, 6, 0, 0)) and
// mode 0xc (FUN_005ddfc0), undone to NU by the same think's idle (FUN_005de080): the walk goes on from
// the noted spot. tools/emu/quest_cain_first_think.py checks game.exe's side; its --dump rows
// (tests/cain_first_think_cases.inc) are replayed here against cain_step.

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
// One game.exe first think (FUN_005e77a0) at subtile (20, 30): spot noted already, life cut, goal offset.
struct ThinkCase {
    bool noted;
    bool lifeless;
    int goal_dx;
    int goal_dy;
};

constexpr ThinkCase kCases[] = {
#include "cain_first_think_cases.inc"
};
}   // namespace

// Drives World::cain_step's noted-spot first think (FUN_005e77a0): stat 6 is cut (FUN_00627260) only there.
int main() try {
    d2d::test::TestData test;
    World world(&test.data, -1, -1);
    Level tristram;
    tristram.id = d2d::rules::CainQuest::kTristram;
    tristram.ds1 = d2d::ds1::Map(16, 16, 1, 1);
    tristram.walk.assign(80 * 80, 0);
    tristram.npcs.push_back(Npc{});
    world.level = &tristram;
    const auto centre = [](int subtile) { return (float(subtile) + 0.5f) / 5; };
    const auto first_think = [&](bool noted) {
        world.npc_states.assign(1, UnitState{});
        world.npc_states[0].x = centre(20);
        world.npc_states[0].y = centre(30);
        world.cain = {};
        if (noted) world.cain.note_spot(11, 12);
        world.cain_walk = { .npc = 0 };
        world.cain_step(1000, 0.04f);
        return world.cain_walk;
    };

    const auto fresh = first_think(false);
    assert(fresh.stage == 1 && fresh.x == 23 && fresh.y == 33 && fresh.spot_x == 20 && fresh.spot_y == 30);
    assert(!fresh.lifeless && !world.cain.camp_due && !world.npc_states[0].walking);
    std::printf("OK: a first think notes the spot and keeps his life\n");

    const auto again = first_think(true);
    assert(again.stage == 1 && again.x == 23 && again.y == 33 && again.spot_x == 11 && again.spot_y == 12);
    assert(again.lifeless && world.cain.camp_due && !world.npc_states[0].walking && !world.npc_states[0].hidden);
    std::printf("OK: with the spot noted, camp Cain due and his life gone (stat 6), standing again (NU)\n");

    for (const auto& row : kCases) {
        const auto walk = first_think(row.noted);
        if (walk.lifeless != row.lifeless || walk.x != 20 + row.goal_dx || walk.y != 30 + row.goal_dy) {
            std::printf("FAIL: noted=%d: d2d lifeless=%d goal (%d, %d), game.exe lifeless=%d goal (%d, %d)\n", row.noted, walk.lifeless,
                        walk.x, walk.y, row.lifeless, 20 + row.goal_dx, 30 + row.goal_dy);
            return 1;
        }
    }
    std::printf("OK: %zu game.exe first-think rows (tools/emu/quest_cain_first_think.py --dump)\n", std::size(kCases));
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
