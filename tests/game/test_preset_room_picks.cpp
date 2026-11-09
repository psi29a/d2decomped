// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The Rogue Encampment's rooms as preset_rooms walks them for the automap
// (automap_whole): each floor and wall picked from the preset's DT1 list
// (GameData::prest_automap_dt1s, LvlPrest Dt1Mask) as FUN_0066d820 picks,
// so the automap reads the picked tile's header: the word's own
// (orientation, style, sequence) where the list has one, else (10, 0, 0).
// The miniature towns get no DT1s. Needs the game's MPQs (D2_MPQ_DIR).

#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <tile_pick.hpp>

#include <cassert>
#include <cstddef>
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
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    namespace ids = d2d::rules::level_ids;
    assert(data->prest_automap_dt1s.contains(ids::kRogueEncampment));
    for (const int town : ids::kMiniatureTowns) assert(!data->prest_automap_dt1s.contains(town));

    const Level* moor = data->level(ids::kBloodMoor);   // lends its DRLG tables, as automap_reveal's
    assert(moor && moor->assets);
    const auto& assets = *moor->assets;
    int def = 0;
    for (const auto& [preset_def, preset] : assets.data.presets)
        if (preset.level_id == ids::kRogueEncampment && (def == 0 || preset_def < def)) def = preset_def;
    assert(def != 0);
    const auto list = d2d::drlg::room_dt1_list(assets.data.presets.at(def).dt1_mask, data->prest_automap_dt1s.at(ids::kRogueEncampment));
    assert(!list.empty());

    const auto rooms = preset_rooms(*data, data->town, assets);
    assert(!rooms.empty());
    std::size_t own = 0, fallback = 0;
    for (const auto& room : rooms)
        for (const auto& tile : room.tiles) {
            if (tile.layer > 1) continue;
            assert(tile.file && tile.index >= 0);
            const auto& head = tile.file->tiles[std::size_t(tile.index)];
            const int style = tile.word ? int((tile.word >> 20) & 0x3f) : 0, sequence = tile.word ? int((tile.word >> 8) & 0xff) : 0;
            if (d2d::drlg::tile_candidates(list, tile.orient, style, sequence).empty()) {
                assert(head.orient == 10 && head.style == 0 && head.seq == 0);
                ++fallback;
            } else {
                assert(head.orient == tile.orient && head.style == style && head.seq == sequence);
                ++own;
            }
        }
    assert(own > 0);
    std::printf("OK: the camp's %zu rooms: %zu tiles their word's, %zu the (10, 0, 0) stand-in\n", rooms.size(), own, fallback);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_preset_room_picks: %s\n", error.what());
    return 1;
}
