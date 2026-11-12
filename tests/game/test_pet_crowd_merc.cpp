// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A pet's crowd (a think's mode 4: FUN_00574de0 walks the owner's pet list
// with FUN_005e3900) counts the merc: FUN_00573270 binds a hireling with
// FUN_00575e90(merc, 7, ..), PetType row 7, and the walk takes every row
// from 1. Within 2 (FUN_005dc380, merc_gap) counts, further off or dead
// doesn't. The REAL Fight::pet_crowd. Needs the game data.

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

namespace fs = std::filesystem;
using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    if (!data) { std::printf("FAIL: load_game_data failed\n"); return 1; }
    if (!data->patched) { std::printf("SKIP: needs the 1.14d data (D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    d2d::rules::Rng rng(7);
    constexpr int kNecromancer = 2;
    auto made = new_character(*data, kNecromancer, "Headless", false, true, rng);
    Character character;
    character.character_class = kNecromancer;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    const Level* town = &data->town;
    world.level = town;
    world.fight.enter(town);
    auto& actor = *world.actor;
    const float at_x = actor.player.x - 5, at_y = actor.player.y;
    auto& pet = actor.pets.emplace_back();
    pet.monster = make_monster(*data, data->monsters.row("hydra1"), at_x, at_y, rng, 0);
    pet.where = town;
    assert(world.fight.pet_crowd(pet) == 0);                       // alone

    Monster merc;
    merc.hit_points = 10;
    merc.unit.x = at_x; merc.unit.y = at_y;
    actor.merc = merc;
    const int beside = world.fight.pet_crowd(pet);
    actor.merc->unit.x = at_x + 3;                                  // 15 subtiles off
    const int away = world.fight.pet_crowd(pet);
    actor.merc->unit.x = at_x;
    actor.merc->hit_points = 0;
    const int dead = world.fight.pet_crowd(pet);
    std::printf("crowd with the merc beside: %d, 3 cells off: %d, dead beside: %d\n", beside, away, dead);
    assert(beside == 1 && away == 0 && dead == 0);
    std::puts("OK: the merc (PetType 7) crowds a pet");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_pet_crowd_merc: %s\n", error.what());
    return 1;
}
