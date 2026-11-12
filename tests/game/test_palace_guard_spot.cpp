// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Lut Gholein's palace guard (object 122, FUN_0059f440): FUN_005b3090(x + 1, y,
// 0x14b, 1, 0) -> FUN_005b2a00 (spawn_spot, radius -1). The spot is tried on
// the room1 seed while the level is built, before stamp_footprints, so the
// check uses Level::tile_walk. A miss keeps the subtile asked. Jerhyn stays
// on Level::free_area (test_town_spots). Needs the 1.14d MPQs.

#include <ai.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <monster_ids.hpp>
#include <object_ids.hpp>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <utility>

namespace fs = std::filesystem;
using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: these checks are 1.14d's (set D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    assert(std::size_t(d2d::rules::monster_ids::kPalaceGuard) < data->mon_bin.size());
    const auto* town = data->level(d2d::rules::level_ids::kLutGholein);
    assert(town && town->tile_walk.size() == town->walk.size());
    auto& town_level = const_cast<Level&>(*town);
    auto saved_walk = town_level.walk;
    town_level.walk = town_level.tile_walk;
    const int guard_row = int(data->mon_bin[std::size_t(d2d::rules::monster_ids::kPalaceGuard)]);
    int guards = 0;
    for (const auto& unit : town->units) {
        if (unit.type != d2d::rules::unit_type::kObject || unit.id != 122) continue;
        const int asked_x = unit.x + 1, asked_y = unit.y;
        const int guard_room = room_of(*town, (float(asked_x) + 0.5f) / 5, (float(asked_y) + 0.5f) / 5);
        int spot_x = asked_x, spot_y = asked_y;
        bool placed = false;
        if (guard_room >= 0 && std::size_t(guard_room) < town->rooms.size()) {
            Spawning::LevelState rooms_in_play;
            rooms_in_play.up.assign(town->rooms.size(), false);
            rooms_in_play.up[std::size_t(guard_room)] = true;
            rooms_in_play.room_seeds.assign(town->rooms.size(), d2d::rules::Rng{});
            const std::uint32_t room_seed = std::size_t(guard_room) < town->room1_seeds.size() ? town->room1_seeds[std::size_t(guard_room)]
                                                                                                : town->rooms[std::size_t(guard_room)].seed;
            rooms_in_play.room_seeds[std::size_t(guard_room)] = d2d::rules::Rng{ room_seed };
            const Crowd crowd;
            placed = spawn_spot(*data, *town, crowd, &rooms_in_play, guard_row, asked_x, asked_y, -1, guard_room, spot_x, spot_y);
        }
        const int expect_x = placed ? spot_x : asked_x, expect_y = placed ? spot_y : asked_y;
        for (const auto& npc : town->npcs) {
            if (npc.palace != 4) continue;
            assert(npc.hc_idx == d2d::rules::monster_ids::kPalaceGuard);
            const int at_x = int(std::floor(npc.x * 5)), at_y = int(std::floor(npc.y * 5));
            assert(at_x == expect_x && at_y == expect_y);
            ++guards;
        }
    }
    town_level.walk = std::move(saved_walk);
    assert(guards == 1);
    std::puts("test_palace_guard_spot: ok");
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
