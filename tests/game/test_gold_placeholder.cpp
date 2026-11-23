// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The gold placeholder (object 269, InitFn 28 FUN_0054f8c0): mode 2 (ON)
// first, then its piles (FUN_00559300): gold at ilvl the area level less
// 1, on Tower 5's floor as its rooms come up. Needs the data.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level.hpp>
#include <level_ids.hpp>
#include <loot.hpp>
#include <rules.hpp>
#include <spawning.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

using namespace d2d::game;
namespace fs = std::filesystem;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    const auto* tower = data->level(d2d::rules::level_ids::kTowerCellarLevel5);
    if (!tower) { std::printf("SKIP: no Tower Cellar Level 5\n"); return D2D_TEST_SKIPPED; }
    if (!data->builder) { std::printf("SKIP: no level builder\n"); return D2D_TEST_SKIPPED; }
    Level one = *tower;
    one.npcs.clear();
    auto object_seed = d2d::rules::Rng(4242);
    add_object(*data, data->builder->objects, data->builder->obj_row, one, 269, 10, 10, object_seed);
    assert(one.npcs.size() == 1);
    assert(one.npcs[0].mode == "ON");                    // FUN_00624690(unit, 2)
    std::printf("OK: the gold placeholder starts ON\n");

    d2d::rules::Rng rng(7);
    constexpr int kAmazon = 0;
    auto made = new_character(*data, kAmazon, "Coins", false, true, rng);
    Character character;
    character.character_class = kAmazon;
    character.name = "Coins";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    populate_level(*data, world.fight.spawning, *tower);
    const auto piles = world.fight.spawning.levels[tower].gold.size();
    assert(piles > 0);
    world.drop_new_gold();
    assert(world.fight.spawning.levels[tower].gold.empty());
    const auto& floor = tower == world.loot.ground_level ? world.loot.ground : world.loot.kept[tower];
    assert(floor.size() == piles);
    const int area = area_monster_level(*data, tower->id, 0), ilvl = area > 1 ? area - 1 : area;
    assert(std::ranges::all_of(floor, [&](const Loot::GroundItem& pile) { return pile.item.code == "gld" && pile.gold >= ilvl && pile.gold < 6 * ilvl; }));
    std::printf("OK: %zu gold piles on Tower 5's floor\n", piles);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_gold_placeholder: %s\n", error.what());
    return 1;
}
