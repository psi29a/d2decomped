// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Frozen mid-swing (FUN_0057b230 sets no mode), a monster holds its swing:
// the strike and the end coming due are dropped (FUN_005a7f80, event types 0
// and 1), so the swing never lands and never ends; the think filed at the
// thaw (+ aidel, 0x57b170) replaces it. A Zombie in melee swings at every
// think. Needs the data; SKIPs without it.

#include <ai.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <rules.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <limits>
#include <vector>

using namespace d2d::game;

constexpr std::uint32_t kFrameMs = 40;          // a game frame

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: 1.14d's MonStats: set D2_PATCH_INSTALLER\n"); return D2D_TEST_SKIPPED; }
    const auto zombie = data->monsters.by_id.find("zombie1");
    assert(zombie != data->monsters.by_id.end());
    const int aidel = data->monsters.types[zombie->second].diff[0].aidel;
    Level here{};
    here.id = d2d::rules::level_ids::kBloodMoor;
    const Crowd crowd;
    std::vector<Missile> shots;
    std::array<Foe, 1> foes{};
    foes[0].x = 10.4f; foes[0].y = 10.f; foes[0].alive = true;
    d2d::rules::Rng rng(7);
    constexpr std::uint32_t kStart = 1000 * kFrameMs;
    d2d::rules::Rng spawn{ 0x5eed };
    std::vector<Monster> pack{ make_monster(*data, int(zombie->second), 10.f, 10.f, spawn, 0) };
    auto& monster = pack[0];
    monster.aware = true;
    set_mode(*data, monster, "A1", kStart);
    const std::uint32_t end = monster.mode_until;
    assert(end > kStart);
    const std::uint32_t thaw = end + 10 * kFrameMs;              // past the swing's strike and end
    freeze(monster, thaw);
    monster.thought_ms = kStart;
    monster.next_act = thaw + kFrameMs;                          // FUN_0057b230 files the think a frame past the thaw
    const std::uint32_t think_at = thaw + std::uint32_t(aidel > 0 ? aidel : 15) * kFrameMs;
    std::uint32_t now = kStart;
    for (; now < thaw; now += kFrameMs) monster_update(*data, here, monster, foes, rng, now, 0.04f, crowd, shots, pack);
    assert(monster.mode == "A1" && monster.unit.mode_ms == kStart);   // held
    assert(monster.struck && foes[0].blows.empty());                  // its strike dropped
    assert(monster.mode_until == std::numeric_limits<std::uint32_t>::max());   // its end dropped
    for (; now < think_at; now += kFrameMs) monster_update(*data, here, monster, foes, rng, now, 0.04f, crowd, shots, pack);
    assert(monster.mode == "A1" && monster.unit.mode_ms == kStart);   // still held till the thaw's think
    monster_update(*data, here, monster, foes, rng, think_at, 0.04f, crowd, shots, pack);
    assert((monster.mode == "A1" || monster.mode == "A2") && monster.unit.mode_ms == think_at);

    // A freeze inside a longer stun (state 21): the thaw still files its think
    // (end + aidel, 0x57b170); due while stunned, it stands 3 (FUN_005b10e0).
    std::vector<Monster> stood{ make_monster(*data, int(zombie->second), 10.f, 10.f, spawn, 0) };
    auto& held = stood[0];
    held.aware = true;
    const std::uint32_t thawed = kStart + 10 * kFrameMs, unstunned = thawed + 40 * kFrameMs;
    freeze(held, thawed);
    held.stun_until = held.stunned_until = unstunned;
    held.thought_ms = kStart;
    held.next_act = thawed + kFrameMs;
    for (now = kStart; now <= thawed; now += kFrameMs) monster_update(*data, here, held, foes, rng, now, 0.04f, crowd, shots, stood);
    const std::uint32_t due = thawed + std::uint32_t(aidel > 0 ? aidel : 15) * kFrameMs;
    assert(held.frozen_until == 0 && held.next_act == due && due < unstunned);
    for (; now <= due; now += kFrameMs) monster_update(*data, here, held, foes, rng, now, 0.04f, crowd, shots, stood);
    assert(held.mode == "NU" && held.next_act == due + 3 * kFrameMs);
    std::puts("OK: a frozen swing holds, its strike and end dropped, till the thaw's think; a thaw under a stun thinks");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_freeze_hold: %s\n", error.what());
    return 1;
}
