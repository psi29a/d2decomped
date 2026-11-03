// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Redemption (FUN_005d0e90): the area walker (FUN_0056b7e0) runs its
// callback (FUN_005d0d70) on every unit in reach, so every corpse there is
// redeemed at calc1 % (all of them at level 60's 100 %), not one a pulse;
// the REAL Fight::aura_pulse. Needs the game data.

#include <character.hpp>
#include <character_store.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

using namespace d2d::game;

int main() {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data);
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
    world.fight.skill_level = [](int) { return 60; };
    world.actor->aura = data->skills.by_name.at("Redemption");
    world.actor->aura_next = 0;
    world.fight.monsters.clear();
    for (int k = 0; k < 3; ++k) {
        Monster corpse = make_monster(*data, fallen->second, world.actor->player.x + 0.4f * float(k + 1), world.actor->player.y, rng, 0);
        corpse.hit_points = 0; corpse.mode = "DD";
        world.fight.add_monster(std::move(corpse));
    }
    world.fight.aura_pulse(1000);
    assert(std::ranges::all_of(world.fight.monsters, [](const Monster& monster) { return monster.corpse_used; }));
    return 0;
}
