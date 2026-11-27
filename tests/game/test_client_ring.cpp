// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The client's rooms past the near list (drlg.md "Entering a room"): FUN_0061b390 walks the near list three deep;
// each room first reached at depth 2, whichever level it's of, goes on the status-2 list at its tail, and the client's
// frame (FUN_0061b920, step_client_ring) brings them up from its head, one each 5 frames, so they're made in that
// depth-first order. The frame after the arrival's room1s only clears their count (+0x98). With the game's MPQs.
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <spawning.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace d2d::game;

// Arrives in the Blood Moor, then steps the client's frames: each ring room comes up on its frame, in order.
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
    auto& spawning = world.fight.spawning;
    const auto made = [&](const std::pair<const Level*, int>& entry) {
        const auto found = spawning.levels.find(entry.first);
        return found != spawning.levels.end() && std::ranges::contains(found->second.client_order, std::size_t(entry.second));
    };
    for (const auto& entry : ring) assert(!made(entry));          // not with the arrival's: they wait for the client's frames
    std::vector<int> frame_of(ring.size(), -1);
    for (int frame = 1; frame <= int(ring.size()) * 5 + 10; ++frame) {
        step_client_ring(*data, spawning);
        for (std::size_t index = 0; index < ring.size(); ++index)
            if (frame_of[index] < 0 && made(ring[index])) frame_of[index] = frame;
    }
    for (std::size_t index = 0; index < ring.size(); ++index) {
        if (frame_of[index] != 6 + int(index) * 5) {
            std::printf("FAIL: ring room %zu came up at frame %d, not %d\n", index, frame_of[index], 6 + int(index) * 5);
            return 1;
        }
    }
    std::printf("OK: %zu rooms past the near list, made depth first one each 5 frames (FUN_0061b390, FUN_0061b920)\n", ring.size());
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
