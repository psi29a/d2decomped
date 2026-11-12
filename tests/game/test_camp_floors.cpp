// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The camp's steps are all dirt in game.exe too: FUN_004cadb0 takes the floor
// tile's DT1 material (FUN_004cad60) and none of the camp's floor tiles (LvlTypes
// 1's DT1s by LvlPrest 1's Dt1Mask, as load_town lists them) names one, so every
// step takes the level's SoundEnviron Material 1, dirt (tools/emu/floor_material.py).

#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <sound_ids.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;
using namespace d2d::game;
namespace snd = d2d::rules::sound_ids;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 1);
    assert(data);
    if (!data->patched) { std::printf("SKIP: these checks are 1.14d's (set D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }

    const auto& town = data->town;
    assert(!town.dt1s.empty());
    assert(town.material == 1);                                    // SoundEnviron 1 (ESOUNDENVIRON_TOWN_1): dirt
    int floors = 0;
    for (const auto& dt1 : town.dt1s)
        for (const auto& tile : dt1.tiles())
            if (tile.type == 0) {                                  // a floor
                ++floors;
                assert(snd::floor_material(tile.material_flags) == 0);
                struct CampFloor { int x, y; unsigned material; };
                const std::vector<std::vector<CampFloor>> room{ { { 0, 0, tile.material_flags } } };
                assert(snd::first_floor_material(room, 0, 0, town.material) == 1);
            }
    assert(floors > 0);
    std::printf("OK: none of the camp's %d floor tiles names a material: every step is dirt\n", floors);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
}
