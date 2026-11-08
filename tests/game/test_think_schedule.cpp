// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A monster's think event (unit event 2). One that comes due mid-mode runs
// (FUN_005b1740 reads no mode) and its act replaces the mode (FUN_005a7c20
// stops none); a mode's end goes to NU, whose start FUN_005a73e0 files the
// think aidel on (0: 15) unless one is pending later. A Zombie in melee
// swings at every think. Needs the data; SKIPs without it.

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
    const std::uint32_t aidel_ms = std::uint32_t(aidel > 0 ? aidel : 15) * kFrameMs;
    Level here{};
    here.id = d2d::rules::level_ids::kBloodMoor;
    const Crowd crowd;
    std::vector<Missile> shots;
    std::array<Foe, 1> foes{};
    foes[0].x = 10.4f; foes[0].y = 10.f; foes[0].alive = true;
    d2d::rules::Rng rng(7);
    constexpr std::uint32_t kStart = 1000 * kFrameMs;
    const auto swinging = [&](std::uint32_t pending_frames) {
        d2d::rules::Rng spawn{ 0x5eed };
        std::vector<Monster> pack{ make_monster(*data, int(zombie->second), 10.f, 10.f, spawn, 0) };
        auto& monster = pack[0];
        monster.aware = true;
        set_mode(*data, monster, "A1", kStart);
        monster.mode_until = kStart + 60 * kFrameMs;                   // a long swing
        monster.thought_ms = kStart;
        monster.next_act = kStart + pending_frames * kFrameMs;
        return pack;
    };
    const auto tick = [&](std::vector<Monster>& pack, std::uint32_t now) {
        monster_update(*data, here, pack[0], foes, rng, now, 0.04f, crowd, shots, pack);
    };

    // A think due 5 frames into a 60-frame swing runs then: a new swing starts.
    auto pack = swinging(5);
    for (std::uint32_t frame = 1; frame < 5; ++frame) tick(pack, kStart + frame * kFrameMs);
    assert(pack[0].unit.mode_ms == kStart);
    tick(pack, kStart + 5 * kFrameMs);
    assert((pack[0].mode == "A1" || pack[0].mode == "A2") && pack[0].unit.mode_ms == kStart + 5 * kFrameMs);

    // None pending: the swing plays out, and its end files the think aidel on.
    pack = swinging(0);
    pack[0].next_act = 0;
    const std::uint32_t end = pack[0].mode_until;
    for (std::uint32_t now = kStart + kFrameMs; now <= end; now += kFrameMs) tick(pack, now);
    assert(pack[0].mode == "NU" && pack[0].next_act == end + aidel_ms);

    // One pending past the end stays.
    pack = swinging(200);
    for (std::uint32_t now = kStart + kFrameMs; now <= end; now += kFrameMs) tick(pack, now);
    assert(pack[0].mode == "NU" && pack[0].next_act == kStart + 200 * kFrameMs);
    std::puts("OK: a think comes due mid-mode; a mode's end files aidel unless one is pending later");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_think_schedule: %s\n", error.what());
    return 1;
}
