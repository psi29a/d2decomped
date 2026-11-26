// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The client's rooms past the near list (drlg.md "Entering a room"): FUN_0061b390 walks the near list three deep;
// each room first reached at depth 2, whichever level it's of, goes on the status-2 list at its tail, and the client's
// frame (FUN_0061b920) brings them up from its head, so they're made in that depth-first order. With the game's MPQs.
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <span>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::puts("SKIP: no d2data.mpq (D2_MPQ_DIR)"); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data) { std::puts("FAIL: load_game_data failed"); return 1; }
    if (!data->patched) { std::puts("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)"); return D2D_TEST_SKIPPED; }
    const auto* moor = data->level(d2d::rules::level_ids::kBloodMoor);
    assert(moor && !moor->rooms.empty());
    World world(&*data, -1, -1);
    // The Blood Moor's room nearest its middle.
    const float mid_x = float(moor->ds1.width()) / 2, mid_y = float(moor->ds1.height()) / 2;
    const auto [at_x, at_y] = moor->nearest_free(mid_x, mid_y);
    world.arrive(moor, at_x, at_y, "a test");
    const auto centre = [](const Level& level, int room) {
        const auto rect = level_room(level, std::size_t(room));
        return std::pair{ float(rect->x) + float(rect->width) / 2, float(rect->y) + float(rect->height) / 2 };
    };
    const auto first = near_rooms(*data, *moor, at_x, at_y);
    assert(!first.empty());
    std::vector<std::pair<const Level*, int>> ring;
    for (const auto& [level, room] : first) {
        const auto [room_x, room_y] = centre(*level, room);
        for (const auto& further : near_rooms(*data, *level, room_x, room_y))
            if (!std::ranges::contains(first, further) && !std::ranges::contains(ring, further)) ring.push_back(further);
    }
    assert(!ring.empty());
    const auto& levels = world.fight.spawning.levels;
    std::vector<std::size_t> own;                                 // this level's ring rooms, in the walk's order
    for (const auto& [level, room] : ring) {
        const auto found = levels.find(level);
        assert(found != levels.end() && std::ranges::contains(found->second.client_order, std::size_t(room)));
        if (level == moor) own.push_back(std::size_t(room));
    }
    const auto& order = levels.at(moor).client_order;
    assert(order.size() >= own.size() && std::ranges::equal(own, std::span(order).last(own.size())));
    std::printf("OK: %zu rooms past the near list, made depth first (FUN_0061b390, FUN_0061b920)\n", ring.size());
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
