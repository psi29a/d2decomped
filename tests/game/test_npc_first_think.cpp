// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A town NPC's first think comes 2 frames after its room comes into play:
// game.exe files event 2 at frame + 2 for a monster in NU as the room gets
// its first client (FUN_0053a8e0 -> FUN_00573780, 0x5737c0;
// tools/emu/npc_first_think.py). No game data: a Vendor
// (rules::vendor_think: 20 % S1, else stand 30) typed in.

#include <ai.hpp>
#include <gamedata.hpp>

#include <cassert>
#include <cstdint>
#include <vector>

using namespace d2d::game;

int main() {
    constexpr std::uint32_t kFrameMs = 40, kStart = 1000;
    const GameData game_data;
    Level level{};
    Npc vendor;
    vendor.town_ai = TownAi::vendor;
    // A seed whose first draw stands (30 frames), so no mode needs timings.
    std::uint32_t seed = 1;
    while (d2d::rules::Rng(seed).next() % 100 < 20) ++seed;
    vendor.seed = d2d::rules::Rng(seed);
    level.npcs.push_back(vendor);
    auto states = npc_start(level);
    assert(!states[0].filed && states[0].wait_until == 0);

    npc_patrol(game_data, level, states, { -1, -1, -1 }, kStart, 0.04f);
    assert(states[0].filed && states[0].wait_until == kStart + 2 * kFrameMs);   // filed, not thought
    assert(level.npcs[0].seed.low == seed);
    npc_patrol(game_data, level, states, { -1, -1, -1 }, kStart + kFrameMs, 0.04f);
    assert(level.npcs[0].seed.low == seed);                                       // a frame on: still waiting
    npc_patrol(game_data, level, states, { -1, -1, -1 }, kStart + 2 * kFrameMs, 0.04f);
    assert(level.npcs[0].seed.low != seed);                                       // 2 frames on: the think
    assert(states[0].wait_until == kStart + 2 * kFrameMs + 30 * kFrameMs);       // its stand 30
    return 0;
}
