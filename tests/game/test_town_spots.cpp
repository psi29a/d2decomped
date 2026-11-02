// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Town quest spots with the game's MPQs (D2_MPQ_DIR; the 1.14d ones with
// D2_PATCH_INSTALLER): Lut Gholein's Jerhyn (FUN_0059f380 / FUN_0059ef70 ->
// FUN_00545340, Level::free_area) at the search's answer round the spot
// asked; Harrogath's Nihlathak marker (object 461, FUN_005459a0) already
// inside its room less the last row and column (bugs.md #47 unreached).

#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <object_ids.hpp>

#include <cassert>
#include <cmath>
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
    const auto* town = data->level(d2d::rules::level_ids::kLutGholein);
    assert(town);
    int checked = 0;
    for (const auto& unit : town->units) {
        if (unit.type != d2d::rules::unit_type::kObject || (unit.id != 121 && unit.id != 122)) continue;
        const int asked_x = unit.id == 121 ? unit.x : unit.x - 10, asked_y = unit.id == 121 ? unit.y : unit.y - 3;
        const int size = unit.id == 121 ? 2 : 3;
        const auto spot = town->free_area(asked_x, asked_y, size, 0x100, 100);
        for (const auto& npc : town->npcs) {
            if (npc.palace != (unit.id == 121 ? 1 : 2)) continue;
            const int at_x = int(std::floor(npc.x * 5)), at_y = int(std::floor(npc.y * 5));
            assert(spot ? at_x == spot->first && at_y == spot->second : at_x == asked_x && at_y == asked_y);
            ++checked;
        }
    }
    assert(checked == 2);
    const auto* harrogath = data->level(d2d::rules::level_ids::kHarrogath);
    assert(harrogath);
    int markers = 0;
    for (std::size_t i = 0; i < harrogath->units.size(); ++i) {
        const auto& unit = harrogath->units[i];
        if (unit.type != d2d::rules::unit_type::kObject || unit.id != 461) continue;
        const auto& room = harrogath->rooms[std::size_t(harrogath->unit_rooms[i])];
        const int left = room.x * 5, top = room.y * 5;
        assert(unit.x >= left && unit.y >= top && unit.x < left + room.width * 5 - 1 && unit.y < top + room.height * 5 - 1);
        ++markers;
    }
    assert(markers == 1);
    std::puts("test_town_spots: ok");
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
