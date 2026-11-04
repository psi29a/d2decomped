// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Dragon Flight's to-hit (FUN_005d7850): its own (FUN_006449f0) plus stat
// 0x145, the charges' progressive_tohit (aurastat 325): Fight's
// progressive_tohit over Tiger Strike's charges. Needs the game data.

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
    const auto& tiger = *data->skills.get(data->skills.by_name.at("Tiger Strike"));
    world.fight.skill_level = [](int) { return 1; };
    assert(world.fight.progressive_tohit() == 0);
    world.fight.charge(tiger, 0);
    const int one = world.fight.progressive_tohit();
    world.fight.charge(tiger, 0);
    assert(one > 0 && world.fight.progressive_tohit() == 2 * one);   // a charge's share each
    return 0;
} catch (const std::exception& error) {
    std::printf("test_flight_tohit: %s\n", error.what());
    return 1;
}
