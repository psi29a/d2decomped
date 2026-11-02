// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The char panel's states part (FUN_004a7d00): Fight::update_fighters puts
// the states' defense and resistances on the panel, and their States.txt
// blue / red (armblue, rpblue, the curse's reds) on it and the skill blocks.

#include <character.hpp>
#include <character_store.hpp>
#include <d2s_items.hpp>
#include <fight.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <rules.hpp>
#include <skills.hpp>
#include <world.hpp>

#include <cassert>
#include <cstdint>
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
    assert(data && data->town.id == 1);
    if (!data->patched) { std::printf("SKIP: States.txt / Skills.txt as 1.14d has them: set D2_PATCH_INSTALLER\n"); return D2D_TEST_SKIPPED; }
    // States.txt's flags as game.exe's loader (FUN_00618100) reads them.
    assert(data->states.at("frozenarmor").panel_blue == d2d::rules::kPanelDefense);
    assert(data->states.at("antidote").panel_blue == d2d::rules::kPanelPoison);
    assert(data->states.at("lowerresist").panel_red == (d2d::rules::kPanelFire | d2d::rules::kPanelCold | d2d::rules::kPanelLightning | d2d::rules::kPanelPoison));
    assert(data->states.at("might").panel_blue == d2d::rules::kPanelDamage && data->states.at("enchant").panel_red == d2d::rules::kPanelDamage);

    d2d::rules::Rng rng(7);
    constexpr int kSorceress = 1;
    auto made = new_character(*data, kSorceress, "Headless", false, true, rng);
    Character character;
    character.character_class = kSorceress;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    World world(&*data, -1, -1);
    world.enter(character);
    auto& fight = world.fight;
    const std::uint32_t now = 2000 * kTickMs;
    fight.update_fighters(now);
    const auto plain = world.actor->character.panel;
    assert(plain.blue == 0 && plain.red == 0);

    // Frozen Armor (skill_armor_percent): more defense, blue.
    world.actor->self_states.push_back({ data->skills.by_name.at("Frozen Armor"), 5, now + 3000 * kTickMs });
    fight.update_fighters(now);
    const auto& armoured = world.actor->character.panel;
    assert(armoured.defense > plain.defense && armoured.blue == d2d::rules::kPanelDefense && armoured.red == 0);
    std::printf("OK: Frozen Armor's defense on the panel, blue\n");

    // An antidote: its poison resistance on the panel, blue.
    world.actor->self_states.clear();
    world.actor->potion_states[0] = { { { d2d::d2s::kPoisonResist, 50 } }, now + 3000 * kTickMs };
    fight.update_fighters(now);
    assert(world.actor->character.panel.res[3] == std::min<std::int64_t>(plain.res[3] + 50, plain.res_cap[3]));
    assert(world.actor->character.panel.blue == d2d::rules::kPanelPoison);
    std::printf("OK: an antidote's poison resistance on the panel, blue\n");

    // A monster's Lower Resist on the player: every resistance down, red.
    world.actor->potion_states[0] = {};
    world.actor->curses[0] = { data->skills.by_name.at("Lower Resist"), 10, now + 3000 * kTickMs };
    fight.update_fighters(now);
    const auto& cursed = world.actor->character.panel;
    for (std::size_t k = 0; k < 4; ++k) assert(cursed.res[k] < plain.res[k] && cursed.res[k] >= -100);
    assert(cursed.red == (d2d::rules::kPanelFire | d2d::rules::kPanelCold | d2d::rules::kPanelLightning | d2d::rules::kPanelPoison));
    std::printf("OK: Lower Resist's resistances on the panel, red\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "test_panel_states: %s\n", error.what());
    return 1;
}
