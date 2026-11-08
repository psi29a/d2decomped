// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The player's aura on monsters by its aurafilter (FUN_0056b7e0 ->
// FUN_0056b3e0, rules/aura_filter.hpp), with the game's MPQs (D2_MPQ_DIR;
// the 1.14d ones with D2_PATCH_INSTALLER): Might (0x12003) is over no
// monster, so its auratargetstate isn't drawn on one; Conviction is;
// Sanctuary takes the undead only, Holy Freeze no prime evil.

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
#include <string>

namespace fs = std::filesystem;
using namespace d2d::game;

int main() try {
    const char* mpq_dir = std::getenv("D2_MPQ_DIR");
    const char* home = std::getenv("HOME");
    const fs::path data_dir = mpq_dir ? fs::path(mpq_dir) : fs::path(home ? home : "") / "Workspace/private/diablo2";
    if (!fs::exists(data_dir / "d2data.mpq")) { std::printf("SKIP: no d2data.mpq in %s\n", data_dir.string().c_str()); return D2D_TEST_SKIPPED; }
    const char* patch = std::getenv("D2_PATCH_INSTALLER");
    auto data = load_game_data(data_dir, patch ? fs::path(patch) : fs::path{}, 0x1234);
    assert(data && data->town.id == 1);
    if (!data->patched) { std::printf("SKIP: these checks are 1.14d's (set D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }

    const auto skill = [&](const char* name) { return data->skills.get(data->skills.by_name.at(name)); };
    const auto* might = skill("Might");
    const auto* conviction = skill("Conviction");
    const auto* sanctuary = skill("Sanctuary");
    const auto* holy_freeze = skill("Holy Freeze");
    // Skills.txt's aurafilter, read (0 would be FUN_0056b7e0's 0x583).
    assert(might->aura_filter == 0x12003 && conviction->aura_filter == 0xa583 && sanctuary->aura_filter == 0xe786 && holy_freeze->aura_filter == 0x4a283);

    d2d::rules::Rng rng(7);
    constexpr int kPaladin = 3;
    auto made = new_character(*data, kPaladin, "Headless", false, true, rng);
    Character character;
    character.character_class = kPaladin;
    character.name = "Headless";
    character.header = made.header;
    character.stats = made.stats;
    character.items = made.items;
    character.panel = panel_stats(*data, character.header, character.items, character.stats);
    World world(&*data, -1, -1);
    world.enter(character);
    auto& fight = world.fight;
    auto& actor = *world.actor;
    fight.skill_level = [](int) { return 1; };

    const auto beside = [&](const char* monster_id) {
        const auto found = data->monsters.by_id.find(monster_id);
        assert(found != data->monsters.by_id.end());
        d2d::rules::Rng spawn_rng(3);
        return make_monster(*data, int(found->second), actor.player.x + 1, actor.player.y, spawn_rng, 0);
    };
    const Monster fallen = beside("fallen1"), zombie = beside("zombie1"), diablo = beside("diablo");

    // Might: over the monster beside the player, but not on it.
    actor.aura = might->id;
    assert(!fight.in_aura(fallen) && !fight.in_aura(zombie));
    std::printf("OK: Might is over no monster\n");
    // Conviction: on it, so its auratargetstate shows.
    actor.aura = conviction->id;
    assert(fight.in_aura(fallen) && fight.in_aura(zombie));
    Monster dead = fallen;
    dead.hit_points = 0;
    assert(!fight.in_aura(dead));
    std::printf("OK: Conviction is over the living ones\n");

    // The pulse's filter: Sanctuary the undead, Holy Freeze not Diablo.
    assert(fight.aura_takes(*sanctuary, zombie) && !fight.aura_takes(*sanctuary, fallen));
    assert(!fight.aura_takes(*holy_freeze, diablo) && fight.aura_takes(*holy_freeze, fallen));
    std::printf("OK: Sanctuary takes the undead, Holy Freeze no prime evil\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
