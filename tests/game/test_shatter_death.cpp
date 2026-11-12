// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A shattered death (FUN_004aff60's command 8, FUN_004afc10): a Fallen
// dying under shatter (107) or a freeze (1) is hidden (corpse_used), its
// hit class's element becomes 0xa0 and it breaks into its MonStats2
// Height's icebreak row and icebreaksmoke; one dying without stays. The
// REAL Fight::boss_events. Needs the game data.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <rules.hpp>
#include <shatter.hpp>
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
    const int fallen_row = data->monsters.row("fallen1");
    assert(fallen_row >= 0);
    d2d::rules::Rng rng{ 3 };
    fight.monsters.clear();
    for (int i = 0; i < 3; ++i) {
        Monster dying = make_monster(*data, fallen_row, float(100 + i * 3), 100, rng, 0);
        dying.align = 0;
        dying.hit_points = dying.last_hp = 50;
        dying.hit_class = 0x32;                                    // a cold blade's blow
        fight.monsters.push_back(std::move(dying));
    }
    fight.monsters[0].shatter = true;
    fight.monsters[1].frozen_until = 5000;
    fight.boss_events({}, 1000);                                   // alive: nothing
    assert(fight.pending.empty() && !fight.monsters[0].corpse_used);
    for (auto& dying : fight.monsters) { dying.hit_points = 0; set_mode(*data, dying, "DT", 1040); }
    fight.boss_events({}, 1040);
    const auto rows = d2d::rules::shatter_rows(1, fight.monsters[0].npc.height, false, false);
    const auto first = d2d::rules::shatter_row_name(rows.first);
    assert(!first.empty());
    for (int i = 0; i < 2; ++i) {
        assert(fight.monsters[std::size_t(i)].corpse_used && fight.monsters[std::size_t(i)].hit_class == 0xa2);
        assert(!fight.monsters[std::size_t(i)].usable_corpse());
    }
    assert(!fight.monsters[2].corpse_used && fight.monsters[2].hit_class == 0x32);
    assert(fight.pending.size() == 4);
    for (std::size_t i = 0; i < 4; ++i) {
        assert(fight.pending[i].visual_only && fight.pending[i].info);
        assert(fight.pending[i].info->name == (i % 2 == 0 ? first : std::string_view("icebreaksmoke")));
    }
    fight.boss_events({}, 1080);                                   // once, as it goes into DT
    assert(fight.pending.size() == 4);
    std::printf("OK: a shattered Fallen breaks into %s and icebreaksmoke, hidden\n", std::string(first).c_str());
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_shatter_death: %s\n", error.what());
    return 1;
}
