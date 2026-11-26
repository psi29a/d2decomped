// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A damage record whose ResultFlags carry 0x4000 and not the get-hit bit 4 takes FUN_0057cee0's 0x4000 branch
// (FUN_0064c040, event 4 via FUN_005a43a0) and never reaches the get-hit roll FUN_0057cb00, stunned or not:
// Static Field's 0x4001 (0x5c97d2, FUN_005c96a0) and Thorns' 0x4021 (FUN_005bf670). A thorns hit worth half
// the striker's life, a sure get-hit by rules::get_hit, leaves it in its swing. tools/emu/soft_hit.py runs FUN_0057cee0
// and writes the flag rows (tests/soft_hit_cases.inc); the rest needs the game data.

#include <ai.hpp>
#include <ai/units.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <monsters.hpp>
#include <rules.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iterator>
#include <vector>

using namespace d2d::game;
namespace fs = std::filesystem;

namespace {
// One FUN_0057cee0 run on a monster in NU: the record's flags, stunned, whether it reached the get-hit.
struct FlagRow {
    int flags;
    bool stunned;
    bool reaches;
};
constexpr FlagRow kRows[] = {
#include "soft_hit_cases.inc"
};
}   // namespace

// FUN_0057cee0's monster branch by record flags, then a zombie striking a thorns foe (FUN_005bf670's 0x4021).
int main() try {
    for (const auto& row : kRows)
        if (record_gets_hit(row.flags) != row.reaches) {
            std::printf("FAIL: flags %#x stunned %d: d2d %d, game.exe %d\n", unsigned(row.flags), int(row.stunned), int(!row.reaches), int(row.reaches));
            return 1;
        }
    std::printf("OK: %zu game.exe FUN_0057cee0 rows (tools/emu/soft_hit.py --dump)\n", std::size(kRows));

    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data) { std::printf("FAIL: game data\n"); return 1; }
    if (!data->patched) { std::printf("SKIP: 1.14d's tables: set D2_PATCH_INSTALLER\n"); return D2D_TEST_SKIPPED; }
    const auto zombie_row = data->monsters.by_id.find("zombie1");
    if (zombie_row == data->monsters.by_id.end() || !data->monsters.types[zombie_row->second].can_get_hit) { std::printf("FAIL: zombie1 with GH\n"); return 1; }

    // hurt itself: half its life without the get-hit bit, stunned or not, stays out of GH; with it, a sure GH.
    for (const bool stunned : { false, true }) {
        d2d::rules::Rng spawn{ 3 };
        Monster zombie = make_monster(*data, int(zombie_row->second), 10.f, 10.f, spawn, 0);
        zombie.stats.hit_points = zombie.hit_points = 400;
        zombie.max_life_frac = zombie.life_frac = 0;
        set_mode(*data, zombie, "NU", 0);
        if (stunned) zombie.stun_until = 10'000;
        hurt(*data, zombie, 200, 40, 0, 0, false, 0, record_gets_hit(0x4001));
        if (zombie.mode == "GH" || zombie.hit_points != 200) { std::printf("FAIL: 0x4001 stunned=%d: mode %.*s life %d\n", stunned, int(zombie.mode.size()), zombie.mode.data(), zombie.hit_points); return 1; }
        hurt(*data, zombie, 100, 80, 0, 0, false, 0, record_gets_hit(0x0004));
        if (zombie.mode != "GH") { std::printf("FAIL: flag 4 stunned=%d: no GH\n", stunned); return 1; }
    }
    std::printf("OK: 0x4001 takes life without a get-hit; flag 4 rolls it\n");

    // Thorns on a melee striker (ai.cpp's swing): 200 back on a 400-life zombie.
    d2d::rules::Rng thorns_rng{ 3 };
    Level here{};
    here.id = d2d::rules::level_ids::kBloodMoor;
    std::vector<Monster> pack{ make_monster(*data, int(zombie_row->second), 10.f, 10.f, thorns_rng, 0) };
    auto& zombie = pack[0];
    zombie.stats.hit_points = zombie.hit_points = 400;
    zombie.max_life_frac = zombie.life_frac = 0;
    set_mode(*data, zombie, "A1", 0);
    std::array<Foe, 1> foes{};
    foes[0].x = 11.f; foes[0].y = 10.f;
    foes[0].fighter.thorns = 200;
    const Crowd crowd;
    std::vector<Missile> missiles;
    for (std::uint32_t frame = 1; frame <= 1000 && foes[0].hits == 0; ++frame)
        monster_update(*data, here, zombie, foes, thorns_rng, frame * 40, 0.04f, crowd, missiles, pack);
    if (foes[0].hits < 1 || zombie.hit_points != 200 || !zombie.struck_event || zombie.mode == "GH") {
        std::printf("FAIL: thorns: hits %d life %d struck %d mode %.*s\n", foes[0].hits, zombie.hit_points, int(zombie.struck_event), int(zombie.mode.size()), zombie.mode.data());
        return 1;
    }
    std::printf("OK: thorns (0x4021) hurt the striker with no get-hit\n");
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
