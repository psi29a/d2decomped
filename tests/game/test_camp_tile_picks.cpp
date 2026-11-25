// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The camp's tiles are the rarity pick on each room's seed (FUN_0066d820), not load_town's first match
// in the DT1s: GameData::town carries Level::picks (lay_camp_picks). Expected floor tiles are game.exe's
// (tools/emu camp_tiles.py, map seed 3: DT1 floor.dt1, the tile's index in it). Needs the game's MPQs
// (D2_MPQ_DIR) and the 1.14d patch (D2_PATCH_INSTALLER).

#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 3);
    assert(data);
    if (!data->patched) { std::printf("SKIP: these seeds are 1.14d's (set D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    const Level& town = data->town;
    assert(!town.picks.empty() && !town.dt1s.empty());
    // The first DT1 of the camp's list is floor.dt1: a floor's index in it, as game.exe picked it.
    const auto floor_tiles = town.dt1s[0].tiles();
    struct Expected { int x, y; long index; };
    for (const Expected expected : { Expected{ 0, 0, 32 }, { 1, 0, 34 }, { 0, 1, 28 }, { 0, 10, 34 } }) {
        const auto& cell = town.picks[std::size_t(expected.y) * std::size_t(town.ds1.width()) + std::size_t(expected.x)];
        bool found = false;
        for (const auto& pick : cell)
            if (pick.layer == 1 && pick.tile >= floor_tiles.data() && pick.tile < floor_tiles.data() + floor_tiles.size()) {
                assert(pick.tile - floor_tiles.data() == expected.index);
                found = true;
            }
        assert(found);
    }
    std::printf("OK: the camp's floors are game.exe's rarity picks\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_camp_tile_picks: %s\n", error.what());
    return 1;
}
