// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A client unit's first seed (FUN_00465fd0, 0x46606a..0x466093): {1, 666}; with a room at its spot,
// that room's client seed stepped once and {its low word, 666}. Each room's stream starts at its
// room1 seed; seed_client_units seeds a level's objects in its object order.

#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <rules.hpp>
#include <spawning.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <map>

using namespace d2d::game;

int main() try {
    // By hand: a level of two rooms, their room1 seeds 5 and 9.
    Level level;
    level.rooms.resize(2);
    level.room1_seeds = { 5, 9 };
    d2d::rules::Rng first{ 5 }, second{ 9 };
    assert(client_unit_seed(level, -1).low == 1 && client_unit_seed(level, -1).high == 666);
    assert(client_unit_seed(level, 0).low == first.next());
    assert(client_unit_seed(level, 1).low == second.next());
    assert(client_unit_seed(level, 0).low == first.next());      // the room's stream steps per unit
    assert(client_unit_seed(level, 0).high == 666);
    level.room1_seeds[1] = 13;                                     // relevel laid room 1 anew: its stream starts over
    d2d::rules::Rng fresh{ 13 };
    assert(client_unit_seed(level, 1).low == fresh.next());

    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) {                   // the by-hand part ran: only the data part skips
        std::printf("SKIP: the Blood Moor's objects: no d2data.mpq in %s\n", data_dir.string().c_str());
        return 0;
    }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    // The Blood Moor's objects: each seeded from its room's stream, in object order.
    const auto* moor = data->level(d2d::rules::level_ids::kBloodMoor);
    assert(moor && !moor->rooms.empty());
    Spawning spawning;
    populate_level(*data, spawning, *moor);                        // every room up: their objects made
    std::map<int, d2d::rules::Rng> streams;
    int objects = 0, roomed = 0;
    for (const auto& npc : moor->npcs) {
        if (npc.root != "objects") continue;
        ++objects;
        assert(npc.client_seeded);
        const int room = npc.room >= 0 ? npc.room : room_of(*moor, npc.x, npc.y);
        if (room < 0) { assert(npc.client_seed.low == 1); continue; }
        ++roomed;
        auto [stream, made] = streams.try_emplace(room, std::size_t(room) < moor->room1_seeds.size() ? moor->room1_seeds[std::size_t(room)] : moor->rooms[std::size_t(room)].seed);
        assert(npc.client_seed.low == stream->second.next() && npc.client_seed.high == 666);
    }
    // A camp has no room list: its objects aren't seeded here, so each keeps its spot (object_frame_delta).
    int camp_objects = 0;
    for (const auto& npc : data->town.npcs)
        if (data->town.rooms.empty() && npc.root == "objects") { assert(!npc.client_seeded); ++camp_objects; }
    assert(!data->town.rooms.empty() || camp_objects > 0);
    std::printf("Blood Moor: %d objects, %d in a room\n", objects, roomed);
    std::fflush(stdout);
    assert(roomed > 0);
    std::printf("test_client_seeds: ok\n");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_client_seeds: %s\n", error.what());
    return 1;
}
