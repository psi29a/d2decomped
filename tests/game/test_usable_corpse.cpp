// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A usable corpse (FUN_0063a770, 0x63a770): the corpse searches refuse one
// with a udead state, shatter (107) among them. The chill's end
// (LAB_0057ad80) takes shatter off only while the unit lives (0x57ada5),
// so a monster that dies chilled keeps it; the REAL Fight::monster_states.
// Needs the game data.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
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
    d2d::rules::Rng made_rng{ 7 };
    auto made_character = new_character(*data, 3, "Headless", false, true, made_rng);
    Character character;
    character.character_class = 3;
    character.name = "Headless";
    character.header = made_character.header;
    character.stats = made_character.stats;
    character.items = made_character.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    d2d::rules::Rng rng{ 3 };
    const auto fallen = data->monsters.by_id.find("fallen1");
    assert(fallen != data->monsters.by_id.end());
    world.fight.monsters.clear();
    // 0: alive, chilled until 500 with shatter rolled on; 1: died while
    // chilled (its chill ran out after); 2: a plain corpse.
    for (int k = 0; k < 3; ++k) {
        Monster monster = make_monster(*data, fallen->second, world.actor->player.x + 3.f * float(k + 1), world.actor->player.y, rng, 0);
        monster.chill_until = 500;
        monster.shatter = k < 2;
        if (k > 0) { monster.hit_points = 0; monster.mode = "DD"; }
        world.fight.add_monster(std::move(monster));
    }
    world.fight.monster_states(400);
    assert(world.fight.monsters[0].shatter);          // still chilled
    world.fight.monster_states(1000);
    assert(!world.fight.monsters[0].shatter);         // the chill ended alive
    assert(world.fight.monsters[1].shatter);          // a dead one keeps it
    assert(!world.fight.monsters[1].usable_corpse());
    assert(world.fight.monsters[2].usable_corpse());
    world.fight.monsters[2].corpse_used = true;
    assert(!world.fight.monsters[2].usable_corpse());
    std::printf("usable_corpse: ok\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_usable_corpse: %s\n", error.what());
    return 1;
}
