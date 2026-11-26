// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Larzuk's marker (InitFn 71, FUN_00587840) whose free spot (FUN_00545340) lies past
// its level's edge, in a neighbour level's room: FUN_00463740 hands that room, and
// FUN_005b2f20 makes him there (FUN_00555230 -> FUN_00554850: his room and act are
// that room's). d2d: room_objects' marker_make_away puts him in the neighbour Level's
// npcs, in its subtiles, its collision across its east edge as it stands then (level 1's
// room up: not blocked). Two made-up levels side by side; no game files.
#include <ds1.hpp>
#include <gamedata.hpp>
#include <level.hpp>
#include <object_ids.hpp>
#include <spawning.hpp>
#include <test_data.hpp>
#include <txt.hpp>
#include <units.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <span>
#include <string>

using namespace d2d::game;

namespace {

constexpr int kLarzukRow = 5;

// A made-up excel table from tab-separated text.
d2d::txt::Table table(const std::string& text) {
    return d2d::txt::Table(std::as_bytes(std::span(text.data(), text.size())));
}

// An 8 x 8 tile level with one room filling it, at act tile (world_x, 0), walk grid `walk` everywhere.
Level level_of(int id, int world_x, std::uint16_t walk) {
    Level level{};
    level.id = id;
    level.world_x = world_x;
    level.ds1 = d2d::ds1::Map(8, 8, 0, 0);
    level.rooms.resize(1);
    level.rooms[0].x = 0; level.rooms[0].y = 0; level.rooms[0].width = 8; level.rooms[0].height = 8;
    level.room_flags.assign(1, 0);
    level.walk.assign(40 * 40, walk);
    return level;
}

}  // namespace

// Larzuk's marker by a neighbour level's edge, both levels GameData's (FUN_00587840, FUN_005b2f20).
int main() try {
    d2d::test::TestData test_data;
    auto& data = test_data.data;
    test_data.monster(kLarzukRow, "larzuk");
    data.mon_bin.assign(0x200, 0);
    data.mon_bin[0x1ff] = kLarzukRow;
    data.mon_is_npc.assign(kLarzukRow + 1, false);
    data.mon_is_npc[kLarzukRow] = true;
    data.mon_npc[kLarzukRow].code = "LZ";
    auto builder = std::make_shared<GameData::LevelBuilder>();
    builder->objects = table("Id\tInitFn\n543\t71\n");
    builder->obj_row["543"] = 0;
    builder->levels = table("Id\tThemes\tWarpDist\tPosition\n1\t0\t2025\t0\n2\t0\t2025\t0\n");
    data.builder = builder;

    // Level 1 (act tiles 100..107) walled everywhere; level 2 (92..99) west of it, its free area
    // (walk) only its five east columns, its collision (tile_walk) open only in the last two
    // (subtiles 38, 39): Larzuk's plus (size 2) fits only at 39, its east arm on level 1's
    // subtile 0, free there once level 1's room is up.
    // GameData owns them, so the near list finds each by its id (FUN_0066c220).
    data.levels[1] = std::make_unique<Level>(level_of(1, 100, 0x100));
    data.levels[2] = std::make_unique<Level>(level_of(2, 92, 0x100));
    Level& here = *data.levels[1];
    Level& west = *data.levels[2];
    west.tile_walk.assign(40 * 40, 0x01);
    for (int row = 0; row < 40; ++row)
        for (int column = 35; column < 40; ++column) {
            west.walk[std::size_t(row * 40 + column)] = 0;
            if (column >= 38) west.tile_walk[std::size_t(row * 40 + column)] = 0;
        }
    here.vis[0] = 2; here.vis_warp[0] = -1;
    west.vis[0] = 1; west.vis_warp[0] = -1;
    // Each room holds its walk slot 0 (FUN_00667970: a preset room's), which the near list walks (FUN_0066c370).
    here.room_flags[0] = 0x10;
    west.room_flags[0] = 0x10;
    here.nearby.push_back({ &west, -8, 0 });
    west.nearby.push_back({ &here, 8, 0 });
    d2d::drlg::Unit marker{};
    marker.type = d2d::rules::unit_type::kObject;
    marker.id = 543;
    marker.x = 1; marker.y = 20;                                   // a subtile in from level 1's west edge
    here.units.push_back(marker);
    here.unit_rooms.push_back(0);

    Spawning spawning;
    spawning.regions.resize(3);
    for (Level* level : { &west, &here }) {
        auto& state = spawning.levels[level];
        state.order = { 0 };
        state.rooms_up = { true };
    }
    (void)room_objects(data, spawning, west, 0);                  // the neighbour's room up first: its ObjectRooms
    const auto before = west.npcs.size();
    (void)room_objects(data, spawning, here, 0);
    std::size_t larzuks = 0;
    for (const auto& npc : here.npcs) larzuks += npc.code == "LZ";
    assert(larzuks == 0);                                          // not the marker's level's
    assert(west.npcs.size() == before + 1 && west.npcs.back().code == "LZ");
    const auto& larzuk = west.npcs.back();
    // In level 2's own cells: inside it, east of level 1's west edge less 8 tiles.
    std::printf("Larzuk at (%.1f, %.1f) of level 2\n", double(larzuk.x), double(larzuk.y));
    assert(west.inside(larzuk.x, larzuk.y) && larzuk.x > 0 && larzuk.x < 8);
    assert(int(larzuk.x * 5) == 39);                               // the plus on the edge: level 1's subtile counted free
    assert(spawning.marker_records.larzuk_made);
    std::puts("OK: a marker's NPC made in a neighbour level's room is that level's (FUN_00463740, FUN_005b2f20)");
    return 0;
} catch (const std::exception& error) {
    std::printf("FAIL: %s\n", error.what());
    return 1;
}
