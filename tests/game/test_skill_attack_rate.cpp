// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A monster's S1..S4 / SQ run at its attack rate while its current skill has UseAttackRate (FUN_00621580: the mode row's +8,
// Skills record byte +6 & 8), else at other_animrate like any other mode (FUN_00623f50). skill_attack_rate_cases.inc is
// game.exe's own +0x4c on 3000 cases (tools/emu/skill_attack_rate.py), run here against anim_rate_pct with the real Skills.txt.
// GH, DD and DT are left out: the oracle starts at 0x6241d7 and d2d keeps their own rules (GH: get_hit_rate).

#include <ai.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iterator>
#include <string_view>

namespace fs = std::filesystem;
using namespace d2d::game;

namespace {

struct Case { std::string_view mode; int skill, ias, pct, speed, rate; };
constexpr Case kCases[] = {
#include "skill_attack_rate_cases.inc"
};

}   // namespace

int main() try {
    Monster monster;
    monster.anim_pct = 30;
    assert(anim_rate_pct(monster, "SQ", true) == 30 && anim_rate_pct(monster, "S2", true) == 30 && anim_rate_pct(monster, "BL", true) == 30);
    monster.props.ias = 60;   // 120 x 60 / 180 = 40 more, on an attack mode only
    assert(anim_rate_pct(monster, "SQ", true) == 70 && anim_rate_pct(monster, "SQ", false) == 30 && anim_rate_pct(monster, "BL", true) == 30);

    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data) { std::printf("FAIL: load_game_data failed\n"); return 1; }
    if (!data->patched) { std::printf("SKIP: 1.14d's tables: set D2_PATCH_INSTALLER\n"); return D2D_TEST_SKIPPED; }
    int bad = 0;
    for (const auto& one : kCases) {
        Monster unit;
        unit.skill = one.skill;
        unit.props.ias = one.ias;
        unit.anim_pct = one.pct - 100;
        const int rate = std::min((100 + anim_rate_pct(*data, unit, one.mode)) * one.speed / 100, 0x7fff);
        if (rate != one.rate && ++bad <= 5)
            std::printf("FAIL: %.*s skill %d ias %d pct %d speed %d: d2d %d, game.exe %d\n", int(one.mode.size()), one.mode.data(), one.skill, one.ias, one.pct,
                        one.speed, rate, one.rate);
    }
    if (bad) { std::printf("%d of %zu differ\n", bad, std::size(kCases)); return 1; }
    std::printf("OK: %zu mode rates as FUN_00623f50 sets them\n", std::size(kCases));
    return 0;
} catch (const std::exception& error) {
    std::printf("test_skill_attack_rate: %s\n", error.what());
    return 1;
}
