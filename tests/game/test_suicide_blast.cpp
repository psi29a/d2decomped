// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// MonUMod 33 suicideminion: FUN_005a3e70 puts it from GH into death, its
// event 7 four frames on; FUN_005a3ef0 rolls its A1 damage on its seed as
// physical and, by aip1 (15 on every row: cold, for 15 frames), cold, on
// everyone within aip4 subtiles, and makes suicideiceexplode (428) on its
// spot (0x5a4081). The REAL Fight::boss_events. Needs the game data.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <array>
#include <cassert>
#include <cmath>
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
    const int minion_row = data->monsters.row("suicideminion1");
    assert(minion_row >= 0);
    const auto& aip = data->monsters.types[std::size_t(minion_row)].diff[0].aip;
    assert(aip[0] == 15 && aip[3] == 4);
    // Every Foe on a subtile centre: aip4 (4) subtiles off is in reach, 5 isn't.
    const float at_x = 100, at_y = 100;
    const auto centre = [&](float cell, int subtiles) { return (std::floor(cell * 5) + float(subtiles) + 0.5f) / 5; };
    d2d::rules::Rng rng{ 3 };
    fight.monsters.clear();
    Monster minion = make_monster(*data, minion_row, centre(at_x, 0), centre(at_y, 0), rng, 0);
    minion.align = 0;
    minion.hit_points = minion.last_hp = 50;
    set_mode(*data, minion, "GH", 1000);
    auto seed = minion.seed;
    const int low = minion.stats.a1_min, high = minion.stats.a1_max;
    const int pts = high > low ? low + seed(high - low) : low;
    fight.monsters.push_back(std::move(minion));
    std::array<Foe, 2> foes;
    for (std::size_t k = 0; k < foes.size(); ++k) { foes[k].x = centre(at_x, 4 + int(k)); foes[k].y = centre(at_y, 0); }
    fight.boss_events(foes, 1000);                                 // GH: straight into death
    assert(!fight.monsters[0].alive() && fight.monsters[0].mode == "DT");
    fight.boss_events(foes, 1000 + 3 * 40);
    assert(foes[0].damage == 0 && fight.pending.empty());          // 3 frames: not yet
    fight.boss_events(foes, 1000 + 4 * 40);
    assert(pts > 0 && foes[0].damage == pts + pts);                // physical, then cold (no resistances)
    assert(foes[0].chill_ticks == 15);                             // aip1 frames
    assert(foes[1].damage == 0);                                   // 5 subtiles off
    assert(fight.pending.size() == 1 && fight.pending.back().info->name == "suicideiceexplode");
    fight.boss_events(foes, 1000 + 5 * 40);
    assert(fight.pending.size() == 1);                             // once
    std::printf("suicide blast: %d physical + %d cold, suicideiceexplode\n", pts, pts);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_suicide_blast: %s\n", error.what());
    return 1;
}
