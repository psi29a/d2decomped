// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A trap's sumumod 32 firespike_explode (Wake of Fire Sentry, FUN_005d5e10
// -> FUN_005c4470 at 0x5c4934): 4 frames after it dies, FUN_005a3d20 hits
// each enemy within its level x aip3 subtiles with (El1MinD + rand(El1MaxD -
// El1MinD) on its seed) << 8 fire, nothing physical and no missile; the REAL
// Fight::summon and Fight::golem_blast. Needs the game data.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <combat.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <level_ids.hpp>
#include <rules.hpp>
#include <uniques.hpp>
#include <world.hpp>

#include <algorithm>
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
    const auto* wake = data->skills.get(262);
    const int fallen_row = data->monsters.row("fallen1");
    assert(wake && wake->name == "Wake of Fire Sentry" && wake->sumumod == 32 && fallen_row >= 0);
    for (const auto& room : moor->rooms) {
        const float at_x = float(room.x) + float(room.width) / 2, at_y = float(room.y) + float(room.height) / 2;
        auto& pets = world.actor->pets;
        pets.clear();
        fight.summon(*wake, 1, at_x, at_y, 1000);
        if (pets.empty()) continue;
        auto& pet = pets.back();
        assert(std::ranges::contains(pet.monster.mods, d2d::rules::umod::firespike));
        const auto& type_info = data->monsters.types[std::size_t(pet.monster.type)];
        const int radius = pet.monster.stats.level * type_info.diff[std::size_t(pet.monster.difficulty)].aip[2];
        assert(radius > 0);
        bool open = true;
        for (int step = 0; step <= radius + 1 && open; ++step) open = !moor->blocked(at_x + float(step) / 5, at_y, 0x805);
        if (!open) continue;
        // Everyone on subtile centres: `radius` subtiles off is in reach, one more isn't.
        const auto centre = [&](int subtiles) { return (std::floor(at_x * 5) + float(subtiles) + 0.5f) / 5; };
        d2d::rules::Rng rng{ 3 };
        fight.monsters.clear();
        for (const int off : { radius, radius + 1 }) {
            Monster fallen = make_monster(*data, fallen_row, centre(off), at_y, rng, 0);
            fallen.align = 0; fallen.hit_points = fallen.last_hp = fallen.stats.hit_points = 1000;
            fight.monsters.push_back(std::move(fallen));
        }
        pet.monster.unit.x = centre(0); pet.monster.unit.y = at_y;
        pet.where = moor;
        pet.monster.hit_points = 0;
        set_mode(*data, pet.monster, "DT", 2000);
        auto seed = pet.monster.seed;
        const auto& normal = type_info.diff[0].elements[0];
        const int fire = normal.min + (normal.max > normal.min ? seed(normal.max - normal.min) : 0);
        const int want = d2d::rules::resisted_fine(fire << 8, fight.target_of(0).res[2]) >> 8;
        fight.pending.clear();
        assert(fight.blast_due(pet.monster));
        assert(!fight.golem_blast(pet, 2000 + 3 * 40));          // 3 frames: not yet
        assert(fight.monsters[0].hit_points == 1000);
        assert(fight.golem_blast(pet, 2000 + 4 * 40));
        assert(!fight.blast_due(pet.monster) && !fight.golem_blast(pet, 2000 + 5 * 40));   // once
        const int lost = 1000 - fight.monsters[0].hit_points;
        assert(fire >= 5 && fire < 10 && lost >= want && lost <= want + 1);
        assert(fight.monsters[1].hit_points == 1000);             // one subtile further
        assert(fight.pending.empty());                            // no missile
        std::printf("trap blast: %d fire within %d subtiles, %d life off\n", fire, radius, lost);
        return 0;
    }
    std::printf("SKIP: no open room in the Blood Moor\n");
    return D2D_TEST_SKIPPED;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_trap_blast: %s\n", error.what());
    return 1;
}
