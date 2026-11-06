// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The bridge's event 7 (FUN_005bcac0): with Mephisto dead, only a player in
// the bridge's own room (its room's units, room +0x74) within 18 of it
// (FUN_00641530) raises it a step; one as near in the next room doesn't.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <monsters.hpp>
#include <object_ids.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <optional>
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
    assert(data && data->town.id == 1);
    if (!data->patched) { std::printf("SKIP: these checks are 1.14d's (set D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    const auto* durance = data->level(d2d::rules::level_ids::kDuranceOfHateLevel3);
    if (!durance) { std::printf("SKIP: no Durance of Hate 3\n"); return D2D_TEST_SKIPPED; }

    d2d::rules::Rng rng(7);
    constexpr int kSorceress = 1;
    auto made = new_character(*data, kSorceress, "Headless", false, true, rng);
    Character character;
    character.character_class = kSorceress;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);

    World world(&*data, -1, -1);
    world.enter(character);
    populate_level(*data, world.fight.spawning, *durance);
    const auto bridge = std::ranges::find(durance->npcs, d2d::rules::object_ids::kMephistoBridge, &Npc::object_id);
    if (bridge == durance->npcs.end()) { std::printf("SKIP: no bridge in Durance 3 once populated\n"); return D2D_TEST_SKIPPED; }
    world.level = durance;
    world.npc_states = npc_start(*durance);
    world.guardian.dead = true;

    // Two spots within 18 subtiles: one in the bridge's room, one in another.
    const int bridge_room = room_of(*durance, bridge->x, bridge->y);
    std::optional<std::pair<float, float>> inside, outside;
    for (int off_y = -17; off_y <= 17; ++off_y)
        for (int off_x = -17; off_x <= 17; ++off_x) {
            if (d2d::rules::unit_distance(off_x, off_y, 2, bridge->size_x) >= 18) continue;
            const float spot_x = (std::floor(bridge->x * 5) + float(off_x) + 0.5f) / 5, spot_y = (std::floor(bridge->y * 5) + float(off_y) + 0.5f) / 5;
            const int room = room_of(*durance, spot_x, spot_y);
            if (room == bridge_room && !inside) inside = std::pair{ spot_x, spot_y };
            if (room >= 0 && room != bridge_room && !outside) outside = std::pair{ spot_x, spot_y };
        }
    assert(inside && outside);
    const std::pair key{ durance, int(bridge - durance->npcs.begin()) };
    std::uint32_t now_ms = 1000;
    const auto check = [&](std::pair<float, float> spot) {
        world.actor->player.x = spot.first;
        world.actor->player.y = spot.second;
        world.bridge_check = 0;
        world.mephisto_bridge(now_ms += 1000);
        return world.doors.contains(key) ? world.doors.at(key).mode : 0;
    };
    assert(check(*outside) == 0);                                   // near, but the next room: nothing
    assert(check(*inside) == 1);                                    // its room: it rises
    std::printf("OK: only a player in the bridge's own room raises it\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
