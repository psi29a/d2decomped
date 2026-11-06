// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// FUN_00463740 (World::near_room): a spot in the room's near list holds,
// and that list (FUN_0066c370) takes the next-door level's close rooms too:
// a spot across the Blood Moor's edge, in the camp, counts.

#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <world.hpp>

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
    const auto* moor = data->level(d2d::rules::level_ids::kBloodMoor);
    if (!moor || moor->nearby.empty()) { std::printf("SKIP: no Blood Moor linked to the camp\n"); return D2D_TEST_SKIPPED; }

    World world(&*data, -1, -1);
    world.level = moor;
    bool across = false;
    for (std::size_t index = 0; index < moor->rooms.size() && !across; ++index) {
        const auto& room = moor->rooms[index];
        const float from_x = float(room.x) + float(room.width) / 2, from_y = float(room.y) + float(room.height) / 2;
        for (const auto& rect : missile_rooms(*data, *moor, from_x, from_y)) {
            // The rect's centre in the Blood Moor's own cells.
            const float x = (float(rect[0] + rect[2] / 2) + 0.5f) / 5 - float(moor->world_x), y = (float(rect[1] + rect[3] / 2) + 0.5f) / 5 - float(moor->world_y);
            if (moor->inside(x, y)) continue;
            assert(world.near_room(from_x, from_y, x, y));
            across = true;
            break;
        }
    }
    assert(across);
    std::printf("OK: a next-door level's close room is in the near list\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
