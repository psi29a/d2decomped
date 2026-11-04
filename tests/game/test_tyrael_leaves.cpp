// SPDX-License-Identifier: GPL-3.0-or-later
// test-links: game
// Tyrael leaving Duriel's Lair (FUN_005e73a0 -> FUN_0059c750): every player
// (FUN_005537d0) whose Seven Tombs bit 13 is set gets unit event 0x36, the
// class's act2_complete_tombs (LAB_0059c710: FUN_0065c310(quest 14, bit 13)
// then FUN_00553380(player, 0x36)); one without it hears nothing.

#include <ai.hpp>
#include <character.hpp>
#include <character_store.hpp>
#include <cues.hpp>
#include <gamedata.hpp>
#include <gamedata_load.hpp>
#include <quests.hpp>
#include <rules.hpp>
#include <world.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
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
    assert(data && data->town.id == 1);
    if (!data->patched) { std::printf("SKIP: these checks are 1.14d's (set D2_PATCH_INSTALLER)\n"); return D2D_TEST_SKIPPED; }
    using Tombs = d2d::rules::SevenTombsQuest;
    const auto* lair = data->level(Tombs::kLair);
    const auto voice = data->sound_index.find("sorceress_act2_complete_tombs");
    if (!lair || voice == data->sound_index.end()) { std::printf("SKIP: no Duriel's Lair or Sorceress voice\n"); return D2D_TEST_SKIPPED; }

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
    populate_level(*data, world.fight.spawning, *lair);         // its rooms up: Tyrael made (as test_game's Tristram)
    const auto tyrael = std::ranges::find(lair->npcs, Tombs::kTyrael, &Npc::hc_idx);
    if (tyrael == lair->npcs.end()) { std::printf("SKIP: no Tyrael in the Lair once populated\n"); return D2D_TEST_SKIPPED; }
    world.level = lair;
    world.npc_states = npc_start(*lair);
    world.actor->player.x = tyrael->x + 4;                          // 20 subtiles off: out of his 12
    world.actor->player.y = tyrael->y;
    const auto leaves = [&]() {
        world.tombs.tyrael_portal = true;
        world.tombs.tyrael_gone = false;
        world.npc_states[std::size_t(tyrael - lair->npcs.begin())].hidden = false;
        world.cues.due.clear();
        world.act2_tick();
        assert(world.tombs.tyrael_gone);
        return std::ranges::count(world.cues.due, voice->second, &Cues::Cue::sound);
    };
    assert(leaves() == 0);                                          // not a taker: nothing
    d2d::rules::quest::set(world.quests(), Tombs::kQuest, 13);
    assert(leaves() == 1);
    std::printf("OK: Tyrael's leaving speaks to the players with bit 13 only\n");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
