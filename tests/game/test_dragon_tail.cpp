// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Dragon Tail's fire (FUN_005d7180): the kick's physical x (calc1 + stat
// 0x149, fire mastery) / 100, through the REAL Fight::dragon_tail on a
// Fallen (no fire resistance in normal). Needs the game data.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <world.hpp>

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
    d2d::rules::Rng rng{ 3 };
    const auto fallen = data->monsters.by_id.find("fallen1");
    assert(fallen != data->monsters.by_id.end());
    Monster made = make_monster(*data, fallen->second, 10, 10, rng, 0);
    made.stats.hit_points = 5000; made.hit_points = 5000;
    d2d::rules::Rng made_rng{ 7 };
    auto made_character = new_character(*data, 6, "Headless", false, true, made_rng);
    Character character;
    character.character_class = 6;
    character.name = "Headless";
    character.header = made_character.header;
    character.stats = made_character.stats;
    character.items = made_character.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    const auto row = data->skills.by_name.find("Dragon Tail");
    assert(row != data->skills.by_name.end());
    const auto& skill = *data->skills.get(row->second);
    world.fight.skill_level = [](int) { return 1; };
    const auto lost = [&](int mastery) {
        world.actor->psum[d2d::d2s::kPassiveFireMastery] = mastery;
        world.fight.monsters.clear();
        made.unit.x = world.actor->player.x + 0.4f; made.unit.y = world.actor->player.y;
        world.fight.add_monster(Monster(made));
        world.fight.dragon_tail(skill, 0, 1000, 1000);
        return 5000 - world.fight.monsters[0].hit_points;
    };
    const int plain = lost(0), mastered = lost(50);
    assert(plain > 0);                             // calc1's own share at level 1
    assert(mastered - plain == 1000 * 50 / 100);   // the mastery's points join calc1's
    return 0;
}
