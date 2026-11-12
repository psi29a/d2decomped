// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Through the real World tick: a walk to an item out of reach when it ends,
// blocked or not, is issued once more (FUN_00580c20 -> FUN_00548b00,
// LAB_00580e73) and the second's end is plain idle (pending cleared,
// LAB_00580e88): two walks, then the pick-up is dropped (collision.md "The
// server's pending interact"). Needs the data; SKIPs without it.

#include <character.hpp>
#include <character_store.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <protocol.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <tuple>
#include <utility>

using namespace d2d::game;
namespace fs = std::filesystem;

namespace {

bool on_centre(float value) {
    const float centre = (std::floor(value * 5) + 0.5f) / 5;
    return std::abs(value - centre) < 0.001f;
}

}  // namespace

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    if (!mpq_dir || !fs::exists(fs::path(mpq_dir) / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq (D2_MPQ_DIR)\n"); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(mpq_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && !data->town.walk.empty());
    if (!data->patched) { std::printf("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }

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
    auto* actor = world.actor;
    auto& town = data->town;
    assert(world.level == &town);

    // Walls all round a patch 7 subtiles wide, gold lying 30 subtiles east of it.
    const int width = town.ds1.width() * 5, height = town.ds1.height() * 5;
    const int at_x = int(std::floor(actor->player.x * 5)), at_y = int(std::floor(actor->player.y * 5));
    assert(at_x > 10 && at_y > 10 && at_x + 40 < width && at_y + 10 < height);
    town.walk.assign(std::size_t(width) * std::size_t(height), std::uint16_t(1));
    for (int row = -3; row <= 3; ++row)
        for (int column = -3; column <= 3; ++column)
            town.walk[std::size_t(at_y + row) * std::size_t(width) + std::size_t(at_x + column)] = 0;
    std::uint32_t now = 1000 * kTickMs;
    world.loot.put({ .code = "gld", .gold = 7 }, actor->player.x, actor->player.y, 1, world.fight.spawning.game, now);
    std::tie(world.loot.ground.back().x, world.loot.ground.back().y) = std::pair{ actor->player.x + 6, actor->player.y };
    const int gold = world.loot.ground.back().id;
    world.apply(cmd::Pickup{ gold }, now);
    assert(actor->pick_item == gold);
    int walks = 0;
    bool had_path = false;
    for (int tick = 0; tick < 300 && actor->pick_item >= 0; ++tick) {
        world.tick({}, now + kTickMs, now);
        now += kTickMs;
        const bool has_path = !actor->player.path.empty();
        if (has_path && !had_path) ++walks;
        had_path = has_path;
    }
    assert(actor->pick_item == -1 && !actor->player.walking);    // dropped: no third walk
    assert(walks >= 1 && walks <= 2);
    assert(world.loot.index_of(gold) >= 0);                      // still lying there
    assert(on_centre(actor->player.x) && on_centre(actor->player.y));
    const float stop_x = actor->player.x, stop_y = actor->player.y;
    for (int tick = 0; tick < 10; ++tick) {
        world.tick({}, now + kTickMs, now);
        now += kTickMs;
    }
    assert(!actor->player.walking && actor->player.x == stop_x && actor->player.y == stop_y);
    std::printf("ok: two walks to the item (%d paths), then idle\n", walks);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_walk_item_twice: %s\n", error.what());
    return 1;
}
