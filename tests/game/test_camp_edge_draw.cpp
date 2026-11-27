// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The camp up first, a Blood Moor room on its edge finds the camp's edge
// tiles (FUN_0066c370 -> FUN_0066e940) and doesn't pick them: they are the
// camp room's, picked on its seed from its DT1s (FUN_0066d820). d2d lays the
// camp beside the Blood Moor without its DT1s (camp_rooms), so a cell it
// finds draws the tile the camp's own picks hold there (lay_camp_picks,
// tools/emu camp_tiles.py), not the Blood Moor's first tile of its kind.

#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <spawning.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

namespace fs = std::filesystem;
using namespace d2d::game;

// The camp up, then the Blood Moor: its cells over the camp's draw the camp's picks.
int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data || !data->patched) { std::printf("SKIP: these checks are 1.14d's (set D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    const auto* moor = data->level(d2d::rules::level_ids::kBloodMoor);
    const auto& town = data->town;
    if (!moor || town.picks.empty()) { std::printf("SKIP: no Blood Moor, or the camp has no picks\n"); return D2D_TEST_SKIPPED; }
    const auto beside = std::ranges::find(moor->nearby, &town, &Level::Near::level);
    assert(beside != moor->nearby.end());

    Spawning spawning;
    (void)populate_level(*data, spawning, town);                   // the camp's rooms up first
    (void)populate_level(*data, spawning, *moor);                  // then the Blood Moor's: it finds the camp's edge
    // Every Blood Moor cell over the camp's: count the picks that are the camp's own tile there.
    int shared = 0, camp_drawn = 0;
    const int width = moor->ds1.width();
    for (int y = 0; y < moor->ds1.height(); ++y)
        for (int x = 0; x < width; ++x) {
            const int at_x = x - beside->dx, at_y = y - beside->dy;
            if (at_x < 0 || at_y < 0 || at_x >= town.ds1.width() || at_y >= town.ds1.height()) continue;
            const auto& camp = town.picks[std::size_t(at_y) * std::size_t(town.ds1.width()) + std::size_t(at_x)];
            for (const auto& pick : moor->picks[std::size_t(y) * std::size_t(width) + std::size_t(x)]) {
                ++shared;
                for (const auto& camp_pick : camp) camp_drawn += camp_pick.tile == pick.tile;
            }
        }
    std::printf("%d Blood Moor picks over the camp's cells, %d the camp's own tile\n", shared, camp_drawn);
    assert(shared > 0 && camp_drawn > 0);
    std::printf("OK: the camp's edge tiles the Blood Moor finds draw as the camp picked them\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
