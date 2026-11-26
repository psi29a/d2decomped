// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A camp room coming up after the first roll takes the room1 seed it gets as it comes
// up (FUN_0061b190 lays its tiles then, FUN_006422a0 steps its tile seed once more):
// keep_camp_seeds lays the camp again with the rooms up by then, and a room already
// up keeps its stream. Needs the game's MPQs (D2_MPQ_DIR) and the 1.14d patch
// (D2_PATCH_INSTALLER).

#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <spawning.hpp>

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

using namespace d2d::game;

namespace {

// camp_room1_seeds, waiting out the level builder's lock.
std::vector<std::uint32_t> seeds_for(const GameData& data, const std::vector<std::size_t>& came_up) {
    for (int tries = 0; tries < 200; ++tries) {
        if (const auto seeds = camp_room1_seeds(data, came_up)) return *seeds;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(false && "the level builder stayed busy");
    return {};
}

// keep_camp_seeds, waiting out the level builder's lock.
void keep(const GameData& data, Spawning::LevelState& camp) {
    bool kept = false;
    for (int tries = 0; tries < 200 && !(kept = keep_camp_seeds(data, data.town, camp)); ++tries) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(kept);
}

}  // namespace

// A room pair whose later seed depends on the order, then keep_camp_seeds over that order.
int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 3);
    assert(data);
    if (!data->patched) { std::printf("SKIP: these seeds are 1.14d's (set D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }

    // A first room up, and a later one whose seed depends on coming up after it.
    std::size_t count = 0;
    while (level_room(data->town, count)) ++count;
    assert(count > 1);
    std::optional<std::pair<std::size_t, std::size_t>> pair;
    for (std::size_t first = 0; first < count && !pair; ++first) {
        const auto alone = seeds_for(*data, { first });
        for (std::size_t later = 0; later < count && !pair; ++later)
            if (later != first && seeds_for(*data, { first, later })[later] != alone[later]) pair = { first, later };
    }
    assert(pair);
    const auto [first, later] = *pair;

    Spawning::LevelState camp;
    camp.order = { first };
    keep(*data, camp);
    const auto first_seed = camp.room_seeds[first];
    camp.room_seeds[first].next();                       // the first room rolled: its stream moves on
    const auto rolled = camp.room_seeds[first];
    camp.order.push_back(later);                         // then the later room comes up
    keep(*data, camp);
    const auto expected = seeds_for(*data, { first, later });
    assert(camp.room_seeds[later].low == expected[later]);
    assert(camp.room_seeds[first].low == rolled.low && camp.room_seeds[first].high == rolled.high);
    assert(first_seed.low == expected[first]);           // laying more rooms never changes an earlier room's seed
    std::printf("OK: camp room %zu, up after room %zu, takes the seed it comes up with\n", later, first);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_camp_seeds_later: %s\n", error.what());
    return 1;
}
