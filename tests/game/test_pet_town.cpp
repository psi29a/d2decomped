// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// A pet in town runs its own think, as out of it: the AI driver
// (FUN_005b1740) tests no town. A Hydra (MonAI 86, FUN_005e9e60: no target,
// stand 10) stays where it is with its owner 20 cells off; d2d's old town
// rule walked every pet after the player. The REAL Fight::world. Needs the
// game data.

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
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>

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
    const Level* town = &data->town;                              // the Rogue Encampment
    assert(town->id == d2d::rules::level_ids::kRogueEncampment);
    d2d::rules::Rng rng(7);
    constexpr int kSorceress = 1;
    auto made = new_character(*data, kSorceress, "Headless", false, true, rng);
    Character character;
    character.character_class = kSorceress;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    world.level = town;
    world.fight.enter(town);
    const float at_x = world.actor->player.x - 20, at_y = world.actor->player.y;
    const std::uint32_t start = 1000 * kTickMs;
    auto& pet = world.actor->pets.emplace_back();
    pet.monster = make_monster(*data, data->monsters.row("hydra1"), at_x, at_y, rng, 0);
    pet.where = town;
    pet.skill = data->skills.by_name.at("Hydra");
    pet.until = start + 250 * kTickMs;
    pet.ctrl[0] = int(pet.until / kTickMs);                       // its last frame (FUN_005e9e60's +0x14)
    set_mode(*data, pet.monster, "NU", start);
    const Fine placed_x = pet.monster.unit.x, placed_y = pet.monster.unit.y;   // at_x / at_y in 16.16
    const Crowd crowd;
    for (std::uint32_t now = start + kTickMs; now <= start + 40 * kTickMs; now += kTickMs) world.fight.world(false, now, 0.04f, crowd);
    std::printf("Hydra in town: at %.2f, %.2f (was %.2f, %.2f), mode %s\n", double(pet.monster.unit.x), double(pet.monster.unit.y), double(at_x), double(at_y),
                std::string(pet.monster.mode).c_str());
    assert(pet.monster.alive() && pet.monster.unit.x.raw == placed_x.raw && pet.monster.unit.y.raw == placed_y.raw);
    std::puts("OK: a pet in town runs its own think (the Hydra stands)");
    return 0;
} catch (const std::exception& error) {
    std::printf("test_pet_town: %s\n", error.what());
    return 1;
}
