// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// The player's aura on monsters by its aurafilter (FUN_0056b7e0 ->
// FUN_0056b3e0, rules/aura_filter.hpp), with the game's MPQs (D2_MPQ_DIR;
// the 1.14d ones with D2_PATCH_INSTALLER): Might (0x12003) is over no
// monster, so its auratargetstate isn't drawn on one; Conviction is;
// Sanctuary takes the undead only, Holy Freeze no prime evil and only in
// a clear line (0x200, mask 4); in town none takes a monster (0x100,
// 0x2000).

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
    const Level* town = world.level;
    assert(town && town->id == 1);

    // Out of town, on a copy of the Blood Moor to wall in: the player on a
    // row of six subtiles clear of mask 4, a monster a cell (5 subtiles) east.
    Level moor = *data->level(d2d::rules::level_ids::kBloodMoor);
    const int width = moor.ds1.width() * 5, height = moor.ds1.height() * 5;
    const auto open = [&](int at_x, int at_y) { return (moor.walk[std::size_t(at_y) * std::size_t(width) + std::size_t(at_x)] & 0x04) == 0; };
    int row_x = -1, row_y = -1;
    for (int at_y = 0; at_y < height && row_x < 0; ++at_y)
        for (int at_x = 0; at_x + 5 < width && row_x < 0; ++at_x) {
            bool clear = true;
            for (int step = 0; step <= 5 && clear; ++step) clear = open(at_x + step, at_y);
            if (clear) { row_x = at_x; row_y = at_y; }
        }
    assert(row_x >= 0);
    world.level = &moor;
    actor.player.x = (float(row_x) + 0.5f) / 5;
    actor.player.y = (float(row_y) + 0.5f) / 5;
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
    const float from_x = actor.player.x, from_y = actor.player.y;
    assert(fight.aura_takes(*sanctuary, zombie, from_x, from_y) && !fight.aura_takes(*sanctuary, fallen, from_x, from_y));
    assert(!fight.aura_takes(*holy_freeze, diablo, from_x, from_y) && fight.aura_takes(*holy_freeze, fallen, from_x, from_y));
    std::printf("OK: Sanctuary takes the undead, Holy Freeze no prime evil\n");

    // A wall subtile (mask 4) between them: Holy Freeze's 0x200 fails,
    // Conviction (0xa583) has no line test.
    moor.walk[std::size_t(row_y) * std::size_t(width) + std::size_t(row_x + 2)] |= 0x04;
    assert(!fight.aura_takes(*holy_freeze, fallen, from_x, from_y) && fight.aura_takes(*conviction, fallen, from_x, from_y));
    std::printf("OK: Holy Freeze needs a clear line, Conviction doesn't\n");

    // In town: Conviction's 0x100 and 0x2000 take no monster.
    world.level = town;
    assert(!fight.aura_takes(*conviction, fallen, from_x, from_y) && !fight.in_aura(fallen));
    std::printf("OK: no aura takes a monster in town\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
