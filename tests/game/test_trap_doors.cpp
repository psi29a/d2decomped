// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Trap doors with the game's MPQs (D2_MPQ_DIR; the 1.14d ones with
// D2_PATCH_INSTALLER): FUN_00581eb0 takes the first warp tile unit of the
// door's room (assert 0x40e without one), so every trap door (object 74,
// OperateFn 16: Lut Gholein's and the Lost City's presets) has a warp in
// its room, and only one: the list's order never picks.

#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <object_ids.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

namespace fs = std::filesystem;
using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && data->town.id == 1);
    if (!data->patched) { std::printf("SKIP: these checks are 1.14d's (set D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    constexpr int kTrapDoorObject = 74;                 // objects.txt "Trap Door", OperateFn 16
    int doors = 0;
    for (int id = 2; id < 137; ++id) {
        const auto* level = data->level(id);
        if (!level) continue;
        for (const auto& unit : level->units) {
            if (unit.type != d2d::rules::unit_type::kObject || unit.id != kTrapDoorObject) continue;
            const int room = room_of(*level, (float(unit.x) + 0.5f) / 5, (float(unit.y) + 0.5f) / 5);
            int warps = 0;
            for (const auto& warp : level->warps) warps += room >= 0 && room_of(*level, warp.unit_x, warp.unit_y) == room;
            std::printf("level %d: trap door, %d warp(s) in its room\n", id, warps);
            assert(warps == 1);
            ++doors;
        }
    }
    assert(doors > 0);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
