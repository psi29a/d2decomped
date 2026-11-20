// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Corpse Explosion's blow, as tools/emu/corpse_explosion.py runs game.exe's
// FUN_005c4df0: `--corpse-explosion` reads its cases (`type difficulty level
// clvl seed_low seed_high calc1 calc2 calc3 range`) and prints each with
// `| physical element walk reach2`: rules::corpse_life_fine,
// corpse_blast_fine on that unit seed, corpse_blast_split (Corpse
// Explosion's fire EType) and corpse_blast_reach (the reach squared).
// Without arguments: the split, the reach and the calc1 % on a few typed-in
// numbers.

#include <combat.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace fs = std::filesystem;
using namespace d2d::game;

namespace {

int corpse_explosion_cases(const GameData& game_data) {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        int type = 0, difficulty = 0, level = 0, clvl = 0, calc1 = 0, calc2 = 0, calc3 = 0, aura_range = 0;
        std::uint32_t seed_low = 0, seed_high = 0;
        if (!(fields >> type >> difficulty >> level >> clvl >> seed_low >> seed_high >> calc1 >> calc2 >> calc3 >> aura_range)
            || difficulty < 0 || difficulty > 2) {
            std::fprintf(stderr, "malformed case: %s\n", line.c_str());
            return 1;
        }
        d2d::rules::Rng seed{ seed_low };
        seed.high = seed_high;
        const int life_fine = d2d::rules::corpse_life_fine(game_data.monsters, type, difficulty, level);
        const int fine = d2d::rules::corpse_blast_fine(life_fine, calc1, calc2, seed, level, clvl);
        const auto [physical, element] = d2d::rules::corpse_blast_split(fine, calc3, true);
        const auto [walk, reach] = d2d::rules::corpse_blast_reach(aura_range);
        std::printf("%s | %d %d %d %d\n", line.c_str(), physical, element, walk, reach * reach);
    }
    return 0;
}

int blast_checks() {
    using d2d::rules::corpse_blast_reach;
    using d2d::rules::corpse_blast_split;
    assert(corpse_blast_split(1000, 50, true) == std::pair(500, 500));
    assert(corpse_blast_split(1000, 150, true) == std::pair(0, 1000));   // calc3 held at 100
    assert(corpse_blast_split(1000, -5, true) == std::pair(1000, 0));
    assert(corpse_blast_split(1000, 50, false) == std::pair(1000, 0));   // no EType: all physical
    assert(corpse_blast_reach(7) == std::pair(4, 3) && corpse_blast_reach(6) == std::pair(3, 3));
    // The % is FUN_00483360's value and the life its times (0x5c4f31): a life over 0x10000 is cut to hundredths first.
    d2d::rules::Rng seed{ 99 };
    assert(d2d::rules::corpse_blast_fine(0x20000, 120, 120, seed, 30, 40) == 0x20000 / 100 * 120);
    std::puts("OK: Corpse Explosion's split, reach and calc1 %");
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1 && std::string_view(argv[1]) == "--corpse-explosion") {
        const char* mpq_dir = std::getenv("D2_MPQ_DIR");
        if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::fputs("--corpse-explosion needs D2_MPQ_DIR\n", stderr); return 1; }
        const char* patch = std::getenv("D2_PATCH_INSTALLER");
        auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
        if (!data || !data->patched) { std::fputs("--corpse-explosion needs the 1.14d data (D2_PATCH_INSTALLER)\n", stderr); return 1; }
        return corpse_explosion_cases(*data);
    }
    return blast_checks();
} catch (const std::exception& error) {
    std::printf("test_corpse_explosion: %s\n", error.what());
    return 1;
}
