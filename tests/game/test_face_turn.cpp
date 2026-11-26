// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// An action's facing outside a walk turns a step a frame, as game.exe's client
// does: FUN_00649ef0(path, x, y, 0) -> FUN_006485f0 sets a player's or
// monster's goal (+0x65) and step (+0x66) only, and FUN_00648640 turns +0x64
// each frame. face_turn_cases.inc is game.exe's own run (tools/emu/face_turn.py),
// replayed through turn_to and turn_frame; then a town NPC's special facing
// (FUN_00648820, the same goal and step) through the real npc_patrol. No game data.

#include <ai.hpp>
#include <ai/units.hpp>
#include <gamedata.hpp>
#include <level.hpp>
#include <npc.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iterator>

using namespace d2d::game;

namespace {

struct FaceTurnFrame {
    int now, goal;
};
struct FaceTurnCase {
    int type;
    std::uint32_t x, y;
    int now, goal, step, to_x, to_y;
    FaceTurnFrame frames[10];
};

#include "face_turn_cases.inc"

}  // namespace

int main() try {
    int bad = 0;
    for (const auto& one : kFaceTurnCases) {
        UnitState unit;
        unit.x.raw = one.x; unit.y.raw = one.y;
        unit.facing = { std::uint8_t(one.now), std::uint8_t(one.goal), std::int8_t(std::uint8_t(one.step)) };
        unit.dir = missile_direction(one.now, 16);              // drawn as it faces: nothing set since
        turn_to(unit, one.to_x, one.to_y);
        int frame = 0;
        for (const auto& want : one.frames) {
            turn_frame(unit);
            if (unit.facing.now != want.now || unit.facing.goal != want.goal || unit.dir != missile_direction(want.now, 16)) {
                if (++bad < 10) std::fprintf(stderr, "type %d now %d goal %d -> (%d, %d), frame %d: game %d / %d, d2d %d / %d\n", one.type, one.now, one.goal,
                                             one.to_x, one.to_y, frame, want.now, want.goal, int(unit.facing.now), int(unit.facing.goal));
                break;
            }
            ++frame;
        }
    }
    if (bad) { std::fprintf(stderr, "%d cases differ from game.exe\n", bad); return 1; }

    // Charsi (hcIdx 0x9a) at her anvil, her special S1 due: the think faces
    // her 0x38 (town-npcs.md, FUN_005e6ae0 -> FUN_00648820). Facing 8 now, she
    // turns there a DIFFDIRECTIONS step (0x30 round: -4) a frame, not at once.
    const GameData game_data;
    Level level{};
    Npc charsi;
    charsi.town_ai = TownAi::npc;
    charsi.hc_idx = 0x9a;
    charsi.modes = 1U << 8;
    charsi.x = charsi.y = 20.1f;
    charsi.path = { { 20.1f, 20.1f } };
    level.npcs.push_back(charsi);
    auto states = npc_start(level);
    auto& state = states[0];
    const int spot_x = state.x.subtile(), spot_y = state.y.subtile();
    state.filed = true;
    state.brain.homed = true; state.brain.home_x = spot_x; state.brain.home_y = spot_y;
    state.brain.special_mode = 8; state.brain.special_x = spot_x; state.brain.special_y = spot_y;
    state.facing = { 8, 8, 0 };
    state.dir = missile_direction(8, 16);
    npc_patrol(game_data, level, states, { -1, -1, -1 }, 1000, 0.04f);
    std::printf("after the think: now %d, goal %d, dir %d\n", int(state.facing.now), int(state.facing.goal), state.dir);
    assert(state.facing.goal == 0x38 && state.facing.now == 8 && state.dir == missile_direction(8, 16));   // a goal, not a snap
    npc_patrol(game_data, level, states, { -1, -1, -1 }, 1040, 0.04f);
    std::printf("a frame on: now %d, goal %d, dir %d\n", int(state.facing.now), int(state.facing.goal), state.dir);
    assert(state.facing.now == 4 && state.facing.goal == 0x38 && state.dir == missile_direction(4, 16));     // 8 - 4: a step
    for (int frame = 0; frame < 3; ++frame) npc_patrol(game_data, level, states, { -1, -1, -1 }, 1080 + std::uint32_t(frame) * 40, 0.04f);
    assert(state.facing.now == 0x38 && state.dir == missile_direction(0x38, 16));
    std::printf("OK: %zu FUN_00649ef0 turns as game.exe's; Charsi turns to 0x38 a step a frame\n", std::size(kFaceTurnCases));
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_face_turn: %s\n", error.what());
    return 1;
}
