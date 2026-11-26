// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The camp's tiles follow the order its rooms came up in: game.exe lays each room's tiles as it
// comes up (FUN_0061b190), so a shared edge re-picks on whichever room came up later and the rooms'
// tile seeds roll on from there. relevel lays the camp again (lay_camp_picks' rooms and DT1s), the
// objects' footprints stamped back. Expected floors are game.exe's (tools/emu camp_tiles_order.py,
// map seed 3, the rooms up in room_rects order: floor.dt1, the tile's index in it). Needs the game's
// MPQs (D2_MPQ_DIR) and the 1.14d patch (D2_PATCH_INSTALLER).

#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <spawning.hpp>

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <vector>

using namespace d2d::game;

namespace {

// The floor.dt1 index of the floor picked at (x, y), -1 none.
long floor_at(const Level& town, int x, int y) {
    const auto floor_tiles = town.dt1s[0].tiles();      // the camp's list starts with floor.dt1
    for (const auto& pick : town.picks[std::size_t(y) * std::size_t(town.ds1.width()) + std::size_t(x)])
        if (pick.layer == 1 && pick.tile >= floor_tiles.data() && pick.tile < floor_tiles.data() + floor_tiles.size()) return long(pick.tile - floor_tiles.data());
    return -1;
}

// Every object's footprint bits are in the walk grid where it stands solid.
bool footprints_stamped(const Level& town) {
    const int walk_width = town.ds1.width() * 5;
    for (const auto& npc : town.npcs) {
        if (npc.root != "objects" || npc.stamp == 0 || npc.size_x <= 0 || npc.size_y <= 0 || !(npc.collision >> mode_index(npc.mode) & 1)) continue;
        const int x = int(npc.x * 5), y = int(npc.y * 5);
        if ((town.walk[std::size_t(y) * std::size_t(walk_width) + std::size_t(x)] & npc.stamp) != npc.stamp) return false;
    }
    return true;
}

}  // namespace

// relevel in room_rects order gives game.exe's floors; a player arriving lays the camp in its rooms' order.
int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 3);
    assert(data);
    if (!data->patched) { std::printf("SKIP: these seeds are 1.14d's (set D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    auto& town = data->town;
    assert(!town.picks.empty() && !town.dt1s.empty());
    std::size_t count = 0;
    while (level_room(town, count)) ++count;
    assert(count > 1);
    assert(floor_at(town, 0, 1) == 28);                 // at load: list order (test_camp_tile_picks)

    // The rooms up in room_rects order (camp_tiles_order.py's "reverse": game.exe's list back to front).
    std::vector<std::size_t> came_up(count);
    for (std::size_t i = 0; i < count; ++i) came_up[i] = i;
    const auto lays = town.lays;
    relevel(town, came_up, &*data);
    assert(town.lays == lays + 1);
    struct Expected { int x, y; long index; };
    for (const Expected expected : { Expected{ 0, 1, 29 }, { 0, 2, 34 }, { 0, 11, 34 }, { 0, 13, 31 }, { 0, 14, 32 } }) {
        if (floor_at(town, expected.x, expected.y) != expected.index)
            std::fprintf(stderr, "floor %d,%d: %ld, game.exe %ld\n", expected.x, expected.y, floor_at(town, expected.x, expected.y), expected.index);
        assert(floor_at(town, expected.x, expected.y) == expected.index);
    }
    assert(footprints_stamped(town));

    // A player arriving at the camp's start: the rooms up as it brings them, the camp laid in that order.
    auto spawning = start_spawning(*data, 0);
    player_moved(*data, spawning, town, town.start.first, town.start.second, true);
    const auto& order = spawning.levels[&town].order;
    assert(!order.empty());
    std::vector<std::size_t> expected_laid;               // relevel's list indices: the list is newest first
    std::vector<bool> taken(count);
    for (const auto room : order) if (room < count && !taken[count - 1 - room]) { expected_laid.push_back(count - 1 - room); taken[count - 1 - room] = true; }
    for (std::size_t i = 0; i < count; ++i) if (!taken[i]) expected_laid.push_back(i);
    assert(town.laid == expected_laid);
    assert(footprints_stamped(town));
    std::printf("OK: the camp's tiles follow the order its rooms came up in\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_camp_picks_order: %s\n", error.what());
    return 1;
}
