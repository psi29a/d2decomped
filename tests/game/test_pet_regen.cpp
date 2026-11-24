// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A summon's own life regeneration, as tools/emu/pet_regen.py runs game.exe's
// FUN_00573cb0 (stat 0x4a) and FUN_005a6920 (its unit event 3): `--pet-regen`
// reads its cases (`roll damage_regen life max_life frames`) and prints each
// with the regeneration d2d gives the pet, its life after the frames and
// whether the event is still filed. No game data.

#include <monsters.hpp>
#include <montypes.hpp>
#include <rules.hpp>

#include <cstdio>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

int regen_cases() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        int roll = 0, damage_regen = 0, life = 0, max_life = 0, frames = 0;
        if (!(fields >> roll >> damage_regen >> life >> max_life >> frames)) {
            std::fprintf(stderr, "malformed case: %s\n", line.c_str());
            return 1;
        }
        // A row made with that roll: noRatio, so its life is as written (rules::monster_stats).
        d2d::rules::Monsters monsters;
        monsters.lvl.resize(1);
        auto& type = monsters.types.emplace_back();
        type.no_ratio = true;
        type.damage_regen = damage_regen;
        type.diff[0].min_hp = type.diff[0].max_hp = roll;
        d2d::rules::Rng rng{ 1 };
        const auto stats = d2d::rules::monster_stats(monsters, 0, 0, rng);
        // d2d's pets never regenerate: their life stays as it is (Fight::pets_turn runs no event 3).
        std::printf("%s | %d %d %d\n", line.c_str(), stats.regen, life, 1);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    if (argc > 1 && std::string_view(argv[1]) == "--pet-regen") return regen_cases();
    std::puts("OK: --pet-regen prints d2d's pet regeneration");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_pet_regen: %s\n", error.what());
    return 1;
}
