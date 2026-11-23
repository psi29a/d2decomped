// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Fire Wall and Blaze through the REAL Fight on an open level. Fire Wall (do 24, FUN_005c9ea0): a flame on the point, then
// its two makers (missile do 6, FUN_005ae680) fly out both ways square to the caster's line and lay a flame at their
// subtile only on a frame they entered a new one (FUN_006505c0: path +0x34 & 8), so no two flames share a subtile.
// Blaze (FUN_005c9d90): a flame at the player's subtile on each new subtile walked into, none while standing in one.

#include <ai/units.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <rules.hpp>
#include <spawning.hpp>
#include <world.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <set>
#include <utility>
#include <vector>

using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data) { std::printf("FAIL: load_game_data failed\n"); return 1; }
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
    Level open = *data->level(d2d::rules::level_ids::kBloodMoor);
    std::ranges::fill(open.walk, std::uint16_t(0));
    world.level = &open;
    world.fight.enter(&open);
    int at_x = -1, at_y = -1;
    for (int y = 20; y < 400 && at_x < 0; ++y)
        for (int x = 20; x < 400 && at_x < 0; ++x)
            if (std::ranges::all_of(std::array<std::pair<int, int>, 5>{ { { 0, 0 }, { 8, 0 }, { -8, 0 }, { 0, 8 }, { 0, -8 } } },
                                    [&](auto off) { return !missile_rooms(*data, open, float(x + off.first) + 0.5f, float(y + off.second) + 0.5f).empty(); }))
                at_x = x * 5 + 2, at_y = y * 5 + 2;
    assert(at_x >= 0);
    world.actor->player.x = (float(at_x) + 0.5f) / 5;
    world.actor->player.y = (float(at_y) + 0.5f) / 5;
    const Crowd crowd;
    std::uint32_t now = 1000 * kTickMs;
    const auto flames_of = [&](const GameData::MissileInfo& row) {
        std::vector<std::pair<int, int>> spots;
        for (const auto* list : { &world.fight.missiles, &world.fight.pending })
            for (const auto& missile : *list)
                if (missile.info == &row) spots.emplace_back(int(std::floor(missile.x * 5)), int(std::floor(missile.y * 5)));
        return spots;
    };

    // Fire Wall, two cells east of the caster: the wall runs north-south through the point.
    const auto& wall = *data->skills.get(data->skills.by_name.at("Fire Wall"));
    const auto& wall_fire = data->missiles.at("firewall");
    const int point_x = at_x + 10, point_y = at_y;
    world.fight.fire(wall, now, Fight::Caster{ .x = world.actor->player.x, .y = world.actor->player.y, .level = 1,
                                               .to_x = (float(point_x) + 0.5f) / 5, .to_y = (float(point_y) + 0.5f) / 5 });
    for (int frame = 0; frame < 20; ++frame) {
        now += kTickMs;
        world.fight.world(true, now, 0.04f, crowd);
    }
    const auto wall_spots = flames_of(wall_fire);
    const std::set<std::pair<int, int>> wall_set(wall_spots.begin(), wall_spots.end());
    int north = 0, south = 0;
    for (const auto& [sub_x, sub_y] : wall_spots) {
        assert(sub_x == point_x);   // square to the caster's line
        north += sub_y < point_y; south += sub_y > point_y;
    }
    std::printf("Fire Wall: %zu flames on %zu subtiles, %d north and %d south of the point\n", wall_spots.size(), wall_set.size(), north, south);
    assert(wall_set.contains({ point_x, point_y }));   // srvmissileb on the point
    assert(wall_set.size() == wall_spots.size());       // one flame a subtile
    assert(north > 0 && north == south);                 // growing both ways from the point

    // Blaze: walk east 0.3 subtile a frame for 30 frames, then stand 10 frames.
    world.fight.missiles.clear();
    const auto& blaze = *data->skills.get(data->skills.by_name.at("Blaze"));
    const auto& blaze_fire = data->missiles.at("blaze");
    world.fight.start_state(blaze, 1, now);
    world.actor->player.walking = true;
    // The subtiles a walking step entered: the one the player already stood on isn't one (the Fire Wall above ran there).
    const auto subtile = [&] { return std::pair{ int(std::floor(world.actor->player.x * 5)), int(std::floor(world.actor->player.y * 5)) }; };
    std::set<std::pair<int, int>> entered;
    for (int frame = 0; frame < 40; ++frame) {
        if (frame == 30) world.actor->player.walking = false;
        if (world.actor->player.walking) {
            const auto before = subtile();
            world.actor->player.x += 0.3f / 5;
            if (subtile() != before) entered.insert(subtile());
        }
        now += kTickMs;
        world.fight.world(true, now, 0.04f, crowd);
    }
    const auto blaze_spots = flames_of(blaze_fire);
    const std::set<std::pair<int, int>> blaze_set(blaze_spots.begin(), blaze_spots.end());
    std::printf("Blaze: %zu flames on %zu subtiles, %zu entered\n", blaze_spots.size(), blaze_set.size(), entered.size());
    assert(blaze_set.size() == blaze_spots.size());   // one flame a subtile
    assert(!entered.empty() && blaze_set == entered);  // on each subtile entered, none off the path or while standing
    std::puts("OK: Fire Wall's makers and Blaze lay a flame a new subtile (FUN_005ae680, FUN_005c9d90)");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_fire_trails: %s\n", error.what());
    return 1;
}
