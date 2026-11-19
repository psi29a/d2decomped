// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Whirlwind's path (FUN_005d8f50): a type 7 path to the point with mask 0xc01
// (FUN_00648ce0: walls, no units), walked to its last point. On the camp's
// grid, a point the straight line can't reach through a wall gets a route
// round it, every point open.
// Needs the game data (D2_MPQ_DIR); SKIPs without it.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <optional>
#include <utility>

using namespace d2d::game;

int main() try {
    const char* data_dir = std::getenv("D2_MPQ_DIR");
    if (!data_dir) return D2D_TEST_SKIPPED;
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? std::filesystem::path(patch) : std::filesystem::path{}, 3397892852u);
    assert(data);
    if (!data->patched) { std::puts("SKIP: 1.14d's camp (set D2_PATCH_INSTALLER)"); return D2D_TEST_SKIPPED; }
    d2d::rules::Rng rng(7);
    auto made = new_character(*data, 4, "Headless", false, true, rng);
    Character character;
    character.character_class = 4;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    World world(&*data, -1, -1);
    world.enter(character);
    const auto* whirlwind = data->skills.get(data->skills.by_name.at("Whirlwind"));
    assert(whirlwind);
    auto& actor = *world.actor;
    const auto& level = *world.level;
    const auto open = [&](float x, float y) { auto shape = actor.player.shape; shape.mask = 0xc01; return !level.unit_blocked(x, y, shape); };
    // A free point 2..6 cells off whose straight line crosses a wall.
    std::optional<std::pair<float, float>> behind;
    const float from_x = actor.player.x, from_y = actor.player.y;
    for (int dy = -30; dy <= 30 && !behind; ++dy)
        for (int dx = -30; dx <= 30 && !behind; ++dx) {
            const float to_x = from_x + float(dx) / 5, to_y = from_y + float(dy) / 5, length = std::hypot(to_x - from_x, to_y - from_y);
            if (length < 2 || !open(to_x, to_y)) continue;
            bool walled = false;
            const auto centre = [](float cells) { return (std::floor(cells * 5) + 0.5f) / 5; };   // the route's subtile centres
            for (float part = 0; part <= 1 && !walled; part += 0.02f) walled = !open(centre(from_x + (to_x - from_x) * part), centre(from_y + (to_y - from_y) * part));
            if (walled) behind = std::pair{ to_x, to_y };
        }
    if (!behind) { std::puts("SKIP: no walled point near the start"); return D2D_TEST_SKIPPED; }
    actor.move_x = behind->first; actor.move_y = behind->second;
    world.fight.start_move(*whirlwind, 1000);
    const auto& route = actor.smove.route;
    assert(actor.smove.active && route.size() >= 2);
    for (const auto& [point_x, point_y] : route) assert(open(point_x, point_y));
    assert(actor.smove.target_x == route.back().first && actor.smove.target_y == route.back().second);
    std::printf("OK: Whirlwind goes round the wall in %zu points\n", route.size());

    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
}
