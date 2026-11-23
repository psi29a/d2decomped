// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The Act 1 merc's shot, as tools/emu/merc_shot.py runs game.exe's
// FUN_005ce1b0 (RogueMissile): `--merc-shot` reads its cases (`magic exploding
// level`: the merc's stats 157 / 158 and the skill's level) and prints each
// with the Missiles row d2d shoots, its level and the row's own physical min
// and max at it (256ths, missile_phys), with the game's MPQs (D2_MPQ_DIR).

#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <skills.hpp>

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

int shot_cases(const GameData& data) {
    const auto& arrow = data.missiles.at("arrow");
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        int magic = 0, exploding = 0, level = 0;
        if (!(fields >> magic >> exploding >> level)) {
            std::fprintf(stderr, "malformed case: %s\n", line.c_str());
            return 1;
        }
        // d2d flies arrow whatever the merc wears (merc.cpp), at the level it's given.
        const d2d::rules::CalcEnv env;
        std::printf("%s | %d %d %d %d\n", line.c_str(), arrow.id, level, missile_phys(data, arrow, env, -1, level, false),
                    missile_phys(data, arrow, env, -1, level, true));
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::puts("SKIP: no d2data.mpq (D2_MPQ_DIR)"); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data) { std::puts("FAIL: load_game_data failed"); return 1; }
    if (!data->patched) { std::puts("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)"); return D2D_TEST_SKIPPED; }
    if (argc > 1 && std::string_view(argv[1]) == "--merc-shot") return shot_cases(*data);
    std::puts("OK: --merc-shot prints d2d's merc shot");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_merc_shot: %s\n", error.what());
    return 1;
}
