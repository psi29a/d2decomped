// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Through the real World tick: a ground click whose path ends short of the spot
// (the toward pather drops its last point when the last step turned,
// FUN_00679c80) ends the walk there: a click on the ground queues nothing, so
// FUN_00650840's stop (LAB_006508ff) is the end, with no new path from it
// (collision.md "A walk to a spot just ends"). Needs the data; SKIPs without it.

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

    // Open ground but a wall 10 subtiles east, from the top down to 20 rows below the player: the
    // path to a spot south-east slides down the wall, rounds its foot and ends a subtile short.
    const int width = town.ds1.width() * 5, height = town.ds1.height() * 5;
    const int at_x = int(std::floor(actor->player.x * 5)), at_y = int(std::floor(actor->player.y * 5));
    assert(at_x > 10 && at_y > 10 && at_x + 40 < width && at_y + 40 < height);
    town.walk.assign(std::size_t(width) * std::size_t(height), std::uint16_t(0));
    for (int row = 0; row <= at_y + 20; ++row) town.walk[std::size_t(row) * std::size_t(width) + std::size_t(at_x + 10)] = 1;
    std::uint32_t now = 1000 * kTickMs;
    world.apply(cmd::Move{ (float(at_x + 35) + 0.5f) / 5, (float(at_y + 35) + 0.5f) / 5, true }, now);
    int paths = 0;
    bool had_path = false;
    for (int tick = 0; tick < 2000; ++tick) {
        world.tick({}, now + kTickMs, now);
        now += kTickMs;
        if (!actor->player.walking) break;
        // One path only: its points only ever come off.
        if (!had_path || actor->player.path.size() > std::size_t(paths)) { assert(!had_path); had_path = true; }
        paths = int(actor->player.path.size());
    }
    assert(had_path && !actor->player.walking && actor->player.path.empty());
    assert(on_centre(actor->player.x) && on_centre(actor->player.y));
    const int end_x = int(std::floor(actor->player.x * 5)), end_y = int(std::floor(actor->player.y * 5));
    assert(end_x > at_x + 10);                                   // round the wall
    assert(end_x != at_x + 35 || end_y != at_y + 35);            // and short of the spot: a second path would close it
    const float stop_x = actor->player.x, stop_y = actor->player.y;
    for (int tick = 0; tick < 10; ++tick) {
        world.tick({}, now + kTickMs, now);
        now += kTickMs;
    }
    assert(!actor->player.walking && actor->player.x == stop_x && actor->player.y == stop_y);
    std::printf("ok: the walk to a spot ended where its path did\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_walk_spot_ends: %s\n", error.what());
    return 1;
}
