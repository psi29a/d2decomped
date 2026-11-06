// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The Fire Golem's MonUMod 31 goboom (FUN_005b1cf0 case BaseId 0x124:
// FUN_005a4850(0x1f, 0)): 4 frames after it dies, FUN_005a2840 (0x5a2840)
// hits each enemy within 6 subtiles, the line clear of 0x805, with 100
// physical and 100 fire (0x6400 each) through its resistances; the REAL
// Fight::pets_turn. Needs the game data.

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
    const int golem_row = data->monsters.row("firegolem"), fallen_row = data->monsters.row("fallen1");
    assert(golem_row >= 0 && fallen_row >= 0);
    for (const auto& room : moor->rooms) {
        const float at_x = float(room.x) + float(room.width) / 2, at_y = float(room.y) + float(room.height) / 2;
        bool open = true;
        for (int step = 0; step <= 10 && open; ++step) open = !moor->blocked(at_x + float(step) / 5, at_y, 0x805);
        if (!open) continue;
        d2d::rules::Rng rng{ 3 };
        fight.monsters.clear();
        for (const float off : { 1.f, 2.f }) {                     // 5 subtiles (in reach), 10 (not)
            Monster fallen = make_monster(*data, fallen_row, at_x + off, at_y, rng, 0);
            fallen.align = 0; fallen.hit_points = fallen.last_hp = fallen.stats.hit_points = 1000;
            fight.monsters.push_back(std::move(fallen));
        }
        const int near_resist = fight.target_of(0).res[0], near_fire = fight.target_of(0).res[2];
        const int want = (d2d::rules::resisted_fine(0x6400, near_resist) >> 8) + (d2d::rules::resisted_fine(0x6400, near_fire) >> 8);
        auto& pets = world.actor->pets;
        pets.clear();
        auto& pet = pets.emplace_back();
        pet.monster = make_monster(*data, golem_row, at_x, at_y, rng, 0);
        pet.where = moor;
        pet.monster.hit_points = 0;
        set_mode(*data, pet.monster, "DT", 1000);
        assert(fight.blast_due(pet.monster));
        assert(!fight.golem_blast(pet, 1000 + 3 * 40));          // 3 frames: not yet
        assert(fight.monsters[0].hit_points == 1000);
        assert(fight.golem_blast(pet, 1000 + 4 * 40));
        assert(!fight.blast_due(pet.monster) && !fight.golem_blast(pet, 1000 + 5 * 40));   // once
        const int lost = 1000 - fight.monsters[0].hit_points;
        assert(lost >= want && lost <= want + 1 && want > 0);
        assert(fight.monsters[1].hit_points == 1000);             // 10 subtiles off
        std::printf("golem blast: %d life off the one in reach\n", lost);
        return 0;
    }
    std::printf("SKIP: no open room in the Blood Moor\n");
    return D2D_TEST_SKIPPED;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_golem_blast: %s\n", error.what());
    return 1;
}
