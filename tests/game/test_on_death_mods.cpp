// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// What a monster's death does by its MonUMod: 35 shatter_on_death (every
// frozenhorror1, FUN_005b21b0) puts state 107 on it as it dies
// (0x5a3a80), so it leaves no corpse to use. The REAL
// Fight::boss_events. Needs the game data.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <utility>

using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
    if (!data->patched) { std::printf("SKIP: 1.14d's tables: set D2_PATCH_INSTALLER\n"); return D2D_TEST_SKIPPED; }
    const auto* moor = data->level(d2d::rules::level_ids::kBloodMoor);
    if (!moor || moor->rooms.empty()) { std::printf("SKIP: no Blood Moor\n"); return D2D_TEST_SKIPPED; }
    d2d::rules::Rng made_rng{ 7 };
    auto made_character = new_character(*data, 2, "Headless", false, true, made_rng);
    Character character;
    character.character_class = 2;
    character.name = "Headless";
    character.header = made_character.header;
    character.stats = made_character.stats;
    character.items = made_character.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    world.level = moor;
    auto& fight = world.fight;
    const int horror_row = data->monsters.row("frozenhorror1"), fallen_row = data->monsters.row("fallen1");
    assert(horror_row >= 0 && fallen_row >= 0);
    d2d::rules::Rng rng{ 3 };
    fight.monsters.clear();
    for (const int row : { horror_row, fallen_row }) {
        Monster dying = make_monster(*data, row, 100, 100, rng, 0);
        dying.align = 0;
        dying.hit_points = dying.last_hp = 50;
        fight.monsters.push_back(std::move(dying));
    }
    fight.boss_events({}, 1000);                                   // alive: nothing
    assert(!fight.monsters[0].shatter);
    for (auto& dying : fight.monsters) { dying.hit_points = 0; set_mode(*data, dying, "DT", 1040); }
    fight.boss_events({}, 1040);
    assert(fight.monsters[0].shatter && !fight.monsters[1].shatter);
    std::printf("shatter_on_death: the Frozen Horror shatters, the Fallen doesn't\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_on_death_mods: %s\n", error.what());
    return 1;
}
