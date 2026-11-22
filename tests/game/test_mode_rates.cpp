// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A monster's animation rate in each mode, as tools/emu/mode_rates.py runs
// game.exe's FUN_00623f50 from its entry: `--mode-rates` reads its cases
// (`class mode skill speed vel frw fhr fbr held ias rate69`) and prints each
// with `| rate`: rules::monster_mode_rate on d2d's MonStats row (its walk /
// run base, npc, under 0x19a) and the case's stats.
// Without arguments: the walk / run base d2d derives (FUN_00650f00) on
// made-up rows, and the get-hit rate as rules::get_hit_rate's.

#include <ai.hpp>
#include <combat.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <monsters.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;
using namespace d2d::game;

namespace {

int rate_cases(const GameData& data) {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        int cls = 0, mode = 0, skill = 0, speed = 0, vel = 0, frw = 0, fhr = 0, fbr = 0, held = 0, ias = 0, rate69 = 0;
        if (!(fields >> cls >> mode >> skill >> speed >> vel >> frw >> fhr >> fbr >> held >> ias >> rate69) || mode < 0 || mode >= 16) {
            std::fprintf(stderr, "malformed case: %s\n", line.c_str());
            return 1;
        }
        const auto* row = data.skills.get(skill);
        const bool known = cls >= 0 && std::size_t(cls) < data.monsters.types.size();
        d2d::rules::ModeRateIn rate_in{ .mode = mode, .low_class = cls < 0x19a,
                                   .npc = known && std::size_t(cls) < data.mon_is_npc.size() && data.mon_is_npc[std::size_t(cls)],
                                   .use_attack_rate = row && row->use_attack_rate, .speed = speed, .velocity_pct = vel, .frw = frw, .fhr = fhr,
                                   .fbr = fbr, .ias = ias, .blocking = held != 0, .attack_rate = rate69, .other_rate = rate69 };
        if (known) rate_in.base = { data.monsters.types[std::size_t(cls)].walk_base, data.monsters.types[std::size_t(cls)].run_base };
        const int rate = d2d::rules::monster_mode_rate(rate_in);
        std::printf("%s | %d\n", line.c_str(), rate);
    }
    return 0;
}

// FUN_00650f00 on made-up rows: the walk base scales its base row's AnimData
// speed by Velocity (none when the base row is itself or its Velocity isn't
// above 0); the run base scales its start by Run the same way.
void base_check() {
    using d2d::rules::monster_run_base;
    using d2d::rules::monster_walk_base;
    assert(monster_walk_base(true, 256, 9, 6) == 256 && monster_walk_base(false, 256, 9, 6) == 384 && monster_walk_base(false, 256, 9, 0) == 256);
    assert(monster_run_base(false, 192, 12, 8) == 288 && monster_run_base(true, 192, 12, 8) == 192 && monster_run_base(false, 0, 12, 8) == 0);
    assert(monster_walk_base(false, 0x7fff, 9, 1) == 0x7fff);
    std::puts("OK: the walk / run base as FUN_00650f00 derives it");
}

// With the game data: rules::monster_mode_rate's get-hit is rules::get_hit_rate
// (the view's), and a slowed zombie's RN rate stays above 0 under any anim_pct
// (the old division by 100 + anim_pct).
int game_check(const GameData& data) {
    for (const int speed : { 128, 256, 400 })
        for (const int fhr : { 0, 20, 60, 200 })
            assert(d2d::rules::monster_mode_rate({ .mode = 3, .speed = speed, .fhr = fhr }) == d2d::rules::get_hit_rate(speed, fhr));
    Monster zombie;
    zombie.type = data.monsters.row("zombie1");
    zombie.npc = data.mon_npc[std::size_t(zombie.type)];
    zombie.anim_pct = -150;
    zombie.walk_pct = 25;
    assert(mode_rate(data, zombie, "RN") > 0 && anim_rate_pct(data, zombie, "RN") > -100 && anim_rate_pct(data, zombie, "WL") > -100);
    std::puts("OK: the get-hit rate as get_hit_rate's; a zombie at anim_pct -150 still runs (its rate from the walk %)");
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    const bool cases = argc > 1 && std::string_view(argv[1]) == "--mode-rates";
    if (!cases) base_check();   // made-up rows: runs without the game's files
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) {
        if (cases) { std::fputs("--mode-rates needs D2_MPQ_DIR\n", stderr); return 1; }
        std::puts("SKIP (the game check): no d2data.mpq (D2_MPQ_DIR)");
        return 0;
    }
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data) { std::puts("FAIL: load_game_data failed"); return 1; }
    if (!data->patched) {
        if (cases) { std::fputs("--mode-rates needs the 1.14d data (D2_PATCH_INSTALLER)\n", stderr); return 1; }
        std::puts("SKIP (the game check): needs the 1.14d data (D2_PATCH_INSTALLER)");
        return 0;
    }
    if (!cases) return game_check(*data);
    return rate_cases(*data);
} catch (const std::exception& error) {
    std::printf("test_mode_rates: %s\n", error.what());
    return 1;
}
