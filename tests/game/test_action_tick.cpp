// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// When a monster's strike or shot comes (ai.hpp action_due_ms): the tick
// game.exe's FUN_005539b0 files the mode's event frame on, at the rate
// FUN_00623f50 stores (mode_rate), not a frame length rounded in ms.
// `--council` prints the Council's S1 (classes 345..347, MissS1) over a
// grid of walk % and anim % as `D class walk anim rate tick`, for
// tools/emu/council_s1.py's `compare`, which runs FUN_005539b0 on each rate.
// Without arguments: the tick against the old ms deadline (no game files),
// then the Council's numbers with the game data.

#include <ai.hpp>
#include <ai/units.hpp>
#include <combat.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string_view>

namespace fs = std::filesystem;
using namespace d2d::game;

namespace {

constexpr std::array<int, 3> kCouncil{ 345, 346, 347 };
constexpr std::array<int, 7> kWalkPct{ 25, 50, 66, 75, 100, 150, 175 };
constexpr std::array<int, 5> kAnimPct{ -85, -50, 0, 50, 75 };

// FUN_005539b0's tick against the old ms deadline, floor(10240 / speed) x
// frame x 100 / (100 + pct), on the Council's HPS1HTH shape (event on frame
// 3, speed 200) at each rate a speed modifier gives.
void rate_check() {
    assert(d2d::rules::frame_tick(3, 0, 200) == 4);           // 800 >= 768 on the fourth tick
    int differ = 0;
    for (int pct = -85; pct <= 75; ++pct) {
        const int rate = 200 * (100 + pct) / 100;
        const int old_ms = 10240 / 200 * 3 * 100 / (100 + pct);
        if ((old_ms + 39) / 40 != d2d::rules::frame_tick(3, 0, rate)) ++differ;
    }
    // +27 %: rate 254 is 762 / 256 frames after 3 ticks, short of 768, so tick 4; the old 120 ms came on tick 3.
    assert(d2d::rules::frame_tick(3, 0, 254) == 4 && 10240 / 200 * 3 * 100 / 127 == 120);
    assert(differ == 1);
    std::printf("OK: FUN_005539b0's tick; the old ms deadline missed it at %d of 161 rates\n", differ);
}

int council_cases(const GameData& data, bool print) {
    for (const int cls : kCouncil) {
        Monster unit;
        unit.type = cls;
        unit.npc = data.mon_npc[std::size_t(cls)];
        const auto& timing = data.npc_timing(unit.npc, "S1");
        if (timing.action != 3) { std::printf("FAIL: class %d's S1 has its event at %d, not 3\n", cls, timing.action); return 1; }
        for (const int walk : kWalkPct)
            for (const int anim : kAnimPct) {
                unit.walk_pct = walk;
                unit.anim_pct = anim;
                const int rate = mode_rate(data, unit, "S1");
                const auto due = action_due_ms(data, unit, "S1");
                assert(due % 40 == 0 && due == std::uint32_t(d2d::rules::frame_tick(3, 0, std::max(rate, 1))) * 40);
                if (print) std::printf("D %d %d %d %d %u\n", cls, walk, anim, rate, due / 40);
            }
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    const bool council = argc > 1 && std::string_view(argv[1]) == "--council";
    if (!council) rate_check();
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) {
        if (council) { std::fputs("--council needs D2_MPQ_DIR\n", stderr); return 1; }
        std::puts("SKIP (the Council check): no d2data.mpq (D2_MPQ_DIR)");
        return 0;
    }
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data) { std::puts("FAIL: load_game_data failed"); return 1; }
    if (!data->patched) {
        if (council) { std::fputs("--council needs the 1.14d data (D2_PATCH_INSTALLER)\n", stderr); return 1; }
        std::puts("SKIP (the Council check): needs the 1.14d data (D2_PATCH_INSTALLER)");
        return 0;
    }
    if (council_cases(*data, council) != 0) return 1;
    if (!council) std::puts("OK: the Council's S1 on FUN_005539b0's tick over walk % x anim %");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_action_tick: %s\n", error.what());
    return 1;
}
