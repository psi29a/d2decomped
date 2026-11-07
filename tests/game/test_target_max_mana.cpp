// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The Succubus's Skill3 (FUN_005e1e00, 0x5e1e00; Baal's act 8,
// FUN_005fcb60, the same test) goes at a player only while its max mana
// (FUN_00625d60, stat 9) is under its max life (FUN_00625d10, 0x625d10,
// stat 7): the player's foe carries both, in 256ths. Needs the game data.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <monsters.hpp>
#include <rules.hpp>
#include <think_scratch.hpp>
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
    auto made_character = new_character(*data, 0, "Headless", false, true, made_rng);   // an Amazon: 50 life, 15 mana
    Character character;
    character.character_class = 0;
    character.name = "Headless";
    character.header = made_character.header;
    character.stats = made_character.stats;
    character.items = made_character.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    const auto succubus = [&] {
        const Foe foe = world.fight.player_foe(0);
        assert(foe.max_life == int(world.actor->character.stats.values[d2d::d2s::kMaxLife]) && foe.max_life > 0);
        assert(foe.max_mana == int(world.actor->character.stats.values[d2d::d2s::kMaxMana]));
        int state = 0;
        d2d::rules::ThinkIn input{ .aip = { 0, 0, 100, 10, 0, 0, 0, 100 }, .dist = 5, .state = &state };
        input.skill[2] = true; input.skill45[0] = true;
        ThinkScratch scratch;
        scratch.point(input);
        input.target_player = true; input.target_life_pct = 100; input.self_life_pct = 100;
        input.target_max_life = foe.max_life; input.target_max_mana = foe.max_mana;
        d2d::rules::Rng roll{ 5 };
        return d2d::rules::mon_think("Succubus", input, roll, [](int, bool) { return false; });
    };
    const auto low_mana = succubus();
    assert(low_mana.act == d2d::rules::MonAct::skill && low_mana.n == 2);      // Skill3 (Blood Mana's twin)
    world.actor->character.stats.values[d2d::d2s::kMaxMana] = world.actor->character.stats.values[d2d::d2s::kMaxLife];
    const auto high_mana = succubus();
    assert(high_mana.act == d2d::rules::MonAct::skill && high_mana.n == 3);    // at least as much mana: Skill4
    std::printf("target max mana: ok\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_target_max_mana: %s\n", error.what());
    return 1;
}
