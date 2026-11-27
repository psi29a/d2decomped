// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Cain's two life-cut paths (FUN_005e77a0 and FUN_005e7880) run the monster death start
// FUN_005a6520. FUN_00649f70(unit, 1) replaces the ordinary footprint with a 3x3 0x8000
// corpse-grid stamp; the first-think path's immediate NU restores the ordinary footprint
// separately. tools/emu/cain_death_effects.py writes the dimensions replayed here.

#include <ai/units.hpp>
#include <ds1.hpp>
#include <gamedata.hpp>
#include <level.hpp>
#include <npc.hpp>
#include <quests.hpp>
#include <test_data.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>

using namespace d2d::game;

namespace {
struct DeathCase {
    int width;
    int height;
    std::uint16_t bit;
};

constexpr DeathCase kCase{
#include "cain_death_effects_case.inc"
};
}   // namespace

// Drives both Cain life cuts and checks FUN_00649f70's persistent corpse-grid effect.
int main() try {
    d2d::test::TestData test;
    World world(&test.data, -1, -1);
    Level tristram;
    tristram.id = d2d::rules::CainQuest::kTristram;
    tristram.ds1 = d2d::ds1::Map(16, 16, 1, 1);
    const int walk_width = tristram.ds1.width() * 5;
    tristram.walk.assign(std::size_t(walk_width * tristram.ds1.height() * 5), 0);
    tristram.npcs.push_back(Npc{});
    world.level = &tristram;
    world.npc_states.assign(1, UnitState{});
    auto& cain = world.npc_states[0];
    constexpr int kCainX = 20;
    constexpr int kCainY = 30;
    cain.x = Fine::centre(kCainX);
    cain.y = Fine::centre(kCainY);

    const auto corpse_stamp_is = [&](bool set) {
        for (int row = -2; row <= 2; ++row)
            for (int column = -2; column <= 2; ++column) {
                const auto cell = tristram.walk[std::size_t(kCainY + row) * std::size_t(walk_width) + std::size_t(kCainX + column)];
                const bool in_stamp = column >= -(kCase.width / 2) && column <= kCase.width / 2
                                   && row >= -(kCase.height / 2) && row <= kCase.height / 2;
                if (bool(cell & kCase.bit) != (set && in_stamp)) return false;
            }
        return true;
    };

    // FUN_005e77a0's exceptional first think: the quest already owns a spot,
    // so life is cut, DD runs, and FUN_005de080 immediately restores NU.
    world.cain.note_spot(11, 12);
    world.cain_walk = { .npc = 0 };
    assert(corpse_stamp_is(false));
    world.cain_step(1000, 0.04f);
    assert(world.cain_walk.lifeless && corpse_stamp_is(true));
    assert(!cain.hidden);

    // FUN_005e7880's ordinary portal-entry path also cuts life and runs DD,
    // but returns without the NU idle. World hides the departed unit.
    std::ranges::fill(tristram.walk, 0);
    world.cain = {};
    world.cain_walk = { .npc = 0, .stage = 7, .spot_x = kCainX, .spot_y = kCainY };
    cain.hidden = false;
    world.cain_step(2000, 0.04f);
    assert(world.cain_walk.lifeless && corpse_stamp_is(true));
    assert(world.cain_walk.npc == -1 && cain.hidden);

    std::printf("OK: both Cain death paths leave FUN_00649f70's %dx%d corpse-grid stamp and cut life\n", kCase.width, kCase.height);
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
