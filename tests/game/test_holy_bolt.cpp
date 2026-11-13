// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Holy Bolt's hit (FUN_005a9fb0): sHitPar1 and TargetPet heal a pet of the
// caster by calc1..calc2 in 256ths (FUN_00646ca0, FUN_0045c3e0). A monster
// that is not that pet is left to the undead / demon filter. Needs the
// 1.14d tables.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <utility>

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
    if (!data->patched) { std::printf("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }

    const auto holy = data->skills.by_name.find("Holy Bolt");
    const auto fist = data->skills.by_name.find("Fist of the Heavens");
    const auto bolt = data->missiles.find("holybolt");
    const auto fallen_row = data->monsters.by_id.find("fallen1");
    assert(holy != data->skills.by_name.end() && fist != data->skills.by_name.end());
    assert(bolt != data->missiles.end() && fallen_row != data->monsters.by_id.end());
    const auto* holy_skill = data->skills.get(holy->second);
    const auto* fist_skill = data->skills.get(fist->second);
    assert(holy_skill && holy_skill->target_pet && holy_skill->target_ally);
    assert(fist_skill && !fist_skill->target_pet && !fist_skill->target_ally);
    assert(bolt->second.hit_func == 7 && bolt->second.hit_par1 != 0 && bolt->second.hit_par2 == 1);
    assert(!data->monsters.types[std::size_t(fallen_row->second)].undead);

    d2d::rules::Rng made_rng{ 7 };
    auto made = new_character(*data, 4, "Headless", false, true, made_rng);
    Character character;
    character.character_class = 4;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    world.fight.skill_base = world.fight.skill_level = [](int) { return 0; };

    d2d::rules::Rng make_rng{ 3 };
    Monster fallen = make_monster(*data, int(fallen_row->second), 10.f, 10.f, make_rng, 0);
    fallen.id = 42;
    fallen.hit_points = 10;
    fallen.stats.hit_points = 100;
    fallen.life_frac = fallen.max_life_frac = 0;
    Fight::Pet pet;
    pet.monster = fallen;
    world.actor->pets.push_back(pet);
    world.fight.monsters.push_back(std::move(fallen));

    Missile missile;
    missile.info = &bolt->second;
    missile.skill = holy->second;
    missile.level = 1;
    missile.friendly = true;
    const int low_points = d2d::rules::eval_calc(data->skills, holy_skill->calc[0], world.fight.calc_env(), holy_skill->id, 1);
    const int high_points = d2d::rules::eval_calc(data->skills, holy_skill->calc[1], world.fight.calc_env(), holy_skill->id, 1);
    assert(low_points == 1 && high_points == 6);
    world.rng = d2d::rules::Rng{ 99 };
    d2d::rules::Rng expect = world.rng;
    const int span = (high_points << 8) - (low_points << 8);
    const int amount = int(expect(span)) + (low_points << 8);
    const int before = world.fight.monsters.back().life_fine();
    assert(world.fight.skill_missile_hits(missile, 0, 1000));
    assert(world.fight.monsters.back().life_fine() == std::min(before + amount, world.fight.monsters.back().max_life_fine()));
    assert(world.actor->pets.back().monster.life_fine() == world.fight.monsters.back().life_fine());

    world.actor->pets.clear();
    world.fight.monsters.back().hit_points = 10;
    world.fight.monsters.back().life_frac = 0;
    const int untouched = world.fight.monsters.back().life_fine();
    assert(!world.fight.skill_missile_hits(missile, 0, 1000));
    assert(world.fight.monsters.back().life_fine() == untouched);

    world.actor->pets.push_back(std::move(pet));
    missile.skill = fist->second;
    assert(!world.fight.skill_missile_hits(missile, 0, 1000));
    assert(world.fight.monsters.back().life_fine() == untouched);
    std::printf("OK: Holy Bolt heals the caster's pet by calc1..calc2\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "holy bolt: %s\n", error.what());
    return 1;
}
