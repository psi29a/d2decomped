// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// preset_rooms (the automap's walk of the camp) rolls each room's tile picks on
// its DRLG room seed, as game.exe's FUN_0066d820 rolls the room's seed: a floor
// key with several rarities takes game.exe's tile (tools/emu camp_tiles.py, map
// seed 3: floor.dt1's index), and every floor matches lay_camp_picks' drawn camp.
// Needs the game's MPQs (D2_MPQ_DIR) and the 1.14d patch (D2_PATCH_INSTALLER).

#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <tile_pick.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

using namespace d2d::game;

// The camp walked by preset_rooms with map seed 3: its floors against game.exe's and the drawn camp's.
int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 3);
    assert(data);
    if (!data->patched) { std::printf("SKIP: these seeds are 1.14d's (set D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    namespace ids = d2d::rules::level_ids;
    const Level* moor = data->level(ids::kBloodMoor);   // lends its DRLG tables, as automap_reveal's
    assert(moor && moor->assets);
    const auto& assets = *moor->assets;
    int def = 0;
    for (const auto& [preset_def, preset] : assets.data.presets)
        if (preset.level_id == ids::kRogueEncampment && (def == 0 || preset_def < def)) def = preset_def;
    assert(def != 0);
    const auto list = d2d::drlg::room_dt1_list(assets.data.presets.at(def).dt1_mask, data->layouts.prest_automap_dt1s.at(ids::kRogueEncampment));
    const Level& town = data->town;
    assert(!list.empty() && list.size() == town.dt1s.size() && !town.picks.empty());

    const auto rooms = preset_rooms(*data, town, assets);
    assert(!rooms.empty());
    // game.exe's floors (test_camp_tile_picks): floor.dt1, the camp's first DT1.
    struct Expected { int x, y, index; };
    for (const Expected expected : { Expected{ 0, 0, 32 }, { 1, 0, 34 }, { 0, 1, 28 }, { 0, 10, 34 } }) {
        bool found = false;
        for (const auto& room : rooms)
            for (const auto& tile : room.tiles)
                if (tile.layer == 1 && tile.x == expected.x && tile.y == expected.y && tile.file == list[0]) {
                    assert(tile.index == expected.index);
                    found = true;
                }
        assert(found);
    }
    // Every floor the walk picked is one lay_camp_picks drew at that cell.
    std::size_t floors = 0;
    for (const auto& room : rooms)
        for (const auto& tile : room.tiles) {
            if (tile.layer != 1) continue;
            const auto file = std::size_t(std::ranges::find(list, tile.file) - list.begin());
            assert(file < list.size());
            const auto tiles = town.dt1s[file].tiles();
            const auto& cell = town.picks[std::size_t(tile.y) * std::size_t(town.ds1.width()) + std::size_t(tile.x)];
            assert(std::ranges::any_of(cell, [&](const Level::Pick& pick) { return pick.layer == 1 && pick.tile == tiles.data() + tile.index; }));
            ++floors;
        }
    std::printf("OK: the camp's %zu rooms pick their %zu floors on their seeds, as game.exe\n", rooms.size(), floors);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_preset_room_seeds: %s\n", error.what());
    return 1;
}
