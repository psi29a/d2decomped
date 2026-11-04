// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A walk's re-plan (FUN_006503f0): a spot moved to another subtile is a new
// path at once, even one subtile (0.2 cells) off the path's end.

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
#include <vector>

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

    d2d::rules::Rng rng(7);
    constexpr int kBarbarian = 4;
    auto made = new_character(*data, kBarbarian, "Headless", false, true, rng);
    Character character;
    character.character_class = kBarbarian;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);

    World world(&*data, -1, -1);
    world.enter(character);
    std::uint32_t now = 0;
    const auto tick = [&](const std::vector<Order>& orders) { world.tick(orders, now + kTickMs, now); now += kTickMs; };
    for (int frame = 0; frame < 25; ++frame) tick({});
    auto& walker = *world.actor;
    const auto [free_x, free_y] = world.level->nearest_free(walker.player.x + 4, walker.player.y);
    const float goal_x = (std::floor(free_x * 5) + 0.5f) / 5, goal_y = (std::floor(free_y * 5) + 0.5f) / 5;   // a subtile's centre
    tick({ { walker.id, cmd::Move{ goal_x, goal_y, true } } });
    assert(walker.player.walking && !walker.player.path.empty() && walker.player.goal_x == goal_x);
    const float next_x = goal_x + 0.2f;                                        // the next subtile over (a drag)
    tick({ { walker.id, cmd::Move{ next_x, goal_y, false } } });
    assert(walker.player.goal_x == next_x && walker.player.goal_y == goal_y);
    std::printf("OK: a spot one subtile on re-plans the walk\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
